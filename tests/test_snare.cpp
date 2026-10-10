#include "ztrip/analysis/snare.hpp"

#include "synth.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace ztrip::analysis;

TEST_CASE("snares are found on the backbeat, kicks and hats are not snares", "[snare]") {
    for (const int sr : {44100, 48000}) {
        constexpr double bpm = 120.0;
        constexpr double spb = 60.0 / bpm;
        constexpr int bars = 8;
        // Kick on every beat, snare on 2 and 4, hats on eighths.
        const auto x = ztrip::test::groove(sr, bpm, bars);
        std::vector<double> snares;
        for (int b = 1; b < bars * 4; b += 2) snares.push_back(b * spb);

        const auto found = detect_snares(x, sr, {});
        std::size_t hits = 0;
        double max_err = 0.0;
        for (double t : snares) {
            double best = 1.0;
            for (const Trigger& s : found) best = std::min(best, std::fabs(s.time - t));
            if (best <= 0.020) {
                ++hits;
                max_err = std::max(max_err, best);
            }
        }
        INFO(sr << " Hz: " << found.size() << " found, " << hits << "/" << snares.size() << " true, max err " << max_err);
        CHECK(hits == snares.size());
        CHECK(found.size() == snares.size());
        CHECK(max_err < 0.010);
    }
}

TEST_CASE("silence and a lone kick find no snares", "[snare]") {
    constexpr int sr = 48000;
    std::vector<float> x(sr * 2, 0.0f);
    CHECK(detect_snares(x, sr, {}).empty());
    ztrip::test::add_kick(x, sr, 0.5);
    ztrip::test::add_kick(x, sr, 1.2);
    CHECK(detect_snares(x, sr, {}).empty());
}
