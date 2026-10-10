#pragma once

#include <span>
#include <vector>

namespace mre::analysis {

// A timestamped event, e.g. an onset or a beat.
struct Trigger {
    double time = 0.0;     // seconds from file start
    float velocity = 0.0f; // 0..1
};

struct OnsetSettings {
    float threshold = 1.5f;  // multiple of the local median a peak must exceed
    float delta = 0.07f;     // absolute offset on the 0..1 normalised flux
    float window_s = 0.4f;   // width of the median window, centred
    float min_gap_s = 0.05f; // minimum time between onsets
    float min_scale = 0.05f; // floor on the normalising peak, in band_flux units
};

// Picks onsets from a flux curve sampled at frame_rate (frame i at time
// i / frame_rate). Flux is normalised by its 99.5th percentile peak (but at
// least min_scale, so near-silent bands don't turn noise into onsets); a frame
// is an onset when it is the local maximum within min_gap_s / 2 and exceeds
// delta + threshold * local median. Times are refined by parabolic
// interpolation; velocity is the normalised flux peak, clamped to 0..1.
std::vector<Trigger> pick_onsets(std::span<const float> flux, double frame_rate,
                                 const OnsetSettings& settings);

} // namespace mre::analysis
