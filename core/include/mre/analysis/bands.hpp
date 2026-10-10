#pragma once

#include "mre/analysis/stft.hpp"

#include <span>
#include <string>
#include <vector>

namespace mre::analysis {

struct Band {
    std::string name;
    float low_hz = 0.0f;
    float high_hz = 0.0f;
};

// Sub 20-60, Bass 60-250, LowMid 250-500, Mid 500-2k, HighMid 2k-6k, Air 6k-20k Hz.
std::vector<Band> default_bands();

// Per-frame band energy in dB (power summed over bins in [low_hz, high_hz)).
std::vector<float> band_energy_db(const Spectrogram& spec, const Band& band);

// Per-frame spectral flux in the band: half-wave rectified increase of
// log-compressed magnitude over the previous frame (max-filtered across
// neighbouring bins), averaged over the band's bins. The signal is taken as
// silent before frame 0, so sound at the very start counts as an onset.
std::vector<float> band_flux(const Spectrogram& spec, const Band& band);

// Maps values to 0..1 using whole-file percentiles: lo_pct -> 0, hi_pct -> 1,
// clamped. If floor_db > 0 the low anchor is raised to at least
// (hi anchor - floor_db), so long silences don't flatten the range.
std::vector<float> normalize_percentile(std::span<const float> values, float lo_pct, float hi_pct,
                                        float floor_db = 0.0f);

// Value at percentile p (0..100) by linear interpolation; 0 for empty input.
float percentile(std::span<const float> values, float p);

// Frame-weighted spectral centroid in Hz.
std::vector<float> spectral_centroid(const Spectrogram& spec);

} // namespace mre::analysis
