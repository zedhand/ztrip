#include "ztrip/analysis/bands.hpp"
#include "ztrip/analysis/onsets.hpp"
#include "ztrip/analysis/stft.hpp"

#include "synth.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace ztrip::analysis;

namespace {

// Fraction of reference times matched one-to-one by a detection within tol.
struct Match {
    double recall = 0.0;
    double precision = 0.0;
    double max_error = 0.0;
};

Match match(const std::vector<Trigger>& det, const std::vector<double>& ref, double tol) {
    std::vector<bool> used(det.size(), false);
    std::size_t hits = 0;
    Match m;
    for (double t : ref) {
        std::size_t best = det.size();
        double best_err = tol;
        for (std::size_t i = 0; i < det.size(); ++i) {
            const double e = std::fabs(det[i].time - t);
            if (!used[i] && e <= best_err) {
                best_err = e;
                best = i;
            }
        }
        if (best < det.size()) {
            used[best] = true;
            ++hits;
            m.max_error = std::max(m.max_error, best_err);
        }
    }
    m.recall = ref.empty() ? 1.0 : static_cast<double>(hits) / static_cast<double>(ref.size());
    m.precision = det.empty() ? 0.0 : static_cast<double>(hits) / static_cast<double>(det.size());
    return m;
}

} // namespace

TEST_CASE("kick and snare onsets land on their own bands within 15 ms", "[onsets]") {
    const int sr = 48000;
    const double bpm = 110.0;
    const auto x = ztrip::test::groove(sr, bpm, 4, 0.3, true, false);
    const auto spec = compute_stft(x, sr, {});

    std::vector<double> kicks, snares;
    for (int b = 0; b < 16; ++b) {
        kicks.push_back(0.3 + b * 60.0 / bpm);
        if (b % 2 == 1) snares.push_back(kicks.back());
    }

    const auto sub = pick_onsets(band_flux(spec, {"Sub", 20.0f, 60.0f}), spec.frame_rate(), {});
    const Match k = match(sub, kicks, 0.015);
    CHECK(k.recall == 1.0);
    CHECK(k.precision == 1.0);

    const auto high = pick_onsets(band_flux(spec, {"HighMid", 2000.0f, 6000.0f}), spec.frame_rate(), {});
    const Match s = match(high, snares, 0.015);
    CHECK(s.recall == 1.0);
    // The kick's attack also reaches 2-6 kHz, but far weaker than a snare.
    float weakest_snare = 1.0f;
    float loudest_other = 0.0f;
    for (const auto& on : high) {
        const bool is_snare = std::any_of(snares.begin(), snares.end(),
                                          [&](double t) { return std::fabs(on.time - t) < 0.015; });
        if (is_snare) weakest_snare = std::min(weakest_snare, on.velocity);
        else loudest_other = std::max(loudest_other, on.velocity);
    }
    CHECK(loudest_other < 0.5f * weakest_snare);
}

TEST_CASE("minimum gap merges hits closer than the gap", "[onsets]") {
    const int sr = 48000;
    std::vector<float> x(sr * 2, 0.0f);
    ztrip::test::Noise noise(7);
    for (double t : {0.5, 0.53, 1.2}) ztrip::test::add_snare(x, sr, t, noise);
    const auto spec = compute_stft(x, sr, {});
    const auto flux = band_flux(spec, {"HighMid", 2000.0f, 6000.0f});

    OnsetSettings wide;
    wide.min_gap_s = 0.1f;
    CHECK(pick_onsets(flux, spec.frame_rate(), wide).size() == 2);
    OnsetSettings narrow;
    narrow.min_gap_s = 0.02f;
    CHECK(pick_onsets(flux, spec.frame_rate(), narrow).size() == 3);
}

TEST_CASE("silence and steady tones produce no onsets after the start", "[onsets]") {
    const int sr = 48000;
    std::vector<float> silent(sr, 0.0f);
    const auto s1 = compute_stft(silent, sr, {});
    CHECK(pick_onsets(band_flux(s1, {"full", 20.0f, 16000.0f}), s1.frame_rate(), {}).empty());

    std::vector<float> tone(sr * 2);
    for (std::size_t i = 0; i < tone.size(); ++i) tone[i] = 0.5f * static_cast<float>(std::sin(0.05 * static_cast<double>(i)));
    const auto s2 = compute_stft(tone, sr, {});
    const auto on = pick_onsets(band_flux(s2, {"full", 20.0f, 16000.0f}), s2.frame_rate(), {});
    REQUIRE(on.size() <= 1);
    if (!on.empty()) CHECK(on[0].time < 0.05);
}
