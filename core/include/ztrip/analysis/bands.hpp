#pragma once

#include "ztrip/analysis/stft.hpp"

#include <span>
#include <string>
#include <vector>

namespace ztrip::analysis {

struct Band {
    std::string name;
    float low_hz = 0.0f;
    float high_hz = 0.0f;
};

// The FFT bins a band covers, [first, last).
struct BinRange {
    std::size_t first = 0;
    std::size_t last = 0;
};
BinRange band_bins(int sample_rate, int fft_size, const Band& band);

// Band energy of one magnitude frame in dB (power summed over the bins).
float frame_energy_db(std::span<const float> magnitude, BinRange bins);

// Spectral centroid of one magnitude frame in Hz (0 when silent).
float frame_centroid_hz(std::span<const float> magnitude, int sample_rate, int fft_size);

// Spectral flux of one band, a frame at a time (see band_flux). Keeps the
// previous frame, so frames must arrive in order.
class BandFlux {
public:
    BandFlux(BinRange bins, std::size_t total_bins);
    float next(std::span<const float> magnitude);
    void reset();

private:
    BinRange r_;
    std::size_t lo_ = 0, hi_ = 0;
    std::vector<float> cur_, prev_;
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

} // namespace ztrip::analysis
