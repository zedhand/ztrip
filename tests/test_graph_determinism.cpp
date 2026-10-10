// Determinism suite: seeking to any time t must give the same outputs (within
// 1e-5) as playing from 0 to t, whatever the host frame rate or seek order.
#include "graph_fixtures.hpp"
#include "ztrip/serialize/mgraph.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <random>

using namespace ztrip;

namespace {

constexpr double kRate = 120.0;
constexpr double kTolerance = 1e-5;
constexpr double kSongSeconds = 60.0;

std::filesystem::path example(const char* name) {
    return std::filesystem::path(ZTRIP_SOURCE_DIR) / "examples" / name;
}

std::unique_ptr<graph::CompiledGraph> compile_file(const char* name) {
    auto r = graph::compile(serialize::load_mgraph(example(name)));
    INFO(r.error_text());
    REQUIRE(r.ok());
    return std::move(r.graph);
}

std::vector<double> outputs_of(const graph::Evaluator& ev) {
    std::vector<double> v;
    for (const auto& o : ev.outputs()) v.push_back(o.value);
    return v;
}

double max_diff(const std::vector<double>& a, const std::vector<double>& b) {
    REQUIRE(a.size() == b.size());
    double d = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) d = std::max(d, std::abs(a[i] - b[i]));
    return d;
}

// Every tick's outputs from playing 0..duration in order.
std::vector<std::vector<double>> play_reference(graph::Evaluator& ev, double duration) {
    std::vector<std::vector<double>> ref;
    const auto ticks = static_cast<int>(duration * kRate);
    for (int i = 0; i <= ticks; ++i) {
        ev.evaluate(i / kRate);
        ref.push_back(outputs_of(ev));
    }
    return ref;
}

std::int64_t tick_of(double t) { return static_cast<std::int64_t>(std::floor(t * kRate + 1e-6)); }

struct Fixture {
    analysis::AnalysisResult song = test::synthetic_song(kSongSeconds);
    std::vector<std::vector<double>> ref;

    Fixture() {
        auto ev = make();
        ref = play_reference(ev, kSongSeconds);
    }
    graph::Evaluator make(graph::EvaluatorSettings settings = {}) const {
        graph::Evaluator ev(compile_file("kitchen_sink.mgraph.json"), settings);
        ev.bind_source("main", &song);
        return ev;
    }
    void check_at(const graph::Evaluator& ev, double t) const {
        const auto n = static_cast<std::size_t>(tick_of(t));
        INFO("t = " << t << ", tick " << n);
        CHECK(max_diff(outputs_of(ev), ref[n]) <= kTolerance);
    }
};

const Fixture& fixture() {
    static const Fixture f;
    return f;
}

} // namespace

TEST_CASE("the reference play actually moves every output", "[graph][determinism]") {
    const auto& f = fixture();
    const std::size_t outputs = f.ref.front().size();
    REQUIRE(outputs == 6);
    for (std::size_t k = 0; k < outputs; ++k) {
        double lo = 1e9, hi = -1e9;
        for (const auto& row : f.ref) {
            lo = std::min(lo, row[k]);
            hi = std::max(hi, row[k]);
        }
        INFO("output " << k);
        CHECK(hi - lo > 0.1);
    }
}

TEST_CASE("cold seeks match playing from 0", "[graph][determinism]") {
    const auto& f = fixture();
    std::mt19937 rng(1234);
    std::uniform_real_distribution<double> time(0.0, kSongSeconds);
    for (int i = 0; i < 40; ++i) {
        const double t = time(rng);
        auto ev = f.make();
        ev.evaluate(t);
        f.check_at(ev, t);
    }
}

TEST_CASE("random seeks back and forth match playing from 0", "[graph][determinism]") {
    const auto& f = fixture();
    auto ev = f.make();
    std::mt19937 rng(99);
    std::uniform_real_distribution<double> time(0.0, kSongSeconds);
    std::uniform_real_distribution<double> nudge(-0.5, 0.5);
    double t = 0.0;
    for (int i = 0; i < 400; ++i) {
        // Mix long jumps with short scrubs either way.
        t = (i % 3 == 0) ? time(rng) : std::clamp(t + nudge(rng), 0.0, kSongSeconds);
        ev.evaluate(t);
        f.check_at(ev, t);
    }
}

TEST_CASE("playing at any host frame rate matches the tick grid", "[graph][determinism]") {
    const auto& f = fixture();
    for (double fps : {24.0, 30.0, 59.94, 60.0, 144.0}) {
        auto ev = f.make();
        for (double t = 0.0; t <= kSongSeconds; t += 1.0 / fps) {
            ev.evaluate(t);
            f.check_at(ev, t);
        }
    }
    // Jittery frame times, as from a real game loop.
    auto ev = f.make();
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> frame(1.0 / 200.0, 1.0 / 20.0);
    for (double t = 0.0; t <= kSongSeconds; t += frame(rng)) {
        ev.evaluate(t);
        f.check_at(ev, t);
    }
}

TEST_CASE("checkpoints bound the cost of a seek", "[graph][determinism]") {
    const auto& f = fixture();
    auto ev = f.make();
    ev.evaluate(kSongSeconds); // plays through, checkpointing every second
    const auto before = ev.ticks_evaluated();
    ev.evaluate(30.55);
    CHECK(ev.ticks_evaluated() - before <= static_cast<std::uint64_t>(kRate) + 1);
    f.check_at(ev, 30.55);
}

TEST_CASE("results are identical across evaluators and a save/load round trip", "[graph][determinism]") {
    const auto& f = fixture();
    const auto doc = serialize::load_mgraph(example("kitchen_sink.mgraph.json"));
    const auto reloaded = serialize::parse_mgraph(serialize::write_mgraph(doc));
    auto r = graph::compile(reloaded);
    REQUIRE(r.ok());
    graph::Evaluator ev(std::move(r.graph));
    ev.bind_source("main", &f.song);
    for (int i = 0; i <= static_cast<int>(kSongSeconds * kRate); i += 7) {
        ev.evaluate(i / kRate);
        CHECK(outputs_of(ev) == f.ref[static_cast<std::size_t>(i)]); // bit-identical
    }
}

TEST_CASE("after a macro change, seeks match a fresh play with the new value", "[graph][determinism]") {
    const auto& f = fixture();
    auto changed = f.make();
    changed.evaluate(20.0);
    REQUIRE(changed.set_macro("Glow", 1.7));
    auto fresh = f.make();
    fresh.set_macro("Glow", 1.7);
    const auto ref = play_reference(fresh, 25.0);
    for (double t : {20.0, 3.3, 24.9, 12.0}) {
        changed.evaluate(t);
        CHECK(max_diff(outputs_of(changed), ref[static_cast<std::size_t>(tick_of(t))]) <= kTolerance);
    }
}

TEST_CASE("hot-swapping keeps state for nodes that persist", "[graph][determinism]") {
    const auto& f = fixture();
    auto doc = serialize::load_mgraph(example("kitchen_sink.mgraph.json"));
    auto ev = f.make();
    ev.evaluate(10.0);
    const double before = ev.port_value("air_smooth", "out")->v;

    // Add a node and an output; air_smooth keeps its envelope across the swap.
    doc.nodes.push_back({"extra", "Constant", {{"value", 0.5}}, 0.0f, 0.0f});
    doc.nodes.push_back({"out_extra", "PropertyOut", {{"target", std::string("Extra:x")}}, 0.0f, 0.0f});
    doc.edges.push_back({"extra", "out", "out_extra", "in"});
    auto r = graph::compile(doc);
    REQUIRE(r.ok());
    ev.swap_graph(std::move(r.graph));
    CHECK(ev.port_value("air_smooth", "out")->v == before);
    ev.evaluate(10.0 + 1.0 / kRate); // keeps playing from the swapped state
    auto reference = f.make();
    reference.evaluate(10.0 + 1.0 / kRate);
    CHECK(ev.port_value("air_smooth", "out")->v == reference.port_value("air_smooth", "out")->v);
    CHECK(ev.output("Extra:x") == 0.5);
}

TEST_CASE("live mode is causal and resets on a backwards jump", "[graph][determinism]") {
    const auto& f = fixture();
    graph::EvaluatorSettings live;
    live.live = true;
    auto ev = f.make(live);
    for (int i = 0; i <= 600; ++i) ev.evaluate(i / kRate);
    f.check_at(ev, 5.0); // playing forward live is the same computation
    const auto ticks = ev.ticks_evaluated();
    ev.evaluate(2.0);    // no replay: state starts over at 2 s
    CHECK(ev.ticks_evaluated() == ticks + 1);
    CHECK(ev.current_step() == 240);
}
