#include "ztrip/analysis/kick.hpp"

#include "ztrip/analysis/bands.hpp"

#include "drum_env.hpp"

#include <algorithm>
#include <cmath>

namespace ztrip::analysis {

using namespace detail;

std::vector<Trigger> detect_kicks(std::span<const float> mono, int sample_rate,
                                  const KickSettings& settings) {
    std::vector<Trigger> out;
    if (mono.empty() || sample_rate <= 0) return out;
    const double sr = sample_rate;
    const EnergySum low = filtered_energy(mono, sample_rate, 0.0, settings.cutoff_hz);
    auto to_samples = [&](double ms) {
        return std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(ms * sr / 1000.0)));
    };

    const std::size_t hop = to_samples(settings.hop_ms);
    const std::size_t half = std::max<std::size_t>(1, to_samples(settings.window_ms) / 2);
    const double frame_rate = sr / static_cast<double>(hop);
    const std::vector<float> amp = amplitude(low, hop, half);
    const std::size_t frames = amp.size();
    const float top = percentile(amp, 99.0f);
    if (!(top > 0.0f)) return out;

    // Jump over the lag against the loudest level within reference_s.
    auto frames_of = [&](double seconds) {
        return static_cast<std::size_t>(std::max(0.0, std::round(seconds * frame_rate)));
    };
    const std::vector<float> jump = relative_jump(amp, std::max<std::size_t>(1, frames_of(settings.lag_ms / 1000.0)),
                                                  frames_of(settings.reference_s),
                                                  top * static_cast<float>(std::pow(10.0, -settings.floor_db / 20.0)));

    struct Hit {
        std::size_t frame;
        float strength;
        float level;
    };
    std::vector<Hit> hits;
    const std::size_t radius = std::max<std::size_t>(1, frames_of(0.025));
    const std::size_t peak_span = frames_of(0.030);
    const float gate = top * static_cast<float>(std::pow(10.0, -settings.gate_db / 20.0));
    for (std::size_t f = 0; f < frames; ++f) {
        if (jump[f] < settings.min_jump || !is_local_peak(jump, f, radius)) continue;
        const float level = peak_in(amp, f, peak_span + 1);
        if (level < gate) continue;
        hits.push_back({f, jump[f], level});
    }

    // Strongest first; drop hits within min_gap_s of one already kept.
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.strength > b.strength; });
    const double gap = settings.min_gap_s * frame_rate;
    std::vector<Hit> kept;
    for (const Hit& h : hits) {
        const bool clear = std::none_of(kept.begin(), kept.end(), [&](const Hit& k) {
            return std::fabs(static_cast<double>(h.frame) - static_cast<double>(k.frame)) <= gap;
        });
        if (clear) kept.push_back(h);
    }
    std::sort(kept.begin(), kept.end(), [](const Hit& a, const Hit& b) { return a.frame < b.frame; });

    // Timing: the steepest rise of the envelope in dB over 5 ms, sampled
    // every 0.5 ms, from 20 ms before to 10 ms after the coarse jump (which
    // sits half a lag after the attack).
    const std::size_t fine_hop = to_samples(0.5);
    const std::size_t fine_half = half;
    const std::size_t fine_lag = 10;
    auto fine_db = [&](std::size_t k) { return 10.0 * std::log10(low.mean(k * fine_hop, fine_half) + 1e-12); };
    const double fine_rate = sr / static_cast<double>(fine_hop);
    const std::size_t fine_frames = (mono.size() - 1) / fine_hop + 1;
    out.reserve(kept.size());
    for (const Hit& h : kept) {
        const double coarse = std::max(0.0, static_cast<double>(h.frame) / frame_rate - settings.lag_ms / 2000.0);
        const auto lo = std::max<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(fine_lag),
                                                 static_cast<std::ptrdiff_t>((coarse - 0.020) * fine_rate));
        const auto hi = std::min<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(fine_frames) - 1,
                                                 static_cast<std::ptrdiff_t>((coarse + 0.010) * fine_rate));
        double time = coarse;
        double best = -1e300;
        for (std::ptrdiff_t k = lo; k <= hi; ++k) {
            const auto uk = static_cast<std::size_t>(k);
            const double rise = fine_db(uk) - fine_db(uk - fine_lag);
            if (rise > best) {
                best = rise;
                time = static_cast<double>(k) / fine_rate - 0.0025;
            }
        }
        out.push_back({std::max(0.0, time), std::clamp(h.level / top, 0.0f, 1.0f)});
    }
    return out;
}

} // namespace ztrip::analysis
