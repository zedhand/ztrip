#pragma once
// Bake to keyframes: evaluate a graph offline at a frame rate and turn each
// target's values into keys an engine's own animation system plays back, so
// the result works without ztrip installed.

#include "ztrip/graph/evaluator.hpp"

#include <span>
#include <string>
#include <vector>

namespace ztrip::host {

struct BakeKey {
    double time = 0.0; // seconds of song time
    double value = 0.0;
};

struct BakeSettings {
    double start_s = 0.0;
    double end_s = -1.0;   // negative: the song's end (the duration passed to bake())
    double fps = 30.0;     // one sample per frame, frame 0 at start_s
    // Keys are dropped while linear interpolation between the kept ones
    // stays within this much of every frame's value. 0 keeps every key that
    // isn't on a straight line (to rounding).
    double tolerance = 0.0;
    std::vector<std::string> targets; // empty: every target of the graph
};

struct BakedCurve {
    std::string target;
    std::vector<BakeKey> keys; // linear interpolation between keys
    std::size_t frames = 0;    // frames sampled before reduction
};

// Samples every frame from start_s to end_s inclusive (offline evaluation
// is deterministic, so this matches what playback shows at those times)
// and reduces each curve with reduce_keys(). The evaluator is left at the
// last frame. Targets not in the graph come back with no keys.
std::vector<BakedCurve> bake(graph::Evaluator& evaluator, double duration_s, const BakeSettings& settings);

// Drops keys while linear interpolation between the kept neighbours stays
// within `tolerance` of every dropped key's value (Douglas-Peucker on the
// value axis). The first and last keys are always kept.
std::vector<BakeKey> reduce_keys(std::span<const BakeKey> keys, double tolerance);

// Largest difference between `keys` (linearly interpolated, held flat
// outside their range) and `samples`, for checking a reduction.
double max_error(std::span<const BakeKey> keys, std::span<const BakeKey> samples);

} // namespace ztrip::host
