#pragma once

#include <cmath>
#include <span>
#include <vector>

namespace mre::analysis {

struct EnvelopeSettings {
    float attack_ms = 5.0f;
    float release_ms = 150.0f;
    float hold_ms = 0.0f; // time to hold a peak before releasing
};

// One-pole follower with separate attack and release time constants (seconds).
inline float follow(float in, float& env, float attack, float release, float dt) {
    const float coeff = (in > env) ? attack : release;
    if (coeff <= 0.0f) {
        env = in;
    } else {
        env += (in - env) * (1.0f - std::exp(-dt / coeff));
    }
    return env;
}

// Runs the follower over a signal sampled at rate_hz, starting from 0.
std::vector<float> follow_envelope(std::span<const float> in, double rate_hz,
                                   const EnvelopeSettings& settings);

// Linearly resamples a signal whose sample i sits at time i / src_rate onto
// a grid of dst_count samples at dst_rate. Holds the end values outside.
std::vector<float> resample_linear(std::span<const float> values, double src_rate,
                                   double dst_rate, std::size_t dst_count);

} // namespace mre::analysis
