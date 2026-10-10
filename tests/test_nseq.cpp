#include "ztrip/serialize/nseq.hpp"
#include "ztrip/serialize/tempo.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace ztrip;
using Catch::Approx;

TEST_CASE("nseq round-trips lanes, notes and envelopes") {
    time::NoteSequence seq;
    seq.name = "song";
    seq.envelopes.push_back({"hit", time::Envelope::Kind::Adsr, 0.0, 0.25, 0.5, 0.75, {}});
    seq.envelopes.push_back({"swell", time::Envelope::Kind::Curve, 0, 0, 0, 0, {0.0, 0.0, 1.0, 1.0}});
    time::Lane kick;
    kick.name = "Kick";
    kick.swing = 0.2;
    kick.notes = {{1.0, 0.25, 0.5f, 36, "hit"}, {0.0, 0.5, 1.0f, -1, ""}};
    seq.lanes.push_back(kick);

    const auto text = serialize::write_nseq(seq);
    const auto back = serialize::parse_nseq(text);
    CHECK(back.name == "song");
    REQUIRE(back.lanes.size() == 1);
    const auto& lane = back.lanes[0];
    CHECK(lane.swing == Approx(0.2));
    REQUIRE(lane.notes.size() == 2);
    CHECK(lane.notes[0].start_beat == Approx(0.0)); // sorted on load
    CHECK(lane.notes[0].pitch == -1);
    CHECK(lane.notes[1].pitch == 36);
    CHECK(lane.notes[1].envelope_ref == "hit");
    REQUIRE(back.envelopes.size() == 2);
    CHECK(back.envelopes[0].release_beats == Approx(0.75));
    CHECK(back.envelopes[1].kind == time::Envelope::Kind::Curve);
    CHECK(back.envelopes[1].points.size() == 4);
    CHECK(serialize::write_nseq(back) == serialize::write_nseq(serialize::parse_nseq(serialize::write_nseq(back))));

    const auto path = std::filesystem::temp_directory_path() / "ztrip_test.nseq.json";
    serialize::save_nseq(path, seq);
    CHECK(serialize::load_nseq(path).lanes[0].notes.size() == 2);
    std::filesystem::remove(path);
}

TEST_CASE("nseq rejects malformed documents") {
    CHECK_THROWS_AS(serialize::parse_nseq("{"), serialize::NseqError);
    CHECK_THROWS_AS(serialize::parse_nseq(R"({"version": 99})"), serialize::NseqError);
    CHECK_THROWS_AS(serialize::parse_nseq(R"({"format": "ztrip.mgraph", "version": 1})"), serialize::NseqError);
    CHECK_THROWS_AS(serialize::parse_nseq(R"({"version": 1, "lanes": [{"name": "A", "notes": [{}]}]})"),
                    serialize::NseqError);
    CHECK_THROWS_AS(serialize::parse_nseq(R"({"version": 1, "lanes": [{"name": "A"}, {"name": "A"}]})"),
                    serialize::NseqError);
    CHECK_THROWS_AS(serialize::parse_nseq(R"({"version": 1, "envelopes": [{"name": "e", "kind": "wobble"}]})"),
                    serialize::NseqError);
}

TEST_CASE("tempo map round-trips segments, meters, offset and nudge") {
    time::TempoMap m(100.0, 0.25);
    m.set_segments({{0.0, 100.0, 140.0, time::TempoCurve::Exponential}, {16.0, 140.0, 140.0, time::TempoCurve::Step}});
    m.set_time_signatures({{0, 4, 4}, {4, 7, 8}});
    m.set_nudge(-0.012);
    const auto back = serialize::parse_tempo_map(serialize::write_tempo_map(m));
    CHECK(back.offset() == Approx(0.25));
    CHECK(back.nudge() == Approx(-0.012));
    REQUIRE(back.segments().size() == 2);
    CHECK(back.segments()[0].curve == time::TempoCurve::Exponential);
    REQUIRE(back.time_signatures().size() == 2);
    CHECK(back.time_signatures()[1].numerator == 7);
    for (double b = -2.0; b < 30.0; b += 1.3) CHECK(back.seconds_at(b) == Approx(m.seconds_at(b)));
}

TEST_CASE("tempo map rejects malformed documents") {
    CHECK_THROWS_AS(serialize::parse_tempo_map("[]"), serialize::TempoMapError);
    CHECK_THROWS_AS(serialize::parse_tempo_map(R"({"version": 2})"), serialize::TempoMapError);
    CHECK_THROWS_AS(serialize::parse_tempo_map(R"({"version": 1, "segments": [{"bpm_start": -1}]})"),
                    serialize::TempoMapError);
    CHECK_THROWS_AS(serialize::parse_tempo_map(R"({"version": 1, "segments": [{"curve": "wobble"}]})"),
                    serialize::TempoMapError);
    CHECK_THROWS_AS(serialize::parse_tempo_map(R"({"version": 1, "time_signatures": [{"denominator": 5}]})"),
                    serialize::TempoMapError);
}
