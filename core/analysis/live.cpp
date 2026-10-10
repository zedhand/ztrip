#include "ztrip/analysis/live.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ztrip::analysis {
namespace {

// Same mapping as the offline centroid signal: log frequency, 20 Hz..20 kHz.
float centroid_level(float hz) {
    return hz > 0.0f ? std::clamp(std::log2(hz / 20.0f) / std::log2(1000.0f), 0.0f, 1.0f) : 0.0f;
}

float full_band_high(int sample_rate) {
    return std::min(16000.0f, static_cast<float>(sample_rate) / 2.0f);
}

} // namespace

float LiveAnalyzer::Normalizer::next(float db, const LiveSettings& s, float k) {
    if (db > hi) hi = db; else hi += (db - hi) * k;
    hi = std::max(hi, s.min_peak_db);
    if (db < lo) lo = db; else lo += (db - lo) * k;
    lo = std::clamp(lo, hi - s.range_db, hi - s.min_range_db);
    return std::clamp((db - lo) / (hi - lo), 0.0f, 1.0f);
}

LiveAnalyzer::LiveAnalyzer(int sample_rate, LiveSettings settings)
    : sample_rate_(sample_rate),
      settings_(std::move(settings)),
      transform_(settings_.stft.fft_size, settings_.stft.window),
      hop_(settings_.stft.effective_hop()),
      full_flux_(band_bins(sample_rate, settings_.stft.fft_size, {"full", 20.0f, full_band_high(sample_rate)}),
                 transform_.bins()) {
    if (sample_rate_ <= 0) throw std::invalid_argument("live analysis: invalid sample rate");
    if (hop_ <= 0 || hop_ > settings_.stft.fft_size) {
        throw std::invalid_argument("live analysis: hop must be in 1..fft_size");
    }
    if (!(settings_.signal_rate_hz > 0.0)) throw std::invalid_argument("live analysis: invalid signal rate");
    frame_rate_ = static_cast<double>(sample_rate_) / hop_;
    gain_ = std::pow(10.0f, settings_.gain_db / 20.0f);
    release_k_ = settings_.norm_release_s > 0.0f
                     ? static_cast<float>(1.0 - std::exp(-1.0 / (frame_rate_ * settings_.norm_release_s)))
                     : 1.0f;

    const auto n = static_cast<std::size_t>(settings_.stft.fft_size);
    frame_.resize(n);
    magnitude_.resize(transform_.bins());
    for (const Band& b : settings_.bands) {
        band_bins_.push_back(band_bins(sample_rate_, settings_.stft.fft_size, b));
        band_flux_.emplace_back(band_bins_.back(), transform_.bins());
        env_.emplace_back(settings_.signal_rate_hz, settings_.envelope);
    }
    band_norm_.resize(settings_.bands.size());
    band_onsets_.resize(settings_.bands.size());

    // Same names, in the same order, as the offline result.
    result_.signal_rate_hz = settings_.signal_rate_hz;
    for (const Band& b : settings_.bands) {
        band_signal_.push_back(result_.signals.size());
        result_.signals.push_back({"band." + b.name, {}});
        env_signal_.push_back(result_.signals.size());
        result_.signals.push_back({"env." + b.name, {}});
        band_events_.push_back(result_.events.size());
        result_.events.push_back({"onset." + b.name, {}});
    }
    rms_signal_ = result_.signals.size();
    result_.signals.push_back({"rms", {}});
    centroid_signal_ = result_.signals.size();
    result_.signals.push_back({"centroid", {}});
    full_events_ = result_.events.size();
    result_.events.push_back({"onset.full", {}});
    result_.events.push_back({"kick", {}});
    result_.events.push_back({"beat", {}});
    result_.events.push_back({"downbeat", {}});

    init_state();
}

void LiveAnalyzer::init_state() {
    window_.assign(frame_.size(), 0.0f);
    window_pos_ = 0;
    samples_ = 0;
    since_frame_ = 0;
    known_time_ = 0.0;
    for (BandFlux& f : band_flux_) f.reset();
    full_flux_.reset();
    for (EnvelopeFollower& e : env_) e.reset();
    const Normalizer fresh{settings_.min_peak_db - settings_.range_db, settings_.min_peak_db};
    std::fill(band_norm_.begin(), band_norm_.end(), fresh);
    rms_norm_ = fresh;
    const auto history = static_cast<std::size_t>(
        std::max(1.0, std::round(settings_.onsets.window_s * frame_rate_)));
    auto fresh_picker = [&](OnsetPicker& p) {
        p = OnsetPicker{};
        p.history.assign(history, 0.0f);
        p.scale = settings_.onsets.min_scale;
    };
    for (OnsetPicker& p : band_onsets_) fresh_picker(p);
    fresh_picker(full_onsets_);
    prev_levels_.clear();
    prev_frame_time_ = -1.0;
    next_tick_ = 0;
    result_.duration_s = 0.0;
    for (SignalBuffer& s : result_.signals) s.values.clear();
    for (EventList& e : result_.events) e.events.clear();
}

void LiveAnalyzer::reset() {
    init_state();
}

void LiveAnalyzer::push(std::span<const float> mono) {
    for (float x : mono) {
        window_[window_pos_] = x * gain_;
        window_pos_ = (window_pos_ + 1) % window_.size();
        ++samples_;
        if (++since_frame_ == hop_) {
            since_frame_ = 0;
            process_frame();
        }
    }
}

void LiveAnalyzer::push_silence(std::size_t count) {
    static constexpr float zeros[256] = {};
    while (count > 0) {
        const std::size_t n = std::min<std::size_t>(count, 256);
        push(std::span<const float>(zeros, n));
        count -= n;
    }
}

void LiveAnalyzer::process_frame() {
    const double t = static_cast<double>(samples_) / sample_rate_;
    // Oldest sample first: window_pos_ is where the next sample will go.
    const std::size_t n = window_.size();
    std::copy(window_.begin() + static_cast<std::ptrdiff_t>(window_pos_), window_.end(), frame_.begin());
    std::copy(window_.begin(), window_.begin() + static_cast<std::ptrdiff_t>(window_pos_),
              frame_.begin() + static_cast<std::ptrdiff_t>(n - window_pos_));
    transform_.magnitude(frame_, magnitude_);

    const std::size_t bands = settings_.bands.size();
    levels_.resize(bands + 2);
    for (std::size_t b = 0; b < bands; ++b) {
        levels_[b] = band_norm_[b].next(frame_energy_db(magnitude_, band_bins_[b]), settings_, release_k_);
        pick(band_onsets_[b], band_flux_[b].next(magnitude_), t, result_.events[band_events_[b]]);
    }
    double power = 0.0;
    for (float m : magnitude_) power += static_cast<double>(m) * m;
    levels_[bands] = rms_norm_.next(static_cast<float>(10.0 * std::log10(power + 1e-12)), settings_, release_k_);
    levels_[bands + 1] = centroid_level(frame_centroid_hz(magnitude_, sample_rate_, settings_.stft.fft_size));
    pick(full_onsets_, full_flux_.next(magnitude_), t, result_.events[full_events_]);

    emit_ticks(t, levels_);
    known_time_ = t;
    result_.duration_s = t;
}

void LiveAnalyzer::emit_ticks(double frame_time, std::span<const float> levels) {
    const std::size_t bands = settings_.bands.size();
    // Every signal tick up to this frame, interpolated from the previous one.
    while (static_cast<double>(next_tick_) / settings_.signal_rate_hz <= frame_time) {
        const double t = static_cast<double>(next_tick_) / settings_.signal_rate_hz;
        const bool between = prev_frame_time_ >= 0.0 && t > prev_frame_time_;
        const auto frac = between ? static_cast<float>((t - prev_frame_time_) / (frame_time - prev_frame_time_)) : 1.0f;
        auto at = [&](std::size_t i) {
            return between ? prev_levels_[i] + (levels[i] - prev_levels_[i]) * frac : levels[i];
        };
        for (std::size_t b = 0; b < bands; ++b) {
            const float level = at(b);
            result_.signals[band_signal_[b]].values.push_back(level);
            result_.signals[env_signal_[b]].values.push_back(env_[b].next(level));
        }
        result_.signals[rms_signal_].values.push_back(at(bands));
        result_.signals[centroid_signal_].values.push_back(at(bands + 1));
        ++next_tick_;
    }
    prev_levels_.assign(levels.begin(), levels.end());
    prev_frame_time_ = frame_time;
}

// The previous frame is an onset when it is a local peak (ties to the
// earlier frame, as offline), clears delta + threshold * the median of the
// trailing window (both relative to a running peak of the flux, the live
// stand-in for the offline percentile), and is min_gap_s after the last
// onset. It is stamped now, one hop after the peak, when it is known.
void LiveAnalyzer::pick(OnsetPicker& p, float flux, double frame_time, EventList& out) {
    const OnsetSettings& s = settings_.onsets;
    const float c = p.prev;
    if (c > p.prev2 && c >= flux && frame_time - p.last_onset >= s.min_gap_s) {
        p.scratch.assign(p.history.begin(), p.history.begin() + static_cast<std::ptrdiff_t>(std::max<std::size_t>(p.count, 1)));
        const auto mid = p.scratch.begin() + static_cast<std::ptrdiff_t>(p.scratch.size() / 2);
        std::nth_element(p.scratch.begin(), mid, p.scratch.end());
        if (c > s.delta * p.scale + s.threshold * *mid) {
            out.events.push_back({frame_time, std::clamp(c / p.scale, 0.0f, 1.0f)});
            p.last_onset = frame_time;
        }
    }
    p.history[p.pos] = flux;
    p.pos = (p.pos + 1) % p.history.size();
    p.count = std::min(p.count + 1, p.history.size());
    if (flux > p.scale) p.scale = flux; else p.scale += (flux - p.scale) * release_k_;
    p.scale = std::max(p.scale, s.min_scale);
    p.prev2 = p.prev;
    p.prev = flux;
}

} // namespace ztrip::analysis
