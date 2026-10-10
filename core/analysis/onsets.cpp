#include "ztrip/analysis/onsets.hpp"

#include "ztrip/analysis/bands.hpp"

#include <algorithm>
#include <cmath>

namespace ztrip::analysis {
namespace {

float median_of(std::vector<float>& scratch) {
    const auto mid = scratch.begin() + static_cast<std::ptrdiff_t>(scratch.size() / 2);
    std::nth_element(scratch.begin(), mid, scratch.end());
    return *mid;
}

} // namespace

std::vector<Trigger> pick_onsets(std::span<const float> flux, double frame_rate,
                                 const OnsetSettings& settings) {
    std::vector<Trigger> out;
    const std::size_t n = flux.size();
    if (n < 3) return out;

    // Normalise against the loud end of the distribution, not a single spike.
    std::vector<float> peaks;
    for (std::size_t i = 1; i + 1 < n; ++i) {
        if (flux[i] > 0.0f && flux[i] >= flux[i - 1] && flux[i] >= flux[i + 1]) {
            peaks.push_back(flux[i]);
        }
    }
    const float scale = std::max(percentile(peaks, 99.5f), settings.min_scale);
    if (scale <= 0.0f) return out;
    std::vector<float> f(n);
    for (std::size_t i = 0; i < n; ++i) f[i] = flux[i] / scale;

    const auto half_window = static_cast<std::size_t>(
        std::max(1.0, std::round(settings.window_s * 0.5 * frame_rate)));
    const auto peak_radius = static_cast<std::size_t>(
        std::max(1.0, std::round(settings.min_gap_s * 0.5 * frame_rate)));
    std::vector<float> scratch;
    double last_time = -1e9;

    for (std::size_t i = 1; i + 1 < n; ++i) {
        const float v = f[i];
        if (v <= settings.delta) continue;

        // Local maximum within the peak radius (ties go to the earliest frame).
        const std::size_t lo = i > peak_radius ? i - peak_radius : 0;
        const std::size_t hi = std::min(n - 1, i + peak_radius);
        bool is_peak = true;
        for (std::size_t j = lo; j <= hi; ++j) {
            if ((j < i && f[j] >= v) || (j > i && f[j] > v)) {
                is_peak = false;
                break;
            }
        }
        if (!is_peak) continue;

        const std::size_t mlo = i > half_window ? i - half_window : 0;
        const std::size_t mhi = std::min(n - 1, i + half_window);
        scratch.assign(f.begin() + static_cast<std::ptrdiff_t>(mlo),
                       f.begin() + static_cast<std::ptrdiff_t>(mhi + 1));
        if (v <= settings.delta + settings.threshold * median_of(scratch)) continue;

        // Parabolic interpolation of the peak position.
        const float a = f[i - 1];
        const float c = f[i + 1];
        const float denom = a - 2.0f * v + c;
        const double offset = denom < 0.0f ? std::clamp(0.5f * (a - c) / denom, -0.5f, 0.5f) : 0.0f;
        const double time = (static_cast<double>(i) + offset) / frame_rate;
        if (time - last_time < settings.min_gap_s) continue;

        out.push_back({time, std::clamp(v, 0.0f, 1.0f)});
        last_time = time;
    }
    return out;
}

} // namespace ztrip::analysis
