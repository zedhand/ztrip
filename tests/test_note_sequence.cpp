#include "ztrip/time/note_sequence.hpp"
#include "ztrip/time/tempo_map.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace ztrip;
using Catch::Approx;

TEST_CASE("grid divisions parse and give step sizes in beats") {
    const auto g = time::parse_grid("1/16");
    REQUIRE(g);
    CHECK(time::grid_step_beats(*g) == Approx(0.25));
    CHECK(time::grid_step_beats(*g, 8) == Approx(0.5));
    const auto t = time::parse_grid("1/8T");
    REQUIRE(t);
    CHECK(t->triplet);
    CHECK(time::grid_step_beats(*t) == Approx(1.0 / 3.0));
    CHECK(time::to_string(*t) == "1/8T");
    CHECK(time::grid_step_beats(*time::parse_grid("1/1")) == Approx(4.0));
    CHECK(time::grid_step_beats(*time::parse_grid("1/64")) == Approx(1.0 / 16.0));
    CHECK_FALSE(time::parse_grid("1/3"));
    CHECK_FALSE(time::parse_grid("1/128"));
    CHECK_FALSE(time::parse_grid("2/4"));
    CHECK_FALSE(time::parse_grid("1/"));
}

TEST_CASE("snap moves towards the grid by strength") {
    CHECK(time::snap(1.1, 0.25) == Approx(1.0));
    CHECK(time::snap(1.2, 0.25) == Approx(1.25));
    CHECK(time::snap(1.1, 0.25, 0.5) == Approx(1.05));
    CHECK(time::snap(1.1, 0.25, 0.0) == Approx(1.1));
    CHECK(time::snap(-0.1, 0.25) == Approx(0.0).margin(1e-12));
}

TEST_CASE("swing delays the second half of each pair") {
    time::Lane lane;
    lane.swing = 1.0 / 3.0;
    CHECK(lane.swung(0.0) == Approx(0.0));
    CHECK(lane.swung(0.5) == Approx(2.0 / 3.0)); // triplet feel
    CHECK(lane.swung(1.0) == Approx(1.0));
    CHECK(lane.swung(3.5) == Approx(3.0 + 2.0 / 3.0));
    CHECK(lane.swung(0.75) == Approx(2.0 / 3.0 + 0.25 * (2.0 / 3.0)));
    lane.swing = 0.0;
    CHECK(lane.swung(0.5) == Approx(0.5));
}

TEST_CASE("ADSR and curve envelopes over a note") {
    time::Note note;
    note.length_beats = 1.0;
    note.velocity = 0.5f;
    CHECK(time::envelope_level(nullptr, note, 0.5) == Approx(0.5));
    CHECK(time::envelope_level(nullptr, note, 1.0) == Approx(0.0));

    time::Envelope adsr;
    adsr.attack_beats = 0.1;
    adsr.decay_beats = 0.2;
    adsr.sustain = 0.5;
    adsr.release_beats = 0.4;
    CHECK(time::envelope_level(&adsr, note, 0.05) == Approx(0.25));
    CHECK(time::envelope_level(&adsr, note, 0.1) == Approx(0.5));
    CHECK(time::envelope_level(&adsr, note, 0.2) == Approx(0.375));
    CHECK(time::envelope_level(&adsr, note, 0.6) == Approx(0.25));
    CHECK(time::envelope_level(&adsr, note, 1.2) == Approx(0.125));
    CHECK(time::envelope_level(&adsr, note, 1.5) == Approx(0.0));

    time::Envelope curve;
    curve.kind = time::Envelope::Kind::Curve;
    curve.points = {0.0, 0.0, 0.5, 1.0, 1.0, 0.0};
    CHECK(time::envelope_level(&curve, note, 0.25) == Approx(0.25));
    CHECK(time::envelope_level(&curve, note, 0.5) == Approx(0.5));
    CHECK(time::envelope_level(&curve, note, 1.0) == Approx(0.0));
}

TEST_CASE("a lane timeline resolves notes to seconds and answers lookups") {
    time::TempoMap tempo(120.0, 1.0); // beat b at 1 + b / 2 s
    time::NoteSequence seq;
    seq.envelopes.push_back({"hit", time::Envelope::Kind::Adsr, 0.0, 0.0, 1.0, 0.5, {}});
    time::Lane lane;
    lane.name = "Kick";
    lane.notes = {{0.0, 0.5, 1.0f, 36, ""}, {2.0, 1.0, 0.4f, -1, "hit"}, {2.5, 0.25, 0.8f, -1, ""}};
    seq.lanes.push_back(lane);
    const time::LaneTimeline tl(seq, seq.lanes[0], tempo);

    REQUIRE(tl.notes().size() == 3);
    CHECK(tl.notes()[0].start_s == Approx(1.0));
    CHECK(tl.notes()[0].end_s == Approx(1.25));
    CHECK(tl.notes()[1].start_s == Approx(2.0));

    CHECK(tl.gate(1.1));
    CHECK_FALSE(tl.gate(1.3));
    CHECK(tl.gate(2.2));
    CHECK_FALSE(tl.gate(2.6));

    const auto* s = tl.started_in(1.9, 2.3);
    REQUIRE(s);
    CHECK(s->note->velocity == Approx(0.8f)); // strongest of the two starts
    CHECK_FALSE(tl.started_in(1.0, 1.9));      // (t0, t1]: the note at 1.0 is excluded
    CHECK(tl.started_in(0.99, 1.0));

    CHECK(tl.current(2.1)->note->velocity == Approx(0.4f));
    CHECK(tl.current(0.5) == nullptr);

    CHECK(tl.envelope(1.1) == Approx(1.0));
    CHECK(tl.envelope(2.1) == Approx(0.4));
    CHECK(tl.envelope(2.3) == Approx(0.8)); // both sounding: max
    // "hit" releases for 0.5 beats (0.25 s) after its end at 2.5 s.
    CHECK(tl.envelope(2.625) == Approx(0.2));
    CHECK(tl.envelope(2.8) == Approx(0.0));
}

TEST_CASE("swing applies in the lane timeline") {
    time::TempoMap tempo(60.0);
    time::NoteSequence seq;
    time::Lane lane;
    lane.name = "Hat";
    lane.swing = 1.0 / 3.0;
    lane.notes = {{0.5, 0.25, 1.0f, -1, ""}};
    seq.lanes.push_back(lane);
    const time::LaneTimeline tl(seq, seq.lanes[0], tempo);
    CHECK(tl.notes()[0].start_s == Approx(2.0 / 3.0));
}
