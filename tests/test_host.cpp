// The engine-free host layer: Session (song, graph, macros, timeline), bake
// to keyframes, and the Unreal host's target syntax.
#include "synth.hpp"

#include "ztrip/graph/compiler.hpp"
#include "ztrip/host/bake.hpp"
#include "ztrip/host/session.hpp"
#include "ztrip/host/unreal_target.hpp"
#include "ztrip/serialize/mgraph.hpp"
#include "ztrip/time/autodraft.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace ztrip;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

constexpr int kRate = 44100;
constexpr double kBpm = 120.0;
constexpr int kBars = 8;
constexpr double kFirstBeat = 0.5;

std::vector<std::uint8_t> wav_bytes(const std::vector<float>& x, int sr) {
    std::vector<std::uint8_t> out;
    const auto put = [&](std::uint32_t v, int bytes) {
        for (int i = 0; i < bytes; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    };
    const auto tag = [&](const char* t) { out.insert(out.end(), t, t + 4); };
    const auto data_size = static_cast<std::uint32_t>(x.size() * 2);
    tag("RIFF");
    put(36 + data_size, 4);
    tag("WAVE");
    tag("fmt ");
    put(16, 4);
    put(1, 2); // PCM
    put(1, 2); // mono
    put(static_cast<std::uint32_t>(sr), 4);
    put(static_cast<std::uint32_t>(sr * 2), 4);
    put(2, 2);
    put(16, 2);
    tag("data");
    put(data_size, 4);
    for (float s : x) {
        const auto v = static_cast<std::int16_t>(std::lround(std::clamp(s, -1.0f, 1.0f) * 32767.0f));
        put(static_cast<std::uint16_t>(v), 2);
    }
    return out;
}

// A scratch folder per test, removed afterwards.
struct TempDir {
    fs::path path;
    explicit TempDir(const std::string& name) {
        path = fs::temp_directory_path() / ("ztrip_host_" + name);
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void write_file(const fs::path& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}

fs::path write_groove(const fs::path& dir) {
    const auto bytes = wav_bytes(test::groove(kRate, kBpm, kBars, kFirstBeat), kRate);
    const fs::path wav = dir / "groove.wav";
    std::ofstream(wav, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                               static_cast<std::streamsize>(bytes.size()));
    return wav;
}

fs::path demo_graph() { return fs::path(ZTRIP_SOURCE_DIR) / "hosts/godot/project/demo/stage.mgraph.json"; }

std::string light_graph(double gain) {
    return R"({"format": "ztrip.mgraph", "version": 1, "name": "light",
      "nodes": [{"id": "kick", "type": "NoteLane", "params": {"lane": "Kick"}},
                {"id": "flash", "type": "ImpulseDecay", "params": {"decay_ms": 150}},
                {"id": "gain", "type": "Multiply", "params": {"b": )" +
           std::to_string(gain) + R"(}},
                {"id": "light", "type": "PropertyOut", "params": {"target": "Key:Intensity"}}],
      "edges": [{"from": "kick.trigger", "to": "flash.trigger"}, {"from": "flash.out", "to": "gain.a"},
                {"from": "gain.out", "to": "light.in"}],
      "macros": [{"name": "Gain", "node": "gain", "param": "b", "min": 0, "max": 10, "default": 4}]})";
}

// Waits for background analysis the way a host does: poll every frame.
host::SessionChanges poll_until_loaded(host::Session& s) {
    host::SessionChanges all;
    for (int i = 0; i < 2000 && s.loading(); ++i) {
        const auto c = s.poll(0.0);
        all.analysis |= c.analysis;
        all.graph |= c.graph;
        all.timeline |= c.timeline;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return all;
}

} // namespace

TEST_CASE("Session analyses a song in the background and drafts its timeline", "[host]") {
    TempDir dir("session");
    host::Session s({true, dir.path / "cache", 0.5});
    REQUIRE(s.load_audio_file(write_groove(dir.path)));
    CHECK(s.loading());
    const auto changes = poll_until_loaded(s);
    CHECK(changes.analysis);
    CHECK(changes.timeline);
    REQUIRE(s.analysis() != nullptr);
    CHECK_FALSE(s.from_cache());
    CHECK(s.audio_error().empty());
    CHECK(s.bpm() == Approx(kBpm).margin(1.0));
    CHECK(s.has_tempo());

    // Kick, Snare and Hats lanes, as in the Godot host; every kick drafted
    // within 20 ms of a true one.
    REQUIRE(s.sequence().lanes.size() == 3);
    const time::Lane* kick = s.sequence().find_lane("Kick");
    REQUIRE(kick != nullptr);
    CHECK(s.sequence().find_lane("Snare") != nullptr);
    CHECK(s.sequence().find_lane("Hats") != nullptr);
    CHECK(kick->notes.size() >= static_cast<std::size_t>(kBars * 4 - 2));
    for (const auto& n : kick->notes) {
        const double t = s.tempo().seconds_at(n.start_beat);
        const double beat = std::round((t - kFirstBeat) * kBpm / 60.0);
        CHECK(std::abs(t - (kFirstBeat + beat * 60.0 / kBpm)) < 0.020);
    }

    // The second load of the same audio comes from the .sigcache.
    host::Session again({false, dir.path / "cache", 0.5});
    REQUIRE(again.load_audio_file(write_groove(dir.path)));
    again.wait();
    CHECK(again.from_cache());

    host::Session missing({false, {}, 0.5});
    CHECK_FALSE(missing.load_audio_file(dir.path / "nope.wav"));
    CHECK_THAT(missing.audio_error(), Catch::Matchers::ContainsSubstring("not found"));
}

TEST_CASE("Session gives the same outputs as the evaluator the Godot host wires by hand", "[host]") {
    // The Godot player binds the analysis, a tempo map drafted from its beats
    // and an auto-drafted Note Sequence with the default lanes to the graph's
    // "main" slot. The Unreal host runs through Session; both must produce
    // identical values for the same song and .mgraph.json.
    TempDir dir("parity");
    const fs::path wav = write_groove(dir.path);
    host::Session s({false, {}, 0.5});
    REQUIRE(s.load_audio_file(wav));
    s.wait();
    REQUIRE(s.load_graph_file(demo_graph()).graph);
    REQUIRE(s.graph_error().empty());

    std::ifstream in(wav, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto outcome = host::analyze_wav(bytes, {}, "groove.wav");
    REQUIRE(outcome.result);
    time::TempoMap tempo = time::tempo_map_from_beats(outcome.result->beats);
    time::DraftSettings draft;
    draft.lanes = host::default_draft_lanes();
    time::NoteSequence seq = time::auto_draft(*outcome.result, tempo, draft);
    auto compiled = graph::compile(serialize::load_mgraph(demo_graph()));
    REQUIRE(compiled.ok());
    graph::Evaluator ev(std::move(compiled.graph));
    ev.bind_source("main", outcome.result.get());
    ev.bind_tempo("main", &tempo);
    ev.bind_sequence("main", &seq);

    REQUIRE(s.targets().size() == 8);
    double compared = 0;
    for (double t = 0.0; t < s.duration(); t += 1.0 / 60.0) {
        s.evaluate(t);
        ev.evaluate(t);
        REQUIRE(s.outputs().size() == ev.outputs().size());
        for (std::size_t i = 0; i < ev.outputs().size(); ++i) {
            REQUIRE(s.outputs()[i].target == ev.outputs()[i].target);
            REQUIRE(s.outputs()[i].value == ev.outputs()[i].value);
            ++compared;
        }
    }
    CHECK(compared > 1000);
    // And the light did something.
    double peak = 0.0;
    for (double t = 1.0; t < 3.0; t += 1.0 / 60.0) {
        s.evaluate(t);
        peak = std::max(peak, *s.output("Lights/Kick:light_energy"));
    }
    CHECK(peak > 5.0);
}

TEST_CASE("Session seeks deterministically", "[host]") {
    TempDir dir("seek");
    host::Session s({false, {}, 0.5});
    REQUIRE(s.load_audio_file(write_groove(dir.path)));
    s.wait();
    REQUIRE(s.load_graph_file(demo_graph()).graph);
    std::vector<double> played;
    for (int f = 0; f <= 600; ++f) {
        s.evaluate(f / 60.0);
        for (const auto& o : s.outputs()) played.push_back(o.value);
    }
    std::size_t k = 0;
    for (int f : {600, 17, 333, 0, 599, 240}) {
        s.evaluate(f / 60.0);
        for (std::size_t i = 0; i < s.outputs().size(); ++i)
            CHECK(s.outputs()[i].value == Approx(played[static_cast<std::size_t>(f) * s.outputs().size() + i]).margin(1e-5));
        ++k;
    }
    CHECK(k == 6);
}

TEST_CASE("Session keeps the last good graph, hot-reloads edits and keeps macros", "[host]") {
    TempDir dir("graph");
    const fs::path graph = dir.path / "light.mgraph.json";
    write_file(graph, light_graph(4.0));
    host::Session s({false, {}, 0.5});
    // A macro set before any graph is loaded applies when one is.
    s.set_macro("Gain", 7.0);
    REQUIRE(s.load_graph_file(graph).graph);
    CHECK(s.macro("Gain") == Approx(7.0));
    REQUIRE(s.macros().size() == 1);
    CHECK(s.macros()[0].value == Approx(7.0));
    CHECK(s.macros()[0].default_value == Approx(4.0));
    CHECK(s.set_macro("Gain", 50.0)); // clamped to the range
    CHECK(s.macro("Gain") == Approx(10.0));
    CHECK_FALSE(s.set_macro("Nope", 1.0));
    CHECK(s.targets() == std::vector<std::string>{"Key:Intensity"});

    // Nothing changed on disk: no reload.
    CHECK_FALSE(s.poll(1.0).graph);

    // A broken edit keeps the old graph running.
    write_file(graph, "{\"format\": \"ztrip.mgraph\", \"version\": 1, \"nodes\": [{\"id\": \"x\", \"type\": \"Nope\"}]}");
    fs::last_write_time(graph, fs::last_write_time(graph) + std::chrono::seconds(2));
    CHECK_FALSE(s.poll(1.0).graph);
    CHECK_THAT(s.graph_error(), Catch::Matchers::ContainsSubstring("light.mgraph.json"));
    CHECK(s.has_graph());
    CHECK(s.targets().size() == 1);

    // A good edit swaps in, keeping the macro's value.
    write_file(graph, light_graph(2.0));
    fs::last_write_time(graph, fs::last_write_time(graph) + std::chrono::seconds(4));
    CHECK_FALSE(s.poll(0.1).graph); // not time to look yet
    CHECK(s.poll(0.5).graph);
    CHECK(s.graph_error().empty());
    CHECK(s.macro("Gain") == Approx(10.0));

    CHECK(s.clear_graph().graph);
    CHECK_FALSE(s.has_graph());
    CHECK(s.outputs().empty());
    CHECK(s.load_graph_file(dir.path / "missing.mgraph.json").any() == false);
    CHECK_THAT(s.graph_error(), Catch::Matchers::ContainsSubstring("not found"));
}

TEST_CASE("Session saves the timeline and prefers it to a fresh draft", "[host]") {
    TempDir dir("timeline");
    const fs::path wav = write_groove(dir.path);
    const fs::path tempo = dir.path / "groove.tempo.json";
    const fs::path seq = dir.path / "groove.nseq.json";
    {
        host::Session s({false, {}, 0.5});
        s.set_timeline_files(tempo, seq);
        CHECK_FALSE(s.has_tempo());
        REQUIRE(s.load_audio_file(wav));
        s.wait();
        REQUIRE(s.has_tempo());
        // Hand edits: one extra kick, a nudge.
        s.sequence().find_lane("Kick")->notes.push_back({30.5, 0.25, 1.0f, -1, ""});
        s.sequence().find_lane("Kick")->sort();
        s.tempo().set_nudge(0.004);
        s.timeline_edited();
        REQUIRE(s.save_timeline());
        CHECK(fs::exists(tempo));
        CHECK(fs::exists(seq));
    }
    host::Session s({false, {}, 0.5});
    REQUIRE(s.load_audio_file(wav));
    s.set_timeline_files(tempo, seq);
    s.wait();
    const auto& kick = s.sequence().find_lane("Kick")->notes;
    CHECK(std::any_of(kick.begin(), kick.end(), [](const time::Note& n) { return n.start_beat == 30.5; }));
    CHECK(s.tempo().nudge() == Approx(0.004));
    const auto lines = s.status_lines();
    CHECK(std::none_of(lines.begin(), lines.end(), [](const std::string& l) { return l.find("(drafted)") != std::string::npos; }));

    // Drafting again replaces the notes and keeps the nudge.
    CHECK(s.draft_timeline("1/8").timeline);
    CHECK(std::none_of(s.sequence().find_lane("Kick")->notes.begin(), s.sequence().find_lane("Kick")->notes.end(),
                       [](const time::Note& n) { return n.start_beat == 30.5; }));
    CHECK(s.tempo().nudge() == Approx(0.004));
    CHECK_FALSE(s.draft_timeline("1/7").timeline);
    CHECK_THAT(s.timeline_error(), Catch::Matchers::ContainsSubstring("Unknown grid"));

    // Save As keeps the edits and switches files.
    s.sequence().find_lane("Kick")->notes.push_back({31.5, 0.25, 1.0f, -1, ""});
    REQUIRE(s.save_timeline_as(dir.path / "b.tempo.json", dir.path / "b.nseq.json"));
    CHECK(s.tempo_file() == dir.path / "b.tempo.json");
    CHECK(fs::exists(dir.path / "b.nseq.json"));
    CHECK(s.sequence().find_lane("Kick")->notes.back().start_beat == 31.5);

    host::Session unsaved({false, {}, 0.5});
    CHECK_FALSE(unsaved.save_timeline());
    CHECK_FALSE(unsaved.draft_timeline().timeline);
}

TEST_CASE("Session's status says what is loaded", "[host]") {
    TempDir dir("status");
    host::Session s({false, {}, 0.5});
    auto lines = s.status_lines();
    CHECK(lines == std::vector<std::string>{"No audio: set the audio file.", "No graph: set the graph file."});
    REQUIRE(s.load_audio_file(write_groove(dir.path)));
    s.wait();
    REQUIRE(s.load_graph_text(light_graph(3.0), "light.mgraph.json").graph);
    lines = s.status_lines();
    REQUIRE(lines.size() == 3);
    CHECK_THAT(lines[0], Catch::Matchers::StartsWith("groove.wav: 17.5 s, "));
    CHECK(lines[1] == "light.mgraph.json: 4 nodes, 1 targets, 1 macros");
    CHECK_THAT(lines[2], Catch::Matchers::ContainsSubstring("(drafted); notes: Kick "));
}

TEST_CASE("reduce_keys keeps every frame within tolerance", "[host][bake]") {
    std::vector<host::BakeKey> keys;
    for (int f = 0; f <= 300; ++f) {
        const double t = f / 30.0;
        keys.push_back({t, std::sin(t * 3.0) + (f % 30 == 0 ? 1.0 : 0.0)});
    }
    for (double tol : {0.0, 0.001, 0.01, 0.1}) {
        const auto reduced = host::reduce_keys(keys, tol);
        CHECK(host::max_error(reduced, keys) <= tol + 1e-8);
        CHECK(reduced.front().time == keys.front().time);
        CHECK(reduced.back().time == keys.back().time);
        if (tol >= 0.01) CHECK(reduced.size() < keys.size() / 2);
    }
    // Straight lines and flat runs collapse to their ends.
    std::vector<host::BakeKey> ramp;
    for (int f = 0; f <= 10; ++f) ramp.push_back({f * 0.1, f * 2.0});
    CHECK(host::reduce_keys(ramp, 0.0).size() == 2);
    CHECK(host::reduce_keys(std::vector<host::BakeKey>{{0.0, 1.0}}, 0.0).size() == 1);
}

TEST_CASE("bake samples what playback shows at every frame", "[host][bake]") {
    TempDir dir("bake");
    host::Session s({false, {}, 0.5});
    REQUIRE(s.load_audio_file(write_groove(dir.path)));
    s.wait();
    REQUIRE(s.load_graph_file(demo_graph()).graph);

    host::BakeSettings settings;
    settings.start_s = 2.0;
    settings.end_s = 6.0;
    settings.fps = 24.0;
    settings.tolerance = 0.0;
    const auto exact = host::bake(*s.evaluator(), s.duration(), settings);
    REQUIRE(exact.size() == 8);
    settings.tolerance = 0.05;
    settings.targets = {"Lights/Kick:light_energy", "Camera:fov", "Nope:x"};
    const auto reduced = host::bake(*s.evaluator(), s.duration(), settings);
    REQUIRE(reduced.size() == 3);
    CHECK(reduced[2].keys.empty());

    for (const auto& curve : exact) {
        CHECK(curve.frames == 97); // 4 s at 24 fps, both ends included
        REQUIRE_FALSE(curve.keys.empty());
        CHECK(curve.keys.front().time == Approx(2.0));
        CHECK(curve.keys.back().time == Approx(6.0));
        CHECK(curve.keys.size() <= curve.frames);
        // Every key is the value playback gives at that time.
        for (std::size_t i = 0; i < curve.keys.size(); i += 7) {
            s.evaluate(curve.keys[i].time);
            CHECK(*s.output(curve.target) == curve.keys[i].value);
        }
    }
    // The reduced kick light has far fewer keys and stays within tolerance.
    std::vector<host::BakeKey> frames;
    for (int f = 0; f <= 96; ++f) {
        s.evaluate(2.0 + f / 24.0);
        frames.push_back({2.0 + f / 24.0, *s.output("Lights/Kick:light_energy")});
    }
    CHECK(reduced[0].keys.size() < frames.size());
    CHECK(host::max_error(reduced[0].keys, frames) <= 0.05 + 1e-9);
    CHECK(std::max_element(frames.begin(), frames.end(), [](auto& a, auto& b) { return a.value < b.value; })
              ->value > 5.0);
}

TEST_CASE("Unreal targets parse", "[host][unreal]") {
    using K = host::UnrealTarget::Kind;
    std::string error;
    auto t = host::parse_unreal_target("KeyLight:Intensity", error);
    REQUIRE(t);
    CHECK(t->kind == K::Property);
    CHECK(t->actor == "KeyLight");
    CHECK(t->component.empty());
    CHECK(t->path == std::vector<std::string>{"Intensity"});

    t = host::parse_unreal_target(" Lamp / PointLightComponent0 : LightColor.R ", error);
    REQUIRE(t);
    CHECK(t->actor == "Lamp");
    CHECK(t->component == "PointLightComponent0");
    CHECK(t->path == std::vector<std::string>{"LightColor", "R"});
    CHECK(host::to_string(*t) == "Lamp/PointLightComponent0:LightColor.R");

    t = host::parse_unreal_target("Main Camera:location.y", error);
    REQUIRE(t);
    CHECK(t->kind == K::Location);
    CHECK(t->actor == "Main Camera");
    CHECK(t->axis == 1);
    t = host::parse_unreal_target("Cam:rotation.yaw", error);
    REQUIRE(t);
    CHECK(t->kind == K::Rotation);
    CHECK(t->axis == 2);
    CHECK(host::to_string(*t) == "Cam:rotation.yaw");
    t = host::parse_unreal_target("Cam:scale.z", error);
    REQUIRE(t);
    CHECK(t->kind == K::Scale);
    CHECK(t->axis == 2);

    t = host::parse_unreal_target("Orb:material.Glow", error);
    REQUIRE(t);
    CHECK(t->kind == K::Material);
    CHECK(t->slot == -1);
    CHECK(t->parameter == "Glow");
    t = host::parse_unreal_target("Orb/Mesh:material[2].Glow", error);
    REQUIRE(t);
    CHECK(t->slot == 2);
    CHECK(host::to_string(*t) == "Orb/Mesh:material[2].Glow");

    t = host::parse_unreal_target("Orb:material.Base Color", error);
    REQUIRE(t);
    CHECK(t->parameter == "Base Color");

    t = host::parse_unreal_target("Sparks:niagara.Rate", error);
    REQUIRE(t);
    CHECK(t->kind == K::Niagara);
    CHECK(t->parameter == "User.Rate");
    t = host::parse_unreal_target("Sparks:niagara.User.Rate", error);
    REQUIRE(t);
    CHECK(t->parameter == "User.Rate");
    CHECK(host::to_string(*t) == "Sparks:niagara.Rate");

    t = host::parse_unreal_target("Sparks:@burst", error);
    REQUIRE(t);
    CHECK(t->kind == K::Burst);
    t = host::parse_unreal_target("Stage:@call.OnKick", error);
    REQUIRE(t);
    CHECK(t->kind == K::Call);
    CHECK(t->parameter == "OnKick");

    t = host::parse_unreal_target("@mpc:Pulse", error);
    REQUIRE(t);
    CHECK(t->kind == K::Collection);
    CHECK(t->collection.empty());
    CHECK(t->parameter == "Pulse");
    t = host::parse_unreal_target("@mpc/MPC_Stage:Pulse", error);
    REQUIRE(t);
    CHECK(t->collection == "MPC_Stage");
    CHECK(host::to_string(*t) == "@mpc/MPC_Stage:Pulse");

    for (const char* bad : {"NoColon", ":Intensity", "Light:", "Light:a..b", "A/B/C:x", "A/:x", "Cam:location.w",
                            "Cam:location", "Cam:rotation.x.y", "Orb:material[x].Glow", "Orb:material[1]",
                            "Orb:material[-1].Glow", "Fx:niagara", "Fx:@boom", "Fx:@call.", "@foo:x", "@mpc:a.b",
                            "Orb:material_override:shader_parameter/glow", "Light:light energy", "Light:Intensity-1"}) {
        error.clear();
        CHECK_FALSE(host::parse_unreal_target(bad, error));
        CHECK_FALSE(error.empty());
    }
    host::parse_unreal_target("Orb:material_override:shader_parameter/glow", error);
    CHECK_THAT(error, Catch::Matchers::ContainsSubstring("Target Map"));
}

TEST_CASE("UTF-8 paths survive the round trip", "[host]") {
    const std::string name = "Пёс_テスト_Motörhead.wav";
    const fs::path p = host::path_from_utf8(name);
    CHECK(host::path_to_utf8(p) == name);
    CHECK(host::path_to_utf8(p.filename()) == name);
}
