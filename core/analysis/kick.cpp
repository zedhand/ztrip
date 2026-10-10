#include "ztrip/analysis/kick.hpp"

#include "ztrip/analysis/bands.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ztrip::analysis {
namespace {

// RBJ cookbook low-pass biquad, run in double for repeatable output.
class Biquad {
public:
    Biquad(double cutoff_hz, double q, int sample_rate) {
        const double w = 2.0 * std::numbers::pi * cutoff_hz / sample_rate;
        const double alpha = std::sin(w) / (2.0 * q);
        const double cw = std::cos(w);
        const double a0 = 1.0 + alpha;
        b0_ = (1.0 - cw) / 2.0 / a0;
        b1_ = (1.0 - cw) / a0;
        b2_ = b0_;
        a1_ = -2.0 * cw / a0;
        a2_ = (1.0 - alpha) / a0;
    }
    double operator()(double x) {
        const double y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_;
        x1_ = x;
        y2_ = y1_;
        y1_ = y;
        return y;
    }

private:
    double b0_, b1_, b2_, a1_, a2_;
    double x1_ = 0.0, x2_ = 0.0, y1_ = 0.0, y2_ = 0.0;
};

} // namespace

std::vector<Trigger> detect_kicks(std::span<const float> mono, int sample_rate,
                                  const KickSettings& settings) {
    std::vector<Trigger> out;
    if (mono.empty() || sample_rate <= 0) return out;
    const double sr = sample_rate;
    const double cutoff = std::min<double>(settings.cutoff_hz, 0.45 * sr);

    // Low-pass (two Butterworth sections) and a running sum of squares.
    Biquad s1(cutoff, 0.54119610, sample_rate);
    Biquad s2(cutoff, 1.30656296, sample_rate);
    std::vector<double> energy_sum(mono.size() + 1, 0.0);
    for (std::size_t i = 0; i < mono.size(); ++i) {
        const double y = s2(s1(mono[i]));
        energy_sum[i + 1] = energy_sum[i] + y * y;
    }

    const auto hop = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(settings.hop_ms * sr / 1000.0)));
    const auto half = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(settings.window_ms * sr / 2000.0)));
    const double frame_rate = sr / static_cast<double>(hop);
    const std::size_t frames = (mono.size() - 1) / hop + 1;
    std::vector<float> db(frames);
    for (std::size_t f = 0; f < frames; ++f) {
        const std::size_t c = f * hop;
        const std::size_t lo = c > half ? c - half : 0;
        const std::size_t hi = std::min(mono.size(), c + half);
        // Mean over the full window length, so the edges read as fading in.
        const double e = (energy_sum[hi] - energy_sum[lo]) / static_cast<double>(2 * half);
        db[f] = static_cast<float>(10.0 * std::log10(e + 1e-12));
    }
    const float top = percentile(db, 99.0f);
    // -100 dBFS is silence however quiet the track is, so digital silence
    // has no range to rise through.
    const float floor_db = std::max(top - settings.range_db, -100.0f);
    for (float& v : db) v = std::max(v, floor_db);

    // Rise over the lag; the signal is silent before the first frame.
    const auto lag = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(settings.lag_ms / 1000.0 * frame_rate)));
    std::vector<float> rise(frames);
    for (std::size_t f = 0; f < frames; ++f) rise[f] = db[f] - (f >= lag ? db[f - lag] : floor_db);

    const auto radius = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(0.025 * frame_rate)));
    const auto peak_span = static_cast<std::size_t>(std::lround(0.030 * frame_rate));
    double last_time = -1e9;
    for (std::size_t f = 0; f < frames; ++f) {
        const float v = rise[f];
        if (v < settings.min_rise_db) continue;

        // Steepest rise within the radius (ties go to the earliest frame).
        const std::size_t lo = f > radius ? f - radius : 0;
        const std::size_t hi = std::min(frames - 1, f + radius);
        bool is_peak = true;
        for (std::size_t j = lo; j <= hi; ++j) {
            if ((j < f && rise[j] >= v) || (j > f && rise[j] > v)) {
                is_peak = false;
                break;
            }
        }
        if (!is_peak) continue;

        const auto level_end = static_cast<std::ptrdiff_t>(std::min(frames, f + peak_span + 1));
        const float level = *std::max_element(db.begin() + static_cast<std::ptrdiff_t>(f), db.begin() + level_end);
        if (level < top - settings.gate_db) continue;

        // db[f] - db[f - lag] is steepest when the window straddles the
        // attack, half a lag after it starts.
        const double time = std::max(0.0, static_cast<double>(f) / frame_rate - settings.lag_ms / 2000.0);
        if (time - last_time < settings.min_gap_s) continue;

        const auto velocity = static_cast<float>(std::pow(10.0, (level - top) / 20.0));
        out.push_back({time, std::clamp(velocity, 0.0f, 1.0f)});
        last_time = time;
    }
    return out;
}

} // namespace ztrip::analysis
