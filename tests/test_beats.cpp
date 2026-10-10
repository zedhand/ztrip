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

TEST_CASE("drum_fit scores beats on the hits, not between them", "[beats]") {
    std::vector<double> beats;
    std::vector<Trigger> on, between;
    for (int i = 0; i < 16; ++i) {
        beats.push_back(0.5 * i);
        on.push_back({0.5 * i + 0.01, 1.0f});
        between.push_back({0.5 * i + 0.25, 1.0f});
    }
    CHECK(drum_fit(beats, on).f == Catch::Approx(1.0));
    CHECK(drum_fit(beats, between).f == 0.0);
    CHECK(drum_fit(beats, {}).f == 0.0);
}

TEST_CASE("a fast rock groove is read at the drummer's tempo, not half of it", "[beats]") {
    // 200 BPM: kick on 1 and 3, snare on 2 and 4, hats on every beat. The
    // onset autocorrelation and the 120 BPM prior favour 100 BPM (kick and
    // snare as one pulse); the drum hits fit 200 better.
    const int sr = 44100;
    const double bpm = 200.0;
    const double spb = 60.0 / bpm;
    const int beats = 160;
    std::vector<float> x(static_cast<std::size_t>((beats * spb + 1.0) * sr), 0.0f);
    ztrip::test::Noise noise(7);
    for (int b = 0; b < beats; ++b) {
        const double t = 0.3 + b * spb;
        if (b % 2 == 0) ztrip::test::add_kick(x, sr, t, 0.8f);
        else ztrip::test::add_snare(x, sr, t, noise, 0.6f);
        ztrip::test::add_hat(x, sr, t, noise);
    }
    const auto grid = analyze(x, sr, {}).beats;
    INFO("-> " << grid.bpm << " BPM");
    CHECK(grid.bpm == Catch::Approx(bpm).epsilon(0.01));
}
