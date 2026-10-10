#include "graph_fixtures.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace ztrip;
using Catch::Approx;
using ztrip::test::GraphBuilder;
using ztrip::test::signal_source;

namespace {

constexpr double kRate = 120.0;

double t_of(int step) { return step / kRate; }

// Evaluates `node.port` at each tick of a step input (0 for `low` ticks, then 1).
std::vector<double> step_response(const std::string& type, std::map<std::string, graph::ParamValue> params,
                                  int low, int high, const std::string& out_port = "out") {
    std::vector<float> values(static_cast<std::size_t>(low), 0.0f);
    values.resize(static_cast<std::size_t>(low + high), 1.0f);
    const auto src = signal_source("x", values);
    GraphBuilder g;
    g.node("src", "AudioBand", {{"signal", std::string("x")}}).node("n", type, std::move(params));
    g.edge("src.out", "n.in").out("o", "n." + out_port);
    auto ev = g.evaluator();
    ev.bind_source("main", &src);
    std::vector<double> out;
    for (int i = 0; i < low + high; ++i) {
        ev.evaluate(t_of(i));
        out.push_back(*ev.output("o"));
    }
    return out;
}

} // namespace

TEST_CASE("AudioBand samples the bound signal by slot and name", "[graph][nodes]") {
    const auto song = test::synthetic_song(5.0);
    GraphBuilder g;
    g.node("env", "AudioBand", {{"band", std::string("Sub")}});
    g.node("raw", "AudioBand", {{"band", std::string("Sub")}, {"smoothed", false}});
    g.node("drums", "AudioBand", {{"source", std::string("drums")}, {"band", std::string("Sub")}});
    g.out("o_env", "env.out").out("o_raw", "raw.out").out("o_drums", "drums.out");
    auto ev = g.evaluator();
    ev.evaluate(1.0);
    CHECK(ev.output("o_env") == 0.0); // nothing bound yet
    ev.bind_source("main", &song);
    ev.evaluate(1.0);
    CHECK(*ev.output("o_env") == Approx(song.find_signal("env.Sub")->values[120]));
    CHECK(*ev.output("o_raw") == Approx(song.find_signal("band.Sub")->values[120]));
    CHECK(ev.output("o_drums") == 0.0);
    ev.bind_source("drums", &song); // repointing a slot
    ev.evaluate(1.0);
    CHECK(*ev.output("o_drums") == Approx(song.find_signal("env.Sub")->values[120]));
}

TEST_CASE("AudioOnset fires on the tick each onset falls in", "[graph][nodes]") {
    // Tick n covers ((n-1)/120, n/120]; 0.1 s is tick 12, 0.104 is tick 13.
    const auto src = signal_source("x", {}, "onset.Sub", {{0.1, 0.8f}, {0.104, 0.3f}, {0.5, 0.2f}});
    GraphBuilder g;
    g.node("on", "AudioOnset", {{"band", std::string("Sub")}, {"min_velocity", 0.25}}).out("o", "on.trigger");
    auto ev = g.evaluator();
    ev.bind_source("main", &src);
    std::vector<int> fired;
    for (int i = 0; i < 120; ++i) {
        ev.evaluate(t_of(i));
        if (ev.port_value("on", "trigger")->fired) fired.push_back(i);
    }
    CHECK(fired == std::vector<int>{12, 13}); // 0.5 s is below min_velocity
    ev.evaluate(t_of(12));
    CHECK(*ev.output("o") == Approx(0.8)); // a trigger read as Continuous is its velocity
}

TEST_CASE("BeatClock follows the beat grid with downbeat at beat 0", "[graph][nodes]") {
    const auto song = test::synthetic_song(10.0);
    const auto& b = song.beats.beats;
    GraphBuilder g;
    g.node("clock", "BeatClock").out("o", "clock.clock");
    auto ev = g.evaluator();
    ev.bind_source("main", &song);

    ev.evaluate(b[1]); // first downbeat (downbeat_phase = 1)
    CHECK(*ev.output("o") == Approx(0.0).margin(0.03));
    const double mid = (b[5] + b[6]) / 2.0;
    ev.evaluate(mid);
    CHECK(*ev.output("o") == Approx(4.5).margin(0.03));
    CHECK(ev.port_value("clock", "bar_phase")->v == Approx(4.5 / 4.0 - 1.0).margin(0.01));

    int beats = 0, bars = 0;
    ev.reset();
    for (int i = 0; i <= static_cast<int>(10.0 * kRate); ++i) {
        ev.evaluate(t_of(i));
        beats += ev.port_value("clock", "beat")->fired;
        bars += ev.port_value("clock", "bar")->fired;
    }
    CHECK(beats == static_cast<int>(b.size()));
    CHECK(bars == static_cast<int>(song.beats.downbeats.size()));
}

TEST_CASE("BeatClock runs at its fallback tempo with no grid", "[graph][nodes]") {
    GraphBuilder g;
    g.node("clock", "BeatClock", {{"bpm", 90.0}, {"beats_per_bar", 3.0}}).out("o", "clock.clock");
    auto ev = g.evaluator();
    int beats = 0, bars = 0;
    for (int i = 0; i < 4 * 120; ++i) { // 4 s at 1.5 beats/s, from t = 0
        ev.evaluate(t_of(i));
        beats += ev.port_value("clock", "beat")->fired;
        bars += ev.port_value("clock", "bar")->fired;
    }
    CHECK(*ev.output("o") == Approx(1.5 * (479.0 / 120.0)));
    CHECK(beats == 6); // beats 0..5
    CHECK(bars == 2);  // beats 0 and 3
}

TEST_CASE("Smooth matches the spec's follower", "[graph][nodes]") {
    const auto out = step_response("Smooth", {{"attack_ms", 50.0}, {"release_ms", 100.0}}, 1, 240);
    // After 6 ticks at 120 Hz = 50 ms, the attack reaches 1 - 1/e.
    CHECK(out[6] == Approx(1.0 - std::exp(-1.0)).epsilon(1e-9));
    CHECK(out.back() == Approx(1.0).epsilon(1e-6));
}

TEST_CASE("Threshold opens and closes with hysteresis", "[graph][nodes]") {
    const std::vector<float> x = {0.0f, 0.52f, 0.6f, 0.47f, 0.44f, 0.6f};
    const auto src = signal_source("x", x);
    GraphBuilder g;
    g.node("src", "AudioBand", {{"signal", std::string("x")}}).node("t", "Threshold", {{"hysteresis", 0.1}});
    g.edge("src.out", "t.in").out("o", "t.gate");
    auto ev = g.evaluator();
    ev.bind_source("main", &src);
    std::vector<double> gate;
    std::vector<int> rises, falls;
    for (int i = 0; i < 6; ++i) {
        ev.evaluate(t_of(i));
        gate.push_back(*ev.output("o"));
        if (ev.port_value("t", "rise")->fired) rises.push_back(i);
        if (ev.port_value("t", "fall")->fired) falls.push_back(i);
    }
    CHECK(gate == std::vector<double>{0, 0, 1, 1, 0, 1});
    CHECK(rises == std::vector<int>{2, 5});
    CHECK(falls == std::vector<int>{4});
}

TEST_CASE("RemapRange, Clamp, Add and Multiply", "[graph][nodes]") {
    GraphBuilder g;
    g.node("c", "Constant", {{"value", 0.75}});
    g.node("r", "RemapRange", {{"in_min", 0.5}, {"in_max", 1.0}, {"out_min", 10.0}, {"out_max", 20.0}});
    g.node("r2", "RemapRange", {{"in_min", 0.0}, {"in_max", 0.5}, {"out_max", 2.0}, {"clamp", false}});
    g.node("cl", "Clamp", {{"min", 12.0}, {"max", 14.0}});
    g.node("add", "Add", {{"b", 0.5}}).node("mul", "Multiply", {{"b", -2.0}});
    g.edge("c.out", "r.in").edge("c.out", "r2.in").edge("r.out", "cl.in").edge("c.out", "add.a").edge("add.out", "mul.a");
    g.out("o_r", "r.out").out("o_r2", "r2.out").out("o_cl", "cl.out").out("o_mul", "mul.out");
    auto ev = g.evaluator();
    ev.evaluate(0.0);
    CHECK(*ev.output("o_r") == Approx(15.0));
    CHECK(*ev.output("o_r2") == Approx(3.0));
    CHECK(*ev.output("o_cl") == Approx(14.0));
    CHECK(*ev.output("o_mul") == Approx(-2.5));
}

TEST_CASE("ResponseCurve presets and custom points", "[graph][nodes]") {
    auto curve = [](std::map<std::string, graph::ParamValue> params, double x) {
        params["in"] = x;
        GraphBuilder g;
        g.node("c", "ResponseCurve", std::move(params)).out("o", "c.out");
        auto ev = g.evaluator();
        ev.evaluate(0.0);
        return *ev.output("o");
    };
    CHECK(curve({{"curve", std::string("linear")}}, 0.3) == Approx(0.3));
    CHECK(curve({{"curve", std::string("ease_in")}, {"amount", 2.0}}, 0.5) == Approx(0.25));
    CHECK(curve({{"curve", std::string("ease_out")}, {"amount", 2.0}}, 0.5) == Approx(0.75));
    CHECK(curve({{"curve", std::string("ease_in_out")}, {"amount", 3.0}}, 0.25) == Approx(0.0625));
    CHECK(curve({{"curve", std::string("smoothstep")}}, 0.5) == Approx(0.5));
    CHECK(curve({{"curve", std::string("exponential")}}, 0.0) == Approx(0.0));
    CHECK(curve({{"curve", std::string("exponential")}}, 1.0) == Approx(1.0));
    CHECK(curve({{"curve", std::string("logarithmic")}}, 0.5) > 0.5);
    CHECK(curve({{"curve", std::string("linear")}}, 1.7) == Approx(1.0)); // input clamped to 0..1
    const std::vector<double> pts = {0.0, 1.0, 0.5, 0.0, 1.0, 1.0};
    CHECK(curve({{"curve", std::string("custom")}, {"points", pts}}, 0.25) == Approx(0.5));
    CHECK(curve({{"curve", std::string("custom")}, {"points", pts}}, 0.75) == Approx(0.5));
}

TEST_CASE("ImpulseDecay jumps to velocity and decays by its time constant", "[graph][nodes]") {
    const auto src = signal_source("x", {}, "onset.full", {{0.1, 0.8f}});
    GraphBuilder g;
    g.node("on", "AudioOnset").node("d", "ImpulseDecay", {{"decay_ms", 100.0}});
    g.edge("on.trigger", "d.trigger").out("o", "d.out");
    auto ev = g.evaluator();
    ev.bind_source("main", &src);
    ev.evaluate(t_of(11));
    CHECK(*ev.output("o") == 0.0);
    ev.evaluate(t_of(12));
    CHECK(*ev.output("o") == Approx(0.8));
    ev.evaluate(t_of(24)); // 100 ms later
    CHECK(*ev.output("o") == Approx(static_cast<double>(0.8f) * std::exp(-1.0)).epsilon(1e-9));
}

TEST_CASE("ADSR runs attack, decay, hold and release", "[graph][nodes]") {
    const auto src = signal_source("x", {}, "onset.full", {{0.0, 0.5f}});
    GraphBuilder g;
    g.node("on", "AudioOnset");
    g.node("env", "ADSR", {{"attack_ms", 100.0}, {"decay_ms", 100.0}, {"sustain", 0.5}, {"hold_ms", 100.0},
                           {"release_ms", 100.0}});
    g.edge("on.trigger", "env.trigger").out("o", "env.out");
    auto ev = g.evaluator();
    ev.bind_source("main", &src);
    auto at = [&](int step) {
        ev.evaluate(t_of(step));
        return *ev.output("o");
    };
    CHECK(at(0) == Approx(0.5 / 12.0));         // first attack tick, peak = velocity 0.5
    CHECK(at(6) == Approx(0.5 * 7.0 / 12.0));
    CHECK(at(11) == Approx(0.5));               // attack done after 12 ticks
    CHECK(at(17) == Approx(0.5 - 0.25 * 0.5));  // halfway through decay to 0.25
    CHECK(at(30) == Approx(0.25));              // sustaining (hold 100 ms)
    CHECK(at(36) == Approx(0.25 * (1.0 - 1.0 / 12.0)));                // releasing
    CHECK(at(60) == 0.0);
}

TEST_CASE("ADSR with a gate sustains until the gate drops", "[graph][nodes]") {
    std::vector<float> gate(60, 1.0f);
    for (std::size_t i = 40; i < gate.size(); ++i) gate[i] = 0.0f;
    const auto src = signal_source("gate", gate, "onset.full", {{0.0, 1.0f}});
    GraphBuilder g;
    g.node("on", "AudioOnset").node("gate", "AudioBand", {{"signal", std::string("gate")}});
    g.node("env", "ADSR", {{"attack_ms", 0.0}, {"decay_ms", 0.0}, {"sustain", 0.7}, {"release_ms", 50.0}});
    g.edge("on.trigger", "env.trigger").edge("gate.out", "env.gate").out("o", "env.out");
    auto ev = g.evaluator();
    ev.bind_source("main", &src);
    ev.evaluate(t_of(0));
    CHECK(*ev.output("o") == Approx(1.0));
    ev.evaluate(t_of(39));
    CHECK(*ev.output("o") == Approx(0.7));
    ev.evaluate(t_of(40));
    CHECK(*ev.output("o") < 0.7);
    ev.evaluate(t_of(46));
    CHECK(*ev.output("o") == 0.0);
}

TEST_CASE("BeatLFO shapes follow the clock", "[graph][nodes]") {
    auto lfo = [](std::map<std::string, graph::ParamValue> params, double beat) {
        GraphBuilder g;
        g.node("c", "Constant", {{"value", beat}}).node("l", "BeatLFO", std::move(params));
        g.edge("c.out", "l.clock").out("o", "l.out");
        auto ev = g.evaluator();
        ev.evaluate(0.0);
        return *ev.output("o");
    };
    CHECK(lfo({}, 0.0) == Approx(1.0)); // sine peaks on the beat
    CHECK(lfo({}, 0.5) == Approx(0.0).margin(1e-12));
    CHECK(lfo({{"shape", std::string("saw")}}, 0.25) == Approx(0.75));
    CHECK(lfo({{"shape", std::string("ramp")}, {"beats", 4.0}}, 1.0) == Approx(0.25));
    CHECK(lfo({{"shape", std::string("triangle")}}, 0.25) == Approx(0.5));
    CHECK(lfo({{"shape", std::string("square")}, {"width", 0.25}}, 0.2) == 1.0);
    CHECK(lfo({{"shape", std::string("square")}, {"width", 0.25}}, 0.3) == 0.0);
    CHECK(lfo({{"shape", std::string("saw")}, {"offset", 0.5}}, 0.0) == Approx(0.5));

    // Random holds one value per cycle, is seeded per node and repeatable.
    const auto r = [&](double beat, double seed) {
        return lfo({{"shape", std::string("random")}, {"seed", seed}}, beat);
    };
    CHECK(r(3.1, 7) == r(3.9, 7));
    CHECK(r(3.1, 7) != r(4.1, 7));
    CHECK(r(3.1, 7) != r(3.1, 8));
    CHECK(r(3.1, 7) >= 0.0);
    CHECK(r(3.1, 7) < 1.0);
}
