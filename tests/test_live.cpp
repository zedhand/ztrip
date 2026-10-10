#include "graph_fixtures.hpp"
#include "synth.hpp"

#include "ztrip/analysis/live.hpp"
#include "ztrip/io/spsc_ring.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

using namespace ztrip;
using analysis::LiveAnalyzer;
using ztrip::test::GraphBuilder;

namespace {

constexpr int kSr = 48000;

// Pushes `x` in blocks whose sizes cycle through `sizes`.
void push_blocks(LiveAnalyzer& live, const std::vector<float>& x, std::vector<std::size_t> sizes) {
    std::size_t i = 0;
    for (std::size_t k = 0; i < x.size(); ++k) {
        const std::size_t n = std::min(sizes[k % sizes.size()], x.size() - i);
        live.push(std::span<const float>(x.data() + i, n));
        i += n;
    }
}

// Kick times of test::groove: one per beat.
std::vector<double> beat_times(double bpm, int bars, double first = 0.0) {
    std::vector<double> t;
    for (int b = 0; b < bars * 4; ++b) t.push_back(first + b * 60.0 / bpm);
    return t;
}

// Reads the numeric array stored under "key": [...] in a truth file.
std::vector<double> truth_times(const std::filesystem::path& file, const std::string& key) {
    std::ifstream in(file);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    const auto at = text.find("\"" + key + "\"");
    REQUIRE(at != std::string::npos);
    const auto open = text.find('[', at);
    const auto close = text.find(']', open);
    std::vector<double> out;
    std::stringstream list(text.substr(open + 1, close - open - 1));
    std::string item;
    while (std::getline(list, item, ',')) out.push_back(std::stod(item));
    return out;
}

struct Delays {
    double recall = 0.0, precision = 0.0, mean_ms = 0.0, worst_ms = 0.0;
};

// Matches each true hit to the first unused detection from 5 ms before it to
// 40 ms after (live detections come late, never much early).
Delays delays(const std::vector<analysis::Trigger>& det, const std::vector<double>& truth, float min_velocity) {
    std::vector<bool> used(det.size(), false);
    std::size_t hits = 0, count = 0;
    double sum = 0.0, worst = 0.0;
    for (const auto& d : det) count += d.velocity >= min_velocity ? 1 : 0;
    for (double t : truth) {
        for (std::size_t i = 0; i < det.size(); ++i) {
            const double d = det[i].time - t;
            if (used[i] || det[i].velocity < min_velocity || d < -0.005 || d > 0.04) continue;
            used[i] = true;
            ++hits;
            sum += d;
            worst = std::max(worst, d);
            break;
        }
    }
    Delays r;
    r.recall = static_cast<double>(hits) / static_cast<double>(truth.size());
    r.precision = count ? static_cast<double>(hits) / static_cast<double>(count) : 0.0;
    r.mean_ms = hits ? 1000.0 * sum / static_cast<double>(hits) : 0.0;
    r.worst_ms = 1000.0 * worst;
    return r;
}

} // namespace

TEST_CASE("live analysis has the offline signal and event names", "[live]") {
    LiveAnalyzer live(kSr);
    const auto x = test::groove(kSr, 120.0, 1);
    const auto offline = analysis::analyze(x, kSr, {});
    for (const auto& s : offline.signals) {
        INFO(s.name);
        CHECK(live.result().find_signal(s.name) != nullptr);
    }
    for (const auto& e : offline.events) {
        INFO(e.name);
        CHECK(live.result().find_events(e.name) != nullptr);
    }
}

TEST_CASE("live analysis does not depend on block size", "[live][determinism]") {
    const auto x = test::groove(kSr, 128.0, 2);
    LiveAnalyzer whole(kSr);
    whole.push(x);
    LiveAnalyzer pieces(kSr);
    push_blocks(pieces, x, {1, 7, 480, 33, 1024, 256, 5000});

    REQUIRE(whole.time() == pieces.time());
    const auto& a = whole.result();
    const auto& b = pieces.result();
    for (std::size_t i = 0; i < a.signals.size(); ++i) {
        INFO(a.signals[i].name);
        CHECK(a.signals[i].values == b.signals[i].values);
    }
    for (std::size_t i = 0; i < a.events.size(); ++i) {
        INFO(a.events[i].name);
        REQUIRE(a.events[i].events.size() == b.events[i].events.size());
        for (std::size_t k = 0; k < a.events[i].events.size(); ++k) {
            CHECK(a.events[i].events[k].time == b.events[i].events[k].time);
            CHECK(a.events[i].events[k].velocity == b.events[i].events[k].velocity);
        }
    }
}

TEST_CASE("live signals reach the stream time at the signal rate", "[live]") {
    LiveAnalyzer live(kSr);
    // Frames come every 256 samples, so 2 s ends on a frame and 2.003 s doesn't.
    std::vector<float> x(2 * kSr + 150, 0.0f);
    live.push(x);
    CHECK(live.time() == 2.0);
    for (const auto& s : live.result().signals) CHECK(s.values.size() == 241);
    CHECK(live.result().duration_s == 2.0);
}

TEST_CASE("live onsets land on drum hits within 30 ms, never early", "[live]") {
    const double bpm = 120.0;
    // Kicks and hats; the snare's body would sit in the Bass band too.
    const auto x = test::groove(kSr, bpm, 4, 0.5, false);
    LiveAnalyzer live(kSr);
    push_blocks(live, x, {480});
    const auto truth = beat_times(bpm, 4, 0.5);
    const auto& onsets = live.result().find_events("onset.Bass")->events;

    std::size_t found = 0;
    double worst = 0.0;
    for (double t : truth) {
        const auto it = std::find_if(onsets.begin(), onsets.end(),
                                     [&](const analysis::Trigger& o) { return o.time >= t && o.time <= t + 0.03; });
        if (it != onsets.end()) {
            ++found;
            worst = std::max(worst, it->time - t);
        }
    }
    INFO("worst delay " << worst * 1000.0 << " ms, " << onsets.size() << " Bass onsets");
    CHECK(found == truth.size());
    // Like offline, the kick's tail adds a faint onset 0.35 s on; nothing
    // else of any strength.
    const auto strong = std::count_if(onsets.begin(), onsets.end(),
                                      [](const analysis::Trigger& o) { return o.velocity >= 0.3f; });
    CHECK(strong == static_cast<std::ptrdiff_t>(truth.size()));
}

TEST_CASE("live levels follow the music and recover after a loud passage", "[live]") {
    // 3 s of loud bass, 15 s silence, 3 s of the same bass 30 dB quieter.
    std::vector<float> x(static_cast<std::size_t>(21 * kSr), 0.0f);
    test::add_bass(x, kSr, 0.0, 80.0, 3.0, 0.5f);
    test::add_bass(x, kSr, 18.0, 80.0, 3.0, 0.5f * 0.0316f);
    LiveAnalyzer live(kSr);
    live.push(x);
    const auto& r = live.result();
    const auto* bass = r.find_signal("band.Bass");
    CHECK(r.sample(*bass, 2.0) > 0.9);
    CHECK(r.sample(*bass, 10.0) == 0.0f);
    // The peak has drifted down towards the quiet level, so it reads high again.
    CHECK(r.sample(*bass, 20.0) > 0.5);
    const auto* env = r.find_signal("env.Bass");
    CHECK(r.sample(*env, 2.0) > 0.8);
}

TEST_CASE("a live graph fires on every onset when evaluated as audio arrives", "[live][graph]") {
    const auto x = test::groove(kSr, 120.0, 4, 0.5, false);
    LiveAnalyzer live(kSr);
    GraphBuilder g;
    g.node("onset", "AudioOnset", {{"band", std::string("Bass")}, {"min_velocity", 0.3}});
    g.node("decay", "ImpulseDecay", {{"decay_ms", 100.0}});
    g.edge("onset.trigger", "decay.trigger").out("light", "decay.out");
    graph::EvaluatorSettings settings;
    settings.live = true;
    auto ev = g.evaluator(settings);
    ev.bind_source("main", &live.result());

    // 10 ms blocks, as an audio callback would deliver them, evaluating the
    // graph at the analysed time after each one.
    int fired = 0;
    const std::size_t block = kSr / 100;
    for (std::size_t i = 0; i < x.size(); i += block) {
        live.push(std::span<const float>(x.data() + i, std::min(block, x.size() - i)));
        // Tick by tick up to the analysed time, to see every tick's trigger.
        const auto last = static_cast<std::int64_t>(std::floor(live.time() * 120.0 + 1e-6));
        for (std::int64_t k = ev.current_step() + 1; k <= last; ++k) {
            ev.evaluate(static_cast<double>(k) / 120.0);
            if (ev.port_value("onset", "trigger")->fired) ++fired;
        }
    }
    const auto& onsets = live.result().find_events("onset.Bass")->events;
    CHECK(fired == std::count_if(onsets.begin(), onsets.end(),
                                 [](const analysis::Trigger& o) { return o.velocity >= 0.3f; }));
    CHECK(fired == 16);
}

TEST_CASE("the SPSC ring hands values across threads in order", "[live][io]") {
    io::SpscRing<std::uint32_t> ring(1000);
    constexpr std::uint32_t kCount = 200000;
    std::thread producer([&] {
        std::uint32_t next = 0;
        std::vector<std::uint32_t> block(64);
        while (next < kCount) {
            const auto n = std::min<std::uint32_t>(64, kCount - next);
            for (std::uint32_t i = 0; i < n; ++i) block[i] = next + i;
            const std::size_t written = ring.write(std::span<const std::uint32_t>(block.data(), n));
            next += static_cast<std::uint32_t>(written);
            if (written < n) std::this_thread::yield();
        }
    });
    std::vector<std::uint32_t> got;
    std::vector<std::uint32_t> buf(100);
    while (got.size() < kCount) {
        const std::size_t n = ring.read(buf);
        got.insert(got.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n));
    }
    producer.join();
    bool in_order = true;
    for (std::uint32_t i = 0; i < kCount; ++i) in_order = in_order && got[i] == i;
    CHECK(in_order);
}

TEST_CASE("the SPSC ring drops and counts what does not fit", "[live][io]") {
    io::SpscRing<float> ring(4);
    const std::vector<float> six{1, 2, 3, 4, 5, 6};
    CHECK(ring.write(six) == 4);
    CHECK(ring.take_dropped() == 2);
    CHECK(ring.take_dropped() == 0);
    std::vector<float> out(10);
    CHECK(ring.read(out) == 4);
    CHECK(out[3] == 4.0f);
    CHECK(ring.available() == 0);
}

TEST_CASE("track 01 live: kicks and hats are found within 20 ms", "[live][reference]") {
    const char* env = std::getenv("ZTRIP_REFERENCE_TRACKS");
    const std::filesystem::path dir = env ? std::filesystem::path(env) : std::filesystem::path();
    const auto wav = dir / "01_four_on_the_floor_128bpm.wav";
    const auto truth = dir / "01_four_on_the_floor_128bpm.truth.json";
    if (dir.empty() || !std::filesystem::exists(wav) || !std::filesystem::exists(truth)) {
        SKIP("set ZTRIP_REFERENCE_TRACKS to the reference-tracks folder to run");
    }
    const auto audio = io::read_wav(wav);
    LiveAnalyzer live(audio.sample_rate);
    const auto mono = io::to_mono(audio);
    push_blocks(live, mono, {512});

    const Delays kick = delays(live.result().find_events("onset.Sub")->events, truth_times(truth, "kick"), 0.3f);
    INFO("kick recall " << kick.recall << " precision " << kick.precision << " mean " << kick.mean_ms
                        << " ms worst " << kick.worst_ms << " ms");
    CHECK(kick.recall >= 0.97);
    CHECK(kick.precision >= 0.97);
    CHECK(kick.worst_ms <= 20.0);

    const Delays hat = delays(live.result().find_events("onset.Air")->events, truth_times(truth, "hat"), 0.3f);
    INFO("hat recall " << hat.recall << " precision " << hat.precision << " mean " << hat.mean_ms
                       << " ms worst " << hat.worst_ms << " ms");
    CHECK(hat.recall >= 0.97);
    CHECK(hat.precision >= 0.9);
    CHECK(hat.worst_ms <= 20.0);
}
