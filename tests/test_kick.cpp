#include "ztrip/analysis/bands.hpp"
#include "ztrip/analysis/kick.hpp"
#include "ztrip/analysis/onsets.hpp"
#include "ztrip/analysis/stft.hpp"

#include "synth.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace ztrip::analysis;

namespace {

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

// Four-on-the-floor kicks with a low synth bass on every off-beat (A1 and
// F1, both inside the Sub band), plus snare and hats.
struct BassGroove {
    std::vector<float> audio;
    std::vector<double> kicks;
};

BassGroove bass_groove(int sr) {
    constexpr double bpm = 128.0;
    constexpr double spb = 60.0 / bpm;
    constexpr int beats = 32;
    BassGroove g;
    g.audio.assign(static_cast<std::size_t>((beats * spb + 1.0) * sr), 0.0f);
    ztrip::test::Noise noise(99);
    for (int b = 0; b < beats; ++b) {
        const double t = b * spb;
        ztrip::test::add_kick(g.audio, sr, t);
        g.kicks.push_back(t);
        if (b % 2 == 1) ztrip::test::add_snare(g.audio, sr, t, noise);
        ztrip::test::add_hat(g.audio, sr, t + spb / 2, noise);
        ztrip::test::add_bass(g.audio, sr, t + spb / 2, (b / 8) % 2 ? 43.65 : 55.0, spb / 2 * 0.9);
    }
    return g;
}

} // namespace

TEST_CASE("kicks are found within 5 ms and bass notes are not", "[kick]") {
    for (const int sr : {44100, 48000}) {
        const BassGroove g = bass_groove(sr);
        const Match m = match(detect_kicks(g.audio, sr, {}), g.kicks, 0.020);
        INFO(sr << " Hz: R=" << m.recall << " P=" << m.precision << " max err=" << m.max_error);
        CHECK(m.recall == 1.0);
        CHECK(m.precision == 1.0);
        CHECK(m.max_error < 0.005);
    }
}

TEST_CASE("the kick detector beats the Sub band on bass-heavy material", "[kick]") {
    // The reason the detector exists: Sub-band flux at FFT 2048 also fires on
    // the off-beat bass notes.
    constexpr int sr = 48000;
    const BassGroove g = bass_groove(sr);
    const Spectrogram spec = compute_stft(g.audio, sr, {});
    const auto sub = pick_onsets(band_flux(spec, default_bands().front()), spec.frame_rate(), {});
    const Match sub_m = match(sub, g.kicks, 0.020);
    const Match kick_m = match(detect_kicks(g.audio, sr, {}), g.kicks, 0.020);
    INFO("Sub P=" << sub_m.precision << " kick P=" << kick_m.precision);
    CHECK(kick_m.precision > sub_m.precision);
}

TEST_CASE("a kick at the very start is found, silence finds nothing", "[kick]") {
    constexpr int sr = 48000;
    std::vector<float> x(sr * 2, 0.0f);
    CHECK(detect_kicks(x, sr, {}).empty());
    ztrip::test::add_kick(x, sr, 0.0);
    ztrip::test::add_kick(x, sr, 1.0, 0.6f);
    const auto k = detect_kicks(x, sr, {});
    REQUIRE(k.size() == 2);
    CHECK(k[0].time < 0.005);
    CHECK(std::fabs(k[1].time - 1.0) < 0.005);
    CHECK(k[0].velocity > k[1].velocity);
    CHECK(k[1].velocity > 0.0f);
}

TEST_CASE("quiet low rumbles under the gate are ignored", "[kick]") {
    constexpr int sr = 48000;
    std::vector<float> x(sr * 3, 0.0f);
    ztrip::test::add_kick(x, sr, 0.5, 0.9f);
    ztrip::test::add_kick(x, sr, 1.5, 0.9f);
    ztrip::test::add_kick(x, sr, 2.5, 0.3f); // -9.5 dB, below the 5 dB gate
    const auto k = detect_kicks(x, sr, {});
    CHECK(k.size() == 2);
}

TEST_CASE("a kick over a sustained, sidechained sub is found and the swell is not", "[kick]") {
    // EDM: the sub bass ducks under each kick and swells back. In dB the kick
    // barely rises over the full-level sub; in amplitude it jumps clearly,
    // while the swell rises slowly.
    constexpr int sr = 48000;
    constexpr double spb = 60.0 / 128.0;
    constexpr int beats = 32;
    std::vector<float> x(static_cast<std::size_t>((beats * spb + 1.0) * sr), 0.0f);
    std::vector<double> kicks;
    for (int b = 0; b < beats; ++b) kicks.push_back(b * spb);
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double t = static_cast<double>(i) / sr;
        const double since = t - std::floor(t / spb) * spb;
        const double duck = 1.0 - 0.9 * std::exp(-(since / 0.13) * (since / 0.13));
        x[i] = static_cast<float>(0.3 * duck * std::sin(2.0 * std::numbers::pi * 55.0 * t));
    }
    for (double t : kicks) ztrip::test::add_kick(x, sr, t);
    const Match m = match(detect_kicks(x, sr, {}), kicks, 0.020);
    INFO("R=" << m.recall << " P=" << m.precision << " max err=" << m.max_error);
    CHECK(m.recall == 1.0);
    CHECK(m.precision == 1.0);
    CHECK(m.max_error < 0.008);
}

TEST_CASE("a fast double kick keeps every hit", "[kick]") {
    // Metal: 16th-note double kick at 190 BPM, 79 ms apart, with a tight
    // kick that has mostly died away before the next hit.
    constexpr int sr = 44100;
    constexpr double step = 60.0 / 190.0 / 4.0;
    std::vector<float> x(static_cast<std::size_t>(5.0 * sr), 0.0f);
    std::vector<double> kicks;
    for (int i = 0; i < 48; ++i) {
        const double t0 = 0.2 + i * step;
        kicks.push_back(t0);
        const auto start = static_cast<std::size_t>(std::lround(t0 * sr));
        double phase = 0.0;
        for (std::size_t j = 0; j < static_cast<std::size_t>(0.2 * sr) && start + j < x.size(); ++j) {
            const double s = static_cast<double>(j) / sr;
            phase += 2.0 * std::numbers::pi * (55.0 + 140.0 * std::exp(-s / 0.015)) / sr;
            x[start + j] += 0.8f * static_cast<float>(std::sin(phase) * std::exp(-s / 0.03));
        }
    }
    const Match m = match(detect_kicks(x, sr, {}), kicks, 0.020);
    INFO("R=" << m.recall << " P=" << m.precision << " max err=" << m.max_error);
    CHECK(m.recall == 1.0);
    CHECK(m.precision == 1.0);
}
