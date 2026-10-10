#include "ztrip/analysis/bands.hpp"
#include "ztrip/analysis/stft.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

using namespace ztrip::analysis;

namespace {

std::vector<float> sine(double hz, int sr, double seconds, float amp = 1.0f) {
    std::vector<float> x(static_cast<std::size_t>(seconds * sr));
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = amp * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / sr));
    }
    return x;
}

} // namespace

TEST_CASE("a sine peaks at its bin with unit magnitude for every window", "[stft]") {
    const int sr = 48000;
    for (Window w : {Window::Hann, Window::Hamming, Window::BlackmanHarris}) {
        for (int n : {512, 2048, 8192}) {
            const double hz = 40.0 * sr / n; // exactly bin 40
            const auto spec = compute_stft(sine(hz, sr, 1.0, 0.5f), sr, {n, 0, w});
            REQUIRE(spec.bins == static_cast<std::size_t>(n / 2 + 1));
            REQUIRE(spec.hop == n / 4);
            const auto frame = spec.frame(spec.frames / 2);
            const auto peak = std::max_element(frame.begin(), frame.end()) - frame.begin();
            CHECK(peak == 40);
            CHECK(frame[40] == Catch::Approx(0.5f).epsilon(0.01));
        }
    }
}

TEST_CASE("frame times are centred on hop multiples", "[stft]") {
    const auto spec = compute_stft(std::vector<float>(48000, 0.0f), 48000, {});
    CHECK(spec.frames == 48000 / 512 + 1);
    CHECK(spec.frame_time(0) == 0.0);
    CHECK(spec.frame_time(10) == Catch::Approx(10 * 512 / 48000.0));
    CHECK(spec.frame_rate() == Catch::Approx(93.75));
}

TEST_CASE("invalid STFT settings are rejected", "[stft]") {
    const std::vector<float> x(4096, 0.0f);
    CHECK_THROWS_AS(compute_stft(x, 48000, {1000}), std::invalid_argument);
    CHECK_THROWS_AS(compute_stft(x, 48000, {256}), std::invalid_argument);
    CHECK_THROWS_AS(compute_stft(x, 48000, {16384}), std::invalid_argument);
    CHECK_THROWS_AS(compute_stft({}, 48000, {}), std::invalid_argument);
}

TEST_CASE("band energy follows the band that holds the tone", "[bands]") {
    const int sr = 48000;
    const auto spec = compute_stft(sine(1000.0, sr, 1.0), sr, {});
    const auto bands = default_bands();
    REQUIRE(bands.size() == 6);
    const auto mid = band_energy_db(spec, bands[3]);  // 500-2k
    const auto bass = band_energy_db(spec, bands[1]); // 60-250
    const std::size_t f = spec.frames / 2;
    CHECK(mid[f] > -10.0f);
    CHECK(bass[f] < mid[f] - 60.0f);
}

TEST_CASE("percentile normalisation maps to 0..1 with a dB floor", "[bands]") {
    std::vector<float> v;
    for (int i = 0; i <= 100; ++i) v.push_back(static_cast<float>(i));
    CHECK(percentile(v, 50.0f) == Catch::Approx(50.0f));
    const auto n = normalize_percentile(v, 0.0f, 100.0f);
    CHECK(n.front() == 0.0f);
    CHECK(n[50] == Catch::Approx(0.5f));
    CHECK(n.back() == 1.0f);
    const auto floored = normalize_percentile(v, 0.0f, 100.0f, 20.0f); // range 80..100
    CHECK(floored[70] == 0.0f);
    CHECK(floored[90] == Catch::Approx(0.5f));
}
