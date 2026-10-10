#pragma once

#include "ztrip/analysis/onsets.hpp"
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
    // With drum hits (kick and snare events) given to analyze_beats:
    float drum_weight = 0.25f;        // weight of the drum-hit curve added to the onset strength for tracking
    // A related tempo (x1/2, x2/3, x3/2, x2) replaces the estimate only when
    // its beats fit the drum hits with at least min_drum_fit (F), at least
    // min_drum_precision of its beats land on a hit, and its fit beats the
    // estimate's by drum_fit_margin.
    float min_drum_fit = 0.5f;
    float min_drum_precision = 0.55f;
    float drum_fit_margin = 0.1f;
};

struct BeatGrid {
    double bpm = 0.0;            // from the median inter-beat interval
    int beats_per_bar = 4;
    int downbeat_phase = 0;      // index of the first downbeat in `beats`
    std::vector<double> beats;   // seconds
    std::vector<double> downbeats;
};

// Global tempo estimate from the autocorrelation of an onset-strength curve
// sampled at frame_rate: each tempo scores the autocorrelation at its beat
// period plus at half that period (the subdivision), weighted by the tempo
// prior. Returns BPM, or 0 if the curve is too short or silent.
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

// How well a beat grid matches drum hits: precision is the share of beats
// with a hit within the tolerance (min(50 ms, period / 6)), recall the share
// of hit velocity that lands on a beat, f their harmonic mean.
struct DrumFit {
    double f = 0.0;
    double precision = 0.0;
    double recall = 0.0;
};
DrumFit drum_fit(std::span<const double> beats, std::span<const Trigger> hits);

// Full offline beat grid: tempo, beats and downbeats. With drum hits (kicks
// and snares together), the tempo estimate is checked against its related
// tempos by drum_fit, and tracking listens to the hits as well as the onset
// strength, so fast metal is not read at half speed and four-on-the-floor
// tracks don't settle on the off-beat hats and bass.
BeatGrid analyze_beats(const Spectrogram& spec, std::span<const float> onset_strength,
                       const BeatSettings& settings, std::span<const Trigger> drum_hits = {});

} // namespace ztrip::analysis
