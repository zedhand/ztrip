#include "ztrip/analysis/offline.hpp"

#include "synth.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>

using namespace ztrip::analysis;

TEST_CASE("beat grid locks to a synthetic groove", "[beats]") {
    const double bpm = GENERATE(96.0, 120.0, 128.0, 140.0);
    const int sr = 44100;
    const double first = 0.25;
    const int bars = 12;
    const auto x = ztrip::test::groove(sr, bpm, bars, first);
    const auto result = analyze(x, sr, {});
    const auto& grid = result.beats;
    INFO("bpm " << bpm << " -> " << grid.bpm);

    CHECK(grid.bpm == Catch::Approx(bpm).epsilon(0.01));
    const double spb = 60.0 / bpm;
    REQUIRE(grid.beats.size() >= static_cast<std::size_t>(bars * 4 - 2));
    for (double t : grid.beats) {
        const double k = std::round((t - first) / spb);
        CHECK(std::fabs(t - (first + k * spb)) < 0.03);
    }
}

TEST_CASE("downbeats fall on the accented, non-backbeat beat", "[beats]") {
    const int sr = 44100;
    const double bpm = 120.0;
    const double first = 0.5; // the pattern's first beat is a downbeat
    const auto x = ztrip::test::groove(sr, bpm, 12, first);
    const auto grid = analyze(x, sr, {}).beats;
    REQUIRE(grid.downbeats.size() >= 10);
    const double bar = 4 * 60.0 / bpm;
    for (double t : grid.downbeats) {
        const double k = std::round((t - first) / bar);
        CHECK(std::fabs(t - (first + k * bar)) < 0.03);
    }
}

TEST_CASE("tempo estimate is 0 for signals too short to judge", "[beats]") {
    const std::vector<float> o(50, 1.0f);
    CHECK(estimate_tempo(o, 100.0, {}) == 0.0);
    CHECK(track_beats(o, 100.0, 0.0, {}).empty());
}
