#pragma once
// Shared pieces of the kick and snare detectors: band-limited amplitude
// envelopes and the "jump against the loudest level nearby" onset measure.
// Private to ztrip_core.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <numbers>
#include <span>
#include <vector>

namespace ztrip::analysis::detail {

// RBJ cookbook biquad, run in double for repeatable output.
class Biquad {
public:
    static Biquad lowpass(double cutoff_hz, double q, int sample_rate) { return {cutoff_hz, q, sample_rate, false}; }
    static Biquad highpass(double cutoff_hz, double q, int sample_rate) { return {cutoff_hz, q, sample_rate, true}; }

    double operator()(double x) {
        const double y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_;
        x1_ = x;
        y2_ = y1_;
        y1_ = y;
        return y;
    }

private:
    Biquad(double cutoff_hz, double q, int sample_rate, bool high) {
        const double w = 2.0 * std::numbers::pi * cutoff_hz / sample_rate;
        const double alpha = std::sin(w) / (2.0 * q);
        const double cw = std::cos(w);
        const double a0 = 1.0 + alpha;
        b0_ = (high ? (1.0 + cw) : (1.0 - cw)) / 2.0 / a0;
        b1_ = (high ? -(1.0 + cw) : (1.0 - cw)) / a0;
        b2_ = b0_;
        a1_ = -2.0 * cw / a0;
        a2_ = (1.0 - alpha) / a0;
    }
    double b0_, b1_, b2_, a1_, a2_;
    double x1_ = 0.0, x2_ = 0.0, y1_ = 0.0, y2_ = 0.0;
};

// Running sum of squares of a filtered signal, for windowed energies.
struct EnergySum {
    std::vector<double> sum; // sum[i] = energy of samples [0, i)

    // Mean energy over [c - half, c + half), divided by the full window
    // length so the file edges read as fading in.
    double mean(std::size_t c, std::size_t half) const {
        const std::size_t n = sum.size() - 1;
        const std::size_t lo = c > half ? c - half : 0;
        const std::size_t hi = std::min(n, c + half);
        return (sum[hi] - sum[lo]) / static_cast<double>(2 * half);
    }
};

// 4th-order Butterworth low-pass (two sections), or a band from a 2nd-order
// high-pass and 2nd-order low-pass each side when low_hz > 0.
inline EnergySum filtered_energy(std::span<const float> mono, int sample_rate, double low_hz, double high_hz) {
    const double nyq_cap = 0.45 * sample_rate;
    high_hz = std::min(high_hz, nyq_cap);
    std::vector<Biquad> chain;
    if (low_hz > 0.0) {
        chain.push_back(Biquad::highpass(low_hz, 0.54119610, sample_rate));
        chain.push_back(Biquad::highpass(low_hz, 1.30656296, sample_rate));
    }
    chain.push_back(Biquad::lowpass(high_hz, 0.54119610, sample_rate));
    chain.push_back(Biquad::lowpass(high_hz, 1.30656296, sample_rate));
    EnergySum e;
    e.sum.assign(mono.size() + 1, 0.0);
    for (std::size_t i = 0; i < mono.size(); ++i) {
        double y = mono[i];
        for (Biquad& b : chain) y = b(y);
        e.sum[i + 1] = e.sum[i] + y * y;
    }
    return e;
}

// RMS amplitude every `hop` samples over a centred window of 2 * half.
inline std::vector<float> amplitude(const EnergySum& e, std::size_t hop, std::size_t half) {
    const std::size_t n = e.sum.size() - 1;
    if (n == 0) return {};
    const std::size_t frames = (n - 1) / hop + 1;
    std::vector<float> out(frames);
    for (std::size_t f = 0; f < frames; ++f) out[f] = static_cast<float>(std::sqrt(e.mean(f * hop, half)));
    return out;
}

// Maximum of v over [i - radius, i + radius] for every i (monotonic deque).
inline std::vector<float> sliding_max(const std::vector<float>& v, std::size_t radius) {
    const std::size_t n = v.size();
    std::vector<float> out(n);
    std::deque<std::size_t> q;
    std::size_t next = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t hi = std::min(n - 1, i + radius);
        for (; next <= hi; ++next) {
            while (!q.empty() && v[q.back()] <= v[next]) q.pop_back();
            q.push_back(next);
        }
        while (q.front() + radius < i) q.pop_front();
        out[i] = v[q.front()];
    }
    return out;
}

// Rise of amp over `lag` frames (silence before the first frame) divided by
// the loudest amp within `radius` frames, which is floored at `floor`.
inline std::vector<float> relative_jump(const std::vector<float>& amp, std::size_t lag, std::size_t radius, float floor) {
    const std::vector<float> ref = sliding_max(amp, radius);
    std::vector<float> out(amp.size());
    for (std::size_t f = 0; f < amp.size(); ++f) {
        const float before = f >= lag ? amp[f - lag] : 0.0f;
        out[f] = (amp[f] - before) / std::max(ref[f], floor);
    }
    return out;
}

// True when v[f] is the largest value within radius (ties go to the earliest).
inline bool is_local_peak(const std::vector<float>& v, std::size_t f, std::size_t radius) {
    const std::size_t lo = f > radius ? f - radius : 0;
    const std::size_t hi = std::min(v.size() - 1, f + radius);
    for (std::size_t j = lo; j <= hi; ++j) {
        if ((j < f && v[j] >= v[f]) || (j > f && v[j] > v[f])) return false;
    }
    return true;
}

inline float peak_in(const std::vector<float>& v, std::size_t from, std::size_t count) {
    const std::size_t end = std::min(v.size(), from + count);
    float m = 0.0f;
    for (std::size_t i = from; i < end; ++i) m = std::max(m, v[i]);
    return m;
}

} // namespace ztrip::analysis::detail
