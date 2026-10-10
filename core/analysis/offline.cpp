#include "ztrip/analysis/offline.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace ztrip::analysis {
namespace {

void add_signal(AnalysisResult& r, std::string name, std::vector<float> values) {
    r.signals.push_back({std::move(name), std::move(values)});
}

class Hasher {
public:
    template <typename T>
    void add(const T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        h_ = fnv1a(&v, sizeof v, h_);
    }
    void add(std::string_view s) {
        add(s.size());
        h_ = fnv1a(s.data(), s.size(), h_);
    }
    std::uint64_t value() const { return h_; }

private:
    std::uint64_t h_ = 0xcbf29ce484222325ull;
};

// Bumped whenever the pipeline changes in a way that alters its output, so
// old caches are regenerated.
constexpr std::uint32_t kPipelineVersion = 2;

} // namespace

const SignalBuffer* AnalysisResult::find_signal(std::string_view name) const noexcept {
    for (const auto& s : signals) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

const EventList* AnalysisResult::find_events(std::string_view name) const noexcept {
    for (const auto& e : events) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

float AnalysisResult::sample(const SignalBuffer& signal, double t) const noexcept {
    const auto& v = signal.values;
    if (v.empty()) return 0.0f;
    const double pos = std::max(0.0, t * signal_rate_hz);
    const auto i = static_cast<std::size_t>(pos);
    if (i + 1 >= v.size()) return v.back();
    const auto frac = static_cast<float>(pos - static_cast<double>(i));
    return v[i] + (v[i + 1] - v[i]) * frac;
}

AnalysisResult analyze(std::span<const float> mono, int sample_rate, const AnalysisSettings& settings) {
    const Spectrogram spec = compute_stft(mono, sample_rate, settings.stft);
    const double fr = spec.frame_rate();

    AnalysisResult r;
    r.signal_rate_hz = settings.signal_rate_hz;
    r.duration_s = static_cast<double>(mono.size()) / sample_rate;
    const auto count = static_cast<std::size_t>(std::floor(r.duration_s * r.signal_rate_hz)) + 1;
    auto to_signal_rate = [&](const std::vector<float>& per_frame) {
        return resample_linear(per_frame, fr, r.signal_rate_hz, count);
    };

    for (const Band& band : settings.bands) {
        const std::vector<float> db = band_energy_db(spec, band);
        // 60 dB of range below the loud end keeps silence from compressing it.
        std::vector<float> level = to_signal_rate(normalize_percentile(db, 5.0f, 99.0f, 60.0f));
        std::vector<float> env = follow_envelope(level, r.signal_rate_hz, settings.envelope);
        add_signal(r, "band." + band.name, std::move(level));
        add_signal(r, "env." + band.name, std::move(env));
        r.events.push_back({"onset." + band.name, pick_onsets(band_flux(spec, band), fr, settings.onsets)});
    }

    // RMS from the STFT frames (Parseval over the window-normalised spectrum
    // is not exact, but the percentile normalisation makes the scale moot).
    std::vector<float> rms_db(spec.frames);
    for (std::size_t f = 0; f < spec.frames; ++f) {
        double sum = 0.0;
        for (float m : spec.frame(f)) sum += static_cast<double>(m) * m;
        rms_db[f] = static_cast<float>(10.0 * std::log10(sum + 1e-12));
    }
    add_signal(r, "rms", to_signal_rate(normalize_percentile(rms_db, 5.0f, 99.0f, 60.0f)));

    std::vector<float> centroid = spectral_centroid(spec);
    for (float& c : centroid) {
        c = c > 0.0f ? std::clamp(std::log2(c / 20.0f) / std::log2(1000.0f), 0.0f, 1.0f) : 0.0f;
    }
    add_signal(r, "centroid", to_signal_rate(centroid));

    const float nyquist = static_cast<float>(sample_rate) / 2.0f;
    const std::vector<float> full_flux = band_flux(spec, {"full", 20.0f, std::min(16000.0f, nyquist)});
    r.events.push_back({"onset.full", pick_onsets(full_flux, fr, settings.onsets)});

    // Beat tracking listens to the default bands with equal weight, so the
    // many high-frequency bins (hats) don't outvote the kick and snare. Sub
    // is left out: at common FFT sizes it spans one or two bins and sees a
    // kick only as its pitch sweeps down, tens of ms late, which pulled the
    // beat phase around on live material.
    std::vector<float> beat_strength(spec.frames, 0.0f);
    for (const Band& band : default_bands()) {
        if (band.low_hz < 60.0f) continue;
        const std::vector<float> flux = band_flux(spec, band);
        const float scale = std::max(percentile(flux, 99.0f), settings.onsets.min_scale);
        for (std::size_t f = 0; f < spec.frames; ++f) beat_strength[f] += flux[f] / scale;
    }
    r.beats = analyze_beats(spec, beat_strength, settings.beats);
    EventList beats{"beat", {}};
    EventList downbeats{"downbeat", {}};
    for (double t : r.beats.beats) beats.events.push_back({t, 1.0f});
    for (double t : r.beats.downbeats) downbeats.events.push_back({t, 1.0f});
    r.events.push_back(std::move(beats));
    r.events.push_back(std::move(downbeats));
    return r;
}

AnalysisResult analyze(const io::AudioBuffer& audio, const AnalysisSettings& settings) {
    const std::vector<float> mono = io::to_mono(audio, settings.channel);
    return analyze(mono, audio.sample_rate, settings);
}

std::uint64_t fnv1a(const void* data, std::size_t size, std::uint64_t seed) {
    const auto* p = static_cast<const unsigned char*>(data);
    std::uint64_t h = seed;
    for (std::size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

std::uint64_t audio_hash(const io::AudioBuffer& audio) {
    Hasher h;
    h.add(audio.sample_rate);
    h.add(audio.channels);
    std::uint64_t v = h.value();
    return fnv1a(audio.samples.data(), audio.samples.size() * sizeof(float), v);
}

std::uint64_t settings_hash(const AnalysisSettings& s) {
    Hasher h;
    h.add(kPipelineVersion);
    h.add(s.channel);
    h.add(s.stft.fft_size);
    h.add(s.stft.effective_hop());
    h.add(static_cast<int>(s.stft.window));
    h.add(s.bands.size());
    for (const Band& b : s.bands) {
        h.add(std::string_view(b.name));
        h.add(b.low_hz);
        h.add(b.high_hz);
    }
    h.add(s.envelope.attack_ms);
    h.add(s.envelope.release_ms);
    h.add(s.envelope.hold_ms);
    h.add(s.onsets.threshold);
    h.add(s.onsets.delta);
    h.add(s.onsets.window_s);
    h.add(s.onsets.min_gap_s);
    h.add(s.onsets.min_scale);
    h.add(s.beats.min_bpm);
    h.add(s.beats.max_bpm);
    h.add(s.beats.prior_bpm);
    h.add(s.beats.prior_octaves);
    h.add(s.beats.tightness);
    h.add(s.beats.beats_per_bar);
    h.add(s.signal_rate_hz);
    return h.value();
}

} // namespace ztrip::analysis
