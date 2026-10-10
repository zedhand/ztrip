#pragma once

#include "mre/analysis/bands.hpp"
#include "mre/analysis/beats.hpp"
#include "mre/analysis/envelope.hpp"
#include "mre/analysis/onsets.hpp"
#include "mre/analysis/stft.hpp"
#include "mre/io/wav.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mre::analysis {

struct AnalysisSettings {
    int channel = -1; // -1 = mono sum, otherwise a single channel
    StftSettings stft;
    std::vector<Band> bands = default_bands();
    EnvelopeSettings envelope;
    OnsetSettings onsets;
    BeatSettings beats;
    double signal_rate_hz = 120.0;
};

// A Continuous signal sampled at the result's signal_rate_hz, nominally 0..1.
struct SignalBuffer {
    std::string name;
    std::vector<float> values;
};

// A Trigger signal: timestamped events with velocity.
struct EventList {
    std::string name;
    std::vector<Trigger> events;
};

// Offline analysis output. Signals are named:
//   band.<Band>  band energy, dB normalised to 0..1 by whole-file percentiles
//   env.<Band>   band energy after the attack/release envelope follower
//   rms          loudness, dB normalised to 0..1
//   centroid     spectral centroid, log-frequency 20 Hz..20 kHz mapped to 0..1
// Events are onset.<Band>, onset.full, beat and downbeat.
struct AnalysisResult {
    double signal_rate_hz = 120.0;
    double duration_s = 0.0;
    std::vector<SignalBuffer> signals;
    std::vector<EventList> events;
    BeatGrid beats;

    const SignalBuffer* find_signal(std::string_view name) const noexcept;
    const EventList* find_events(std::string_view name) const noexcept;

    // Linear interpolation of a signal at time t (clamped to the buffer), so
    // evaluation at any time is a pure lookup.
    float sample(const SignalBuffer& signal, double t) const noexcept;
};

AnalysisResult analyze(std::span<const float> mono, int sample_rate, const AnalysisSettings& settings);
AnalysisResult analyze(const io::AudioBuffer& audio, const AnalysisSettings& settings);

// 64-bit FNV-1a, the hash used for sigcache keys.
std::uint64_t fnv1a(const void* data, std::size_t size, std::uint64_t seed = 0xcbf29ce484222325ull);

// Hash of the decoded audio (sample rate, channel count and sample bits).
std::uint64_t audio_hash(const io::AudioBuffer& audio);

// Hash of every setting that affects the analysis output.
std::uint64_t settings_hash(const AnalysisSettings& settings);

} // namespace mre::analysis
