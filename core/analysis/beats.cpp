#include "ztrip/analysis/beats.hpp"

#include "ztrip/analysis/bands.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace ztrip::analysis {
namespace {

// Onset strength with its local mean removed and scaled to unit deviation.
std::vector<float> condition(std::span<const float> o, double frame_rate) {
    const std::size_t n = o.size();
    const auto half = static_cast<std::size_t>(std::max(1.0, std::round(0.5 * frame_rate)));
    std::vector<double> prefix(n + 1, 0.0);
    for (std::size_t i = 0; i < n; ++i) prefix[i + 1] = prefix[i] + o[i];
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t lo = i > half ? i - half : 0;
        const std::size_t hi = std::min(n, i + half + 1);
        const double mean = (prefix[hi] - prefix[lo]) / static_cast<double>(hi - lo);
        out[i] = static_cast<float>(std::max(0.0, o[i] - mean));
    }
    double sq = 0.0;
    for (float v : out) sq += static_cast<double>(v) * v;
    const double sd = std::sqrt(sq / static_cast<double>(std::max<std::size_t>(1, n)));
    if (sd > 0.0) {
        for (float& v : out) v = static_cast<float>(v / sd);
    }
    return out;
}

double prior_weight(double bpm, const BeatSettings& s) {
    const double x = std::log2(bpm / s.prior_bpm) / s.prior_octaves;
    return std::exp(-0.5 * x * x);
}

std::size_t nearest_frame(double time, double frame_rate, std::size_t frames) {
    const double f = std::round(time * frame_rate);
    return static_cast<std::size_t>(std::clamp(f, 0.0, static_cast<double>(frames - 1)));
}

double mean_of(const std::vector<double>& v) {
    return v.empty() ? 0.0 : std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

// Rescales to zero mean and unit deviation (left as zeros when flat).
void standardize(std::vector<double>& v) {
    const double m = mean_of(v);
    double sq = 0.0;
    for (double x : v) sq += (x - m) * (x - m);
    const double sd = std::sqrt(sq / static_cast<double>(std::max<std::size_t>(1, v.size())));
    for (double& x : v) x = sd > 1e-12 ? (x - m) / sd : 0.0;
}

} // namespace

double estimate_tempo(std::span<const float> onset_strength, double frame_rate,
                      const BeatSettings& settings) {
    const std::vector<float> o = condition(onset_strength, frame_rate);
    const auto min_lag = static_cast<std::size_t>(std::floor(60.0 * frame_rate / settings.max_bpm));
    const auto max_lag = static_cast<std::size_t>(std::ceil(60.0 * frame_rate / settings.min_bpm));
    if (min_lag < 1 || o.size() < 2 * max_lag + 2) return 0.0;

    std::vector<double> ac(max_lag + 2, 0.0);
    for (std::size_t lag = min_lag - 1; lag <= max_lag + 1; ++lag) {
        double sum = 0.0;
        for (std::size_t t = 0; t + lag < o.size(); ++t) sum += static_cast<double>(o[t]) * o[t + lag];
        ac[lag] = sum / static_cast<double>(o.size() - lag);
    }

    std::size_t best = 0;
    double best_score = 0.0;
    for (std::size_t lag = min_lag; lag <= max_lag; ++lag) {
        const double score = ac[lag] * prior_weight(60.0 * frame_rate / static_cast<double>(lag), settings);
        if (score > best_score) {
            best_score = score;
            best = lag;
        }
    }
    if (best == 0) return 0.0;

    // Parabolic refinement of the autocorrelation peak for a fractional lag.
    double lag = static_cast<double>(best);
    const double a = ac[best - 1];
    const double b = ac[best];
    const double c = ac[best + 1];
    const double denom = a - 2.0 * b + c;
    if (denom < 0.0) lag += std::clamp(0.5 * (a - c) / denom, -0.5, 0.5);
    return 60.0 * frame_rate / lag;
}

std::vector<double> track_beats(std::span<const float> onset_strength, double frame_rate,
                                double bpm, const BeatSettings& settings) {
    std::vector<double> beats;
    const std::size_t n = onset_strength.size();
    if (bpm <= 0.0 || n < 3) return beats;
    const double period = 60.0 * frame_rate / bpm;
    const std::vector<float> o = condition(onset_strength, frame_rate);

    // Local score: onset strength smoothed by a narrow Gaussian (period / 32).
    const auto radius = static_cast<std::ptrdiff_t>(std::ceil(period));
    std::vector<double> kernel(static_cast<std::size_t>(2 * radius + 1));
    for (std::ptrdiff_t k = -radius; k <= radius; ++k) {
        const double x = static_cast<double>(k) * 32.0 / period;
        kernel[static_cast<std::size_t>(k + radius)] = std::exp(-0.5 * x * x);
    }
    std::vector<double> local(n, 0.0);
    const auto sn = static_cast<std::ptrdiff_t>(n);
    for (std::ptrdiff_t t = 0; t < sn; ++t) {
        double sum = 0.0;
        for (std::ptrdiff_t k = -radius; k <= radius; ++k) {
            const std::ptrdiff_t j = t + k;
            if (j >= 0 && j < sn) sum += o[static_cast<std::size_t>(j)] * kernel[static_cast<std::size_t>(k + radius)];
        }
        local[static_cast<std::size_t>(t)] = sum;
    }

    // Cumulative score with a log-squared penalty on deviation from the period.
    std::vector<double> cum(n, 0.0);
    std::vector<std::ptrdiff_t> back(n, -1);
    const auto min_step = static_cast<std::ptrdiff_t>(std::round(period / 2.0));
    const auto max_step = static_cast<std::ptrdiff_t>(std::round(period * 2.0));
    for (std::ptrdiff_t t = 0; t < sn; ++t) {
        double best = -std::numeric_limits<double>::infinity();
        std::ptrdiff_t arg = -1;
        for (std::ptrdiff_t step = std::max<std::ptrdiff_t>(1, min_step); step <= max_step; ++step) {
            const std::ptrdiff_t tau = t - step;
            if (tau < 0) break;
            const double dev = std::log(static_cast<double>(step) / period);
            const double score = cum[static_cast<std::size_t>(tau)] - settings.tightness * dev * dev;
            if (score > best) {
                best = score;
                arg = tau;
            }
        }
        const auto ut = static_cast<std::size_t>(t);
        cum[ut] = local[ut] + (arg >= 0 ? best : 0.0);
        back[ut] = arg;
    }

    // Last beat: the latest local maximum of the cumulative score that is
    // above half the median of all its local maxima.
    std::vector<double> maxima;
    for (std::size_t t = 1; t + 1 < n; ++t) {
        if (cum[t] > cum[t - 1] && cum[t] >= cum[t + 1]) maxima.push_back(cum[t]);
    }
    if (maxima.empty()) return beats;
    std::vector<double> sorted = maxima;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2), sorted.end());
    const double threshold = 0.5 * sorted[sorted.size() / 2];
    std::ptrdiff_t t = -1;
    for (std::size_t i = n - 2; i >= 1; --i) {
        if (cum[i] > cum[i - 1] && cum[i] >= cum[i + 1] && cum[i] > threshold) {
            t = static_cast<std::ptrdiff_t>(i);
            break;
        }
    }
    std::vector<std::size_t> frames;
    for (; t >= 0; t = back[static_cast<std::size_t>(t)]) frames.push_back(static_cast<std::size_t>(t));
    std::reverse(frames.begin(), frames.end());

    // Trim leading and trailing beats that sit in near silence.
    if (frames.size() > 2) {
        double sq = 0.0;
        for (std::size_t f : frames) sq += local[f] * local[f];
        const double floor = 0.5 * std::sqrt(sq / static_cast<double>(frames.size()));
        std::size_t first = 0;
        std::size_t last = frames.size();
        while (first < last && local[frames[first]] < floor) ++first;
        while (last > first && local[frames[last - 1]] < floor) --last;
        frames = std::vector<std::size_t>(frames.begin() + static_cast<std::ptrdiff_t>(first),
                                          frames.begin() + static_cast<std::ptrdiff_t>(last));
    }

    // Sub-frame refinement on the local score.
    beats.reserve(frames.size());
    for (std::size_t f : frames) {
        double pos = static_cast<double>(f);
        if (f > 0 && f + 1 < n) {
            const double a = local[f - 1];
            const double b = local[f];
            const double c = local[f + 1];
            const double denom = a - 2.0 * b + c;
            if (denom < 0.0) pos += std::clamp(0.5 * (a - c) / denom, -0.5, 0.5);
        }
        beats.push_back(std::max(0.0, pos / frame_rate));
    }
    return beats;
}

int estimate_downbeat_phase(const Spectrogram& spec, std::span<const double> beats,
                            int beats_per_bar) {
    if (beats_per_bar <= 1 || beats.size() < static_cast<std::size_t>(2 * beats_per_bar)) return 0;
    const double fr = spec.frame_rate();
    const std::size_t nb = beats.size();

    const std::vector<float> low = band_flux(spec, {"low", 20.0f, 150.0f});
    const std::vector<float> back = band_flux(spec, {"backbeat", 1500.0f, 6000.0f});
    auto peak_near = [&](const std::vector<float>& v, double time) {
        const std::size_t c = nearest_frame(time, fr, spec.frames);
        const std::size_t lo = c >= 2 ? c - 2 : 0;
        const std::size_t hi = std::min(spec.frames - 1, c + 2);
        return static_cast<double>(*std::max_element(v.begin() + static_cast<std::ptrdiff_t>(lo),
                                                     v.begin() + static_cast<std::ptrdiff_t>(hi + 1)));
    };

    // Beat-synchronous log spectrum (60 Hz to 4 kHz) for harmonic change.
    std::size_t k0 = 1;
    while (k0 < spec.bins && spec.bin_hz(k0) < 60.0) ++k0;
    std::size_t k1 = k0;
    while (k1 < spec.bins && spec.bin_hz(k1) < 4000.0) ++k1;
    std::vector<std::vector<double>> seg(nb, std::vector<double>(k1 - k0, 0.0));
    for (std::size_t i = 0; i < nb; ++i) {
        const std::size_t f0 = nearest_frame(beats[i], fr, spec.frames);
        const std::size_t f1 = i + 1 < nb ? nearest_frame(beats[i + 1], fr, spec.frames) : std::min(spec.frames, f0 + 1);
        const std::size_t count = std::max<std::size_t>(1, f1 > f0 ? f1 - f0 : 1);
        for (std::size_t f = f0; f < f0 + count && f < spec.frames; ++f) {
            const auto frame = spec.frame(f);
            for (std::size_t k = k0; k < k1; ++k) seg[i][k - k0] += std::log1p(100.0 * frame[k]);
        }
        for (double& v : seg[i]) v /= static_cast<double>(count);
    }

    std::vector<double> low_s(nb), back_s(nb), change(nb, 0.0);
    for (std::size_t i = 0; i < nb; ++i) {
        low_s[i] = peak_near(low, beats[i]);
        back_s[i] = peak_near(back, beats[i]);
        if (i > 0) {
            double d = 0.0;
            for (std::size_t k = 0; k < seg[i].size(); ++k) d += std::fabs(seg[i][k] - seg[i - 1][k]);
            change[i] = d;
        }
    }
    standardize(low_s);
    standardize(back_s);
    standardize(change);

    int best_phase = 0;
    double best = -std::numeric_limits<double>::infinity();
    const auto bpb = static_cast<std::size_t>(beats_per_bar);
    for (std::size_t p = 0; p < bpb; ++p) {
        double score = 0.0;
        std::size_t count = 0;
        for (std::size_t i = p; i < nb; i += bpb) {
            score += low_s[i] + change[i] - back_s[i];
            ++count;
        }
        score /= static_cast<double>(std::max<std::size_t>(1, count));
        if (score > best) {
            best = score;
            best_phase = static_cast<int>(p);
        }
    }
    return best_phase;
}

BeatGrid analyze_beats(const Spectrogram& spec, std::span<const float> onset_strength,
                       const BeatSettings& settings) {
    BeatGrid grid;
    grid.beats_per_bar = std::max(1, settings.beats_per_bar);
    const double fr = spec.frame_rate();
    const double tempo = estimate_tempo(onset_strength, fr, settings);
    grid.beats = track_beats(onset_strength, fr, tempo, settings);
    if (grid.beats.size() >= 2) {
        std::vector<double> ibi(grid.beats.size() - 1);
        for (std::size_t i = 0; i + 1 < grid.beats.size(); ++i) ibi[i] = grid.beats[i + 1] - grid.beats[i];
        std::nth_element(ibi.begin(), ibi.begin() + static_cast<std::ptrdiff_t>(ibi.size() / 2), ibi.end());
        grid.bpm = 60.0 / ibi[ibi.size() / 2];
    } else {
        grid.bpm = tempo;
    }
    grid.downbeat_phase = estimate_downbeat_phase(spec, grid.beats, grid.beats_per_bar);
    for (std::size_t i = static_cast<std::size_t>(grid.downbeat_phase); i < grid.beats.size();
         i += static_cast<std::size_t>(grid.beats_per_bar)) {
        grid.downbeats.push_back(grid.beats[i]);
    }
    return grid;
}

} // namespace ztrip::analysis
