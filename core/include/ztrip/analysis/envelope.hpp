#pragma once

#include <cmath>
#include <span>
#include <vector>

namespace ztrip::analysis {

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

// The follower with hold, one sample at a time at a fixed rate, from 0.
class EnvelopeFollower {
public:
    EnvelopeFollower(double rate_hz, const EnvelopeSettings& settings);
    float next(float in);
    void reset() { env_ = 0.0f; hold_ = 0; }

private:
    float dt_, attack_, release_;
    int hold_samples_;
    float env_ = 0.0f;
    int hold_ = 0;
};

// Runs the follower over a signal sampled at rate_hz, starting from 0.
std::vector<float> follow_envelope(std::span<const float> in, double rate_hz,
                                   const EnvelopeSettings& settings);

// Linearly resamples a signal whose sample i sits at time i / src_rate onto
// a grid of dst_count samples at dst_rate. Holds the end values outside.
std::vector<float> resample_linear(std::span<const float> values, double src_rate,
                                   double dst_rate, std::size_t dst_count);

} // namespace ztrip::analysis
