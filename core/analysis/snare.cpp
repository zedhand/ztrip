#include "ztrip/analysis/snare.hpp"

#include "ztrip/analysis/bands.hpp"

#include "drum_env.hpp"

#include <algorithm>
#include <cmath>

namespace ztrip::analysis {

using namespace detail;

std::vector<Trigger> detect_snares(std::span<const float> mono, int sample_rate,
                                   const SnareSettings& settings) {
    std::vector<Trigger> out;
    if (mono.empty() || sample_rate <= 0) return out;
    const double sr = sample_rate;
    auto to_samples = [&](double ms) {
        return std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(ms * sr / 1000.0)));
    };
    const std::size_t hop = to_samples(settings.hop_ms);
    const std::size_t half = std::max<std::size_t>(1, to_samples(settings.window_ms) / 2);
    const double frame_rate = sr / static_cast<double>(hop);
    auto frames_of = [&](double seconds) {
        return static_cast<std::size_t>(std::max(0.0, std::round(seconds * frame_rate)));
    };
    const std::vector<float> body = amplitude(filtered_energy(mono, sample_rate, 150.0, 350.0), hop, half);
    const std::vector<float> mid = amplitude(filtered_energy(mono, sample_rate, 1000.0, 4000.0), hop, half);
    const std::vector<float> high = amplitude(filtered_energy(mono, sample_rate, 4000.0, 10000.0), hop, half);
    const std::size_t frames = mid.size();
    const float body_top = percentile(body, 99.0f);
    const float mid_top = percentile(mid, 99.0f);
    const float high_top = percentile(high, 99.0f);
    if (!(mid_top > 0.0f) || !(high_top > 0.0f)) return out;

    const auto db_below = [](float top, float db) { return top * static_cast<float>(std::pow(10.0, -db / 20.0)); };
    const std::size_t lag = std::max<std::size_t>(1, frames_of(settings.lag_ms / 1000.0));
    const std::size_t ref = frames_of(settings.reference_s);
    const std::vector<float> mid_jump = relative_jump(mid, lag, ref, db_below(mid_top, settings.floor_db));
    const std::vector<float> high_jump = relative_jump(high, lag, ref, db_below(high_top, settings.floor_db));

    const std::size_t radius = std::max<std::size_t>(1, frames_of(0.025));
    const std::size_t near = frames_of(0.0075);
    const std::size_t peak_span = frames_of(0.030) + 1;
    const std::size_t tail_from = frames_of(0.040);
    const std::size_t tail_to = frames_of(0.060);
    double last_time = -1e9;
    for (std::size_t f = 0; f < frames; ++f) {
        if (mid_jump[f] < settings.min_jump || !is_local_peak(mid_jump, f, radius)) continue;
        const std::size_t lo = f > near ? f - near : 0;
        float hj = 0.0f;
        for (std::size_t j = lo; j <= std::min(frames - 1, f + near); ++j) hj = std::max(hj, high_jump[j]);
        if (hj < settings.min_high_jump) continue;

        const float mid_peak = peak_in(mid, f, peak_span);
        if (mid_peak < db_below(mid_top, settings.gate_db)) continue;
        if (peak_in(high, f, peak_span) < db_below(high_top, settings.high_gate_db)) continue;
        if (peak_in(body, f, peak_span) < db_below(body_top, settings.body_gate_db)) continue;

        // The wires keep ringing: mean level 40-60 ms on, in dB.
        if (f + tail_to >= frames) continue;
        double tail_db = 0.0;
        for (std::size_t j = f + tail_from; j <= f + tail_to; ++j) tail_db += 20.0 * std::log10(mid[j] + 1e-9);
        tail_db /= static_cast<double>(tail_to - tail_from + 1);
        if (tail_db - 20.0 * std::log10(mid_peak + 1e-9) < -settings.max_decay_db) continue;

        const double time = std::max(0.0, static_cast<double>(f) / frame_rate - settings.lag_ms / 2000.0);
        if (time - last_time < settings.min_gap_s) continue;
        out.push_back({time, std::clamp(mid_peak / mid_top, 0.0f, 1.0f)});
        last_time = time;
    }
    return out;
}

} // namespace ztrip::analysis
