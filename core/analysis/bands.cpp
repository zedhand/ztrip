#include "ztrip/analysis/bands.hpp"

#include <algorithm>
#include <cmath>

namespace ztrip::analysis {
namespace {

BinRange bins_for(const Spectrogram& spec, const Band& band) {
    return band_bins(spec.sample_rate, spec.fft_size, band);
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

BinRange band_bins(int sample_rate, int fft_size, const Band& band) {
    const std::size_t bins = static_cast<std::size_t>(fft_size) / 2 + 1;
    const double per_bin = static_cast<double>(sample_rate) / fft_size;
    auto to_bin = [&](float hz) {
        const double b = std::ceil(hz / per_bin);
        return static_cast<std::size_t>(std::clamp(b, 0.0, static_cast<double>(bins)));
    };
    BinRange r{to_bin(band.low_hz), to_bin(band.high_hz)};
    // Narrow low bands at small FFT sizes can fall between bins; keep one.
    if (r.last <= r.first) r.last = std::min(r.first + 1, bins);
    if (r.first >= bins) r.first = r.last = bins;
    return r;
}

float frame_energy_db(std::span<const float> frame, BinRange r) {
    double power = 0.0;
    for (std::size_t k = r.first; k < r.last; ++k) {
        power += static_cast<double>(frame[k]) * frame[k];
    }
    return static_cast<float>(10.0 * std::log10(power + 1e-12));
}

float frame_centroid_hz(std::span<const float> frame, int sample_rate, int fft_size) {
    double num = 0.0;
    double den = 0.0;
    for (std::size_t k = 1; k < frame.size(); ++k) {
        num += static_cast<double>(k) * sample_rate / fft_size * frame[k];
        den += frame[k];
    }
    return den > 1e-9 ? static_cast<float>(num / den) : 0.0f;
}

// Compares against the previous frame max-filtered over +-1 bin (SuperFlux,
// Boeck & Widmer 2013), which suppresses leakage and vibrato false onsets.
BandFlux::BandFlux(BinRange bins, std::size_t total_bins)
    : r_(bins), lo_(bins.first > 0 ? bins.first - 1 : 0), hi_(std::min(total_bins, bins.last + 1)) {
    if (hi_ < lo_) hi_ = lo_;
    cur_.assign(hi_ - lo_, 0.0f);
    prev_.assign(hi_ - lo_, 0.0f);
}

void BandFlux::reset() {
    std::fill(prev_.begin(), prev_.end(), 0.0f);
}

float BandFlux::next(std::span<const float> frame) {
    if (r_.first >= r_.last) return 0.0f;
    for (std::size_t k = lo_; k < hi_; ++k) cur_[k - lo_] = std::log1p(kFluxGamma * frame[k]);
    float sum = 0.0f;
    for (std::size_t k = r_.first; k < r_.last; ++k) {
        const std::size_t i = k - lo_;
        float ref = prev_[i];
        if (i > 0) ref = std::max(ref, prev_[i - 1]);
        if (i + 1 < prev_.size()) ref = std::max(ref, prev_[i + 1]);
        const float d = cur_[i] - ref;
        if (d > 0.0f) sum += d;
    }
    std::swap(cur_, prev_);
    return sum / static_cast<float>(r_.last - r_.first);
}

std::vector<float> band_energy_db(const Spectrogram& spec, const Band& band) {
    const BinRange r = bins_for(spec, band);
    std::vector<float> out(spec.frames);
    for (std::size_t f = 0; f < spec.frames; ++f) out[f] = frame_energy_db(spec.frame(f), r);
    return out;
}

std::vector<float> band_flux(const Spectrogram& spec, const Band& band) {
    BandFlux flux(bins_for(spec, band), spec.bins);
    std::vector<float> out(spec.frames, 0.0f);
    for (std::size_t f = 0; f < spec.frames; ++f) out[f] = flux.next(spec.frame(f));
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
        out[f] = frame_centroid_hz(spec.frame(f), spec.sample_rate, spec.fft_size);
    }
    return out;
}

} // namespace ztrip::analysis
