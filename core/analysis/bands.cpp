#include "ztrip/analysis/bands.hpp"

#include <algorithm>
#include <cmath>

namespace ztrip::analysis {
namespace {

struct BinRange {
    std::size_t first = 0;
    std::size_t last = 0; // exclusive
};

BinRange bins_for(const Spectrogram& spec, const Band& band) {
    const double per_bin = static_cast<double>(spec.sample_rate) / spec.fft_size;
    auto to_bin = [&](float hz) {
        const double b = std::ceil(hz / per_bin);
        return static_cast<std::size_t>(std::clamp(b, 0.0, static_cast<double>(spec.bins)));
    };
    BinRange r{to_bin(band.low_hz), to_bin(band.high_hz)};
    // Narrow low bands at small FFT sizes can fall between bins; keep one.
    if (r.last <= r.first) r.last = std::min(r.first + 1, spec.bins);
    if (r.first >= spec.bins) r.first = r.last = spec.bins;
    return r;
}

// Log compression for flux; gamma sets how strongly quiet partials count.
constexpr float kFluxGamma = 100.0f;

} // namespace

std::vector<Band> default_bands() {
    return {
        {"Sub", 20.0f, 60.0f},          {"Bass", 60.0f, 250.0f},
        {"LowMid", 250.0f, 500.0f},     {"Mid", 500.0f, 2000.0f},
        {"HighMid", 2000.0f, 6000.0f},  {"Air", 6000.0f, 20000.0f},
    };
}

std::vector<float> band_energy_db(const Spectrogram& spec, const Band& band) {
    const BinRange r = bins_for(spec, band);
    std::vector<float> out(spec.frames);
    for (std::size_t f = 0; f < spec.frames; ++f) {
        const auto frame = spec.frame(f);
        double power = 0.0;
        for (std::size_t k = r.first; k < r.last; ++k) {
            power += static_cast<double>(frame[k]) * frame[k];
        }
        out[f] = static_cast<float>(10.0 * std::log10(power + 1e-12));
    }
    return out;
}

std::vector<float> band_flux(const Spectrogram& spec, const Band& band) {
    const BinRange r = bins_for(spec, band);
    std::vector<float> out(spec.frames, 0.0f);
    if (r.first >= r.last) return out;
    // Compare against the previous frame max-filtered over +-1 bin (SuperFlux,
    // Boeck & Widmer 2013), which suppresses leakage and vibrato false onsets.
    const std::size_t lo = r.first > 0 ? r.first - 1 : 0;
    const std::size_t hi = std::min(spec.bins, r.last + 1);
    std::vector<float> cur(hi - lo, 0.0f);
    std::vector<float> prev(hi - lo, 0.0f);
    for (std::size_t f = 0; f < spec.frames; ++f) {
        const auto frame = spec.frame(f);
        for (std::size_t k = lo; k < hi; ++k) cur[k - lo] = std::log1p(kFluxGamma * frame[k]);
        float sum = 0.0f;
        for (std::size_t k = r.first; k < r.last; ++k) {
            const std::size_t i = k - lo;
            float ref = prev[i];
            if (i > 0) ref = std::max(ref, prev[i - 1]);
            if (i + 1 < prev.size()) ref = std::max(ref, prev[i + 1]);
            const float d = cur[i] - ref;
            if (d > 0.0f) sum += d;
        }
        out[f] = sum / static_cast<float>(r.last - r.first);
        std::swap(cur, prev);
    }
    return out;
}

float percentile(std::span<const float> values, float p) {
    if (values.empty()) return 0.0f;
    std::vector<float> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    const double pos = std::clamp(p, 0.0f, 100.0f) / 100.0 * static_cast<double>(sorted.size() - 1);
    const auto i = static_cast<std::size_t>(pos);
    const std::size_t j = std::min(i + 1, sorted.size() - 1);
    const double frac = pos - static_cast<double>(i);
    return static_cast<float>(sorted[i] + (sorted[j] - sorted[i]) * frac);
}

std::vector<float> normalize_percentile(std::span<const float> values, float lo_pct, float hi_pct,
                                        float floor_db) {
    const float hi = percentile(values, hi_pct);
    float lo = percentile(values, lo_pct);
    if (floor_db > 0.0f) lo = std::max(lo, hi - floor_db);
    const float range = hi - lo;
    std::vector<float> out(values.size(), 0.0f);
    if (range <= 0.0f) return out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        out[i] = std::clamp((values[i] - lo) / range, 0.0f, 1.0f);
    }
    return out;
}

std::vector<float> spectral_centroid(const Spectrogram& spec) {
    std::vector<float> out(spec.frames, 0.0f);
    for (std::size_t f = 0; f < spec.frames; ++f) {
        const auto frame = spec.frame(f);
        double num = 0.0;
        double den = 0.0;
        for (std::size_t k = 1; k < spec.bins; ++k) {
            num += spec.bin_hz(k) * frame[k];
            den += frame[k];
        }
        out[f] = den > 1e-9 ? static_cast<float>(num / den) : 0.0f;
    }
    return out;
}

} // namespace ztrip::analysis
