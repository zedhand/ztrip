#include "synth.hpp"
#include "ztrip/analysis/offline.hpp"
#include "ztrip/time/autodraft.hpp"
#include "ztrip/time/tempo_map.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace ztrip;
using Catch::Approx;

TEST_CASE("draft_lane quantizes, merges same-step hits and keeps velocity") {
    time::TempoMap tempo(120.0, 0.0); // 0.5 s per beat, 1/16 = 0.125 s
    const std::vector<analysis::Trigger> events = {
        {0.010, 0.9f},  // -> beat 0
        {0.020, 0.95f}, // same step, stronger: replaces
        {0.495, 0.5f},  // -> beat 1
        {0.810, 0.7f},  // 0.81 s: nearest 16th 0.75 s is 60 ms away, kept as heard
    };
    time::DraftSettings settings;
    const auto lane = time::draft_lane(events, tempo, settings, "Kick");
    REQUIRE(lane.notes.size() == 3);
    CHECK(lane.name == "Kick");
    CHECK(lane.notes[0].start_beat == Approx(0.0).margin(1e-12));
    CHECK(lane.notes[0].velocity == Approx(0.95f));
    CHECK(lane.notes[1].start_beat == Approx(1.0));
    CHECK(lane.notes[2].start_beat == Approx(1.62));
    CHECK(lane.notes[0].length_beats == Approx(0.25));

    settings.quantize = 0.0;
    const auto raw = time::draft_lane(events, tempo, settings, "Kick");
    CHECK(raw.notes[1].start_beat == Approx(0.99));
}

TEST_CASE("auto_draft fills lanes from named event lists") {
    analysis::AnalysisResult r;
    r.events.push_back({"onset.Sub", {{1.0, 1.0f}, {1.5, 0.2f}}});
    time::TempoMap tempo(120.0, 0.0);
    time::DraftSettings settings;
    settings.lanes = {{"Kick", "onset.Sub", 0.5f}, {"Missing", "onset.Nope", 0.0f}};
    const auto seq = time::auto_draft(r, tempo, settings);
    REQUIRE(seq.lanes.size() == 2);
    REQUIRE(seq.lanes[0].notes.size() == 1); // the weak hit is dropped
    CHECK(seq.lanes[0].notes[0].start_beat == Approx(2.0));
    CHECK(seq.lanes[1].notes.empty());
}

TEST_CASE("an auto-drafted kick lane lands on synthetic kicks") {
    const int sr = 44100;
    const double bpm = 124.0, first = 0.4;
    const auto audio = test::groove(sr, bpm, 16, first);
    const auto r = analysis::analyze(audio, sr, {});
    const auto tempo = time::tempo_map_from_beats(r.beats);
    const auto seq = time::auto_draft(r, tempo);
    const auto* kick = seq.find_lane("Kick");
    REQUIRE(kick);

    const double spb = 60.0 / bpm;
    std::size_t hits = 0;
    for (const auto& n : kick->notes) {
        const double t = tempo.seconds_at(n.start_beat);
        const double k = std::round((t - first) / spb);
        if (std::abs(t - (first + k * spb)) <= 0.020) ++hits;
    }
    INFO(kick->notes.size() << " notes, " << hits << " on a kick");
    CHECK(static_cast<double>(hits) >= 0.95 * 64.0);
    CHECK(static_cast<double>(hits) >= 0.9 * static_cast<double>(kick->notes.size()));
}
