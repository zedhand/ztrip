// The graph's musical-time sources: NoteLane and BeatClock on a tempo map.
#include "graph_fixtures.hpp"
#include "ztrip/time/note_sequence.hpp"
#include "ztrip/time/tempo_map.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace ztrip;
using Catch::Approx;

namespace {

time::NoteSequence kick_sequence() {
    time::NoteSequence seq;
    seq.envelopes.push_back({"hit", time::Envelope::Kind::Adsr, 0.0, 0.5, 0.0, 0.0, {}});
    time::Lane lane;
    lane.name = "Kick";
    for (int b = 0; b < 64; ++b) lane.notes.push_back({static_cast<double>(b), 0.25, b % 4 == 0 ? 1.0f : 0.5f, -1, "hit"});
    seq.lanes.push_back(lane);
    return seq;
}

} // namespace

TEST_CASE("NoteLane fires on notes through the bound tempo map") {
    const auto seq = kick_sequence();
    time::TempoMap tempo(120.0, 1.0);
    auto ev = test::GraphBuilder()
                  .node("lane", "NoteLane", {{"lane", std::string("Kick")}})
                  .out("gate", "lane.gate")
                  .out("env", "lane.envelope")
                  .out("vel", "lane.velocity")
                  .out("trig", "lane.trigger")
                  .evaluator();
    ev.bind_tempo("main", &tempo);
    ev.bind_sequence("main", &seq);

    ev.evaluate(1.0); // first note starts exactly here
    CHECK(*ev.output("trig") == Approx(1.0));
    CHECK(*ev.output("gate") == Approx(1.0));
    ev.evaluate(1.0 + 1.0 / 120.0);
    CHECK(*ev.output("trig") == Approx(0.0));
    CHECK(*ev.output("env") < 1.0);
    ev.evaluate(1.2);
    CHECK(*ev.output("gate") == Approx(0.0)); // a 1/16 note at 120 BPM lasts 0.125 s
    ev.evaluate(1.5);
    CHECK(*ev.output("trig") == Approx(0.5));
    CHECK(*ev.output("vel") == Approx(0.5));

    // Nudging the tempo map and rebinding moves the notes.
    tempo.nudge_by(0.25);
    ev.bind_tempo("main", &tempo);
    ev.evaluate(1.5);
    CHECK(*ev.output("gate") == Approx(0.0));
    ev.evaluate(1.25);
    CHECK(*ev.output("trig") == Approx(1.0));
}

TEST_CASE("NoteLane without a tempo map follows the analysed beats") {
    const auto song = test::synthetic_song(20.0);
    const auto seq = kick_sequence();
    auto ev = test::GraphBuilder().node("lane", "NoteLane").out("trig", "lane.trigger").evaluator();
    ev.bind_source("main", &song);
    ev.bind_sequence("main", &seq);
    // Beat 0 is the first downbeat, the grid's beat at downbeat_phase.
    const double t = song.beats.beats[static_cast<std::size_t>(song.beats.downbeat_phase)];
    const auto tick = std::ceil(t * 120.0 - 1e-9) / 120.0;
    ev.evaluate(tick);
    CHECK(*ev.output("trig") == Approx(1.0));
}

TEST_CASE("NoteLane with nothing bound stays silent") {
    auto ev = test::GraphBuilder().node("lane", "NoteLane").out("gate", "lane.gate").evaluator();
    for (double t = 0.0; t < 3.0; t += 0.1) {
        ev.evaluate(t);
        CHECK(*ev.output("gate") == 0.0);
    }
}

TEST_CASE("BeatClock reads a bound tempo map and its meter") {
    time::TempoMap tempo(60.0, 2.0);
    tempo.set_time_signatures({{0, 3, 4}});
    auto ev = test::GraphBuilder()
                  .node("clock", "BeatClock")
                  .out("pos", "clock.clock")
                  .out("bar", "clock.bar")
                  .out("bar_phase", "clock.bar_phase")
                  .evaluator();
    ev.bind_tempo("main", &tempo);
    ev.evaluate(3.5);
    CHECK(*ev.output("pos") == Approx(1.5));
    CHECK(*ev.output("bar_phase") == Approx(0.5));
    ev.evaluate(5.0);
    CHECK(*ev.output("bar") == Approx(1.0)); // bar 1 starts at beat 3
}

TEST_CASE("NoteLane graphs are seek-deterministic") {
    const auto seq = kick_sequence();
    time::TempoMap tempo;
    tempo.set_segments({{0.0, 100.0, 140.0, time::TempoCurve::Linear}, {32.0, 140.0, 140.0, time::TempoCurve::Step}});
    tempo.set_offset(0.3);
    auto builder = test::GraphBuilder()
                       .node("lane", "NoteLane")
                       .node("adsr", "ADSR")
                       .edge("lane.trigger", "adsr.trigger")
                       .edge("lane.gate", "adsr.gate")
                       .out("a", "adsr.out")
                       .out("e", "lane.envelope");
    auto play = builder.evaluator();
    auto seek = builder.evaluator();
    for (auto* ev : {&play, &seek}) {
        ev->bind_tempo("main", &tempo);
        ev->bind_sequence("main", &seq);
    }
    std::vector<std::vector<double>> ref;
    for (int i = 0; i <= 120 * 20; ++i) {
        play.evaluate(i / 120.0);
        ref.push_back({*play.output("a"), *play.output("e")});
    }
    for (int i : {2399, 17, 1200, 600, 601, 2000, 5, 1999}) {
        seek.evaluate(i / 120.0);
        CHECK(*seek.output("a") == Approx(ref[static_cast<std::size_t>(i)][0]).margin(1e-5));
        CHECK(*seek.output("e") == Approx(ref[static_cast<std::size_t>(i)][1]).margin(1e-5));
    }
}
