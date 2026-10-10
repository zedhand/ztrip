#pragma once

#include "ztrip/analysis/stft.hpp"

#include <span>
#include <vector>

namespace ztrip::analysis {

struct BeatSettings {
    float min_bpm = 50.0f;
    float max_bpm = 220.0f;
    float prior_bpm = 120.0f;    // centre of the log-normal tempo prior
    float prior_octaves = 0.5f;  // standard deviation of the prior, in octaves
    float tightness = 100.0f;    // how strongly beats are held to the tempo
    int beats_per_bar = 4;
};

struct BeatGrid {
    double bpm = 0.0;            // from the median inter-beat interval
    int beats_per_bar = 4;
    int downbeat_phase = 0;      // index of the first downbeat in `beats`
    std::vector<double> beats;   // seconds
    std::vector<double> downbeats;
};

// Global tempo estimate from the autocorrelation of an onset-strength curve
// sampled at frame_rate, weighted by the tempo prior. Returns BPM, or 0 if
// the curve is too short or silent.
double estimate_tempo(std::span<const float> onset_strength, double frame_rate,
                      const BeatSettings& settings);

// Offline beat tracker: dynamic programming over onset strength (Ellis 2007)
// at a fixed global tempo. Returns beat times in seconds; leading and
// trailing beats in near silence are trimmed.
std::vector<double> track_beats(std::span<const float> onset_strength, double frame_rate,
                                double bpm, const BeatSettings& settings);

// Chooses which beat phase (0..beats_per_bar-1) starts each bar, scoring each
// phase by low-band onsets (kick), harmonic change across the beat, and the
// absence of backbeat (snare) onsets.
int estimate_downbeat_phase(const Spectrogram& spec, std::span<const double> beats,
                            int beats_per_bar);

// Full offline beat grid: tempo, beats and downbeats.
BeatGrid analyze_beats(const Spectrogram& spec, std::span<const float> onset_strength,
                       const BeatSettings& settings);

} // namespace ztrip::analysis
