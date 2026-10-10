#include "mre/analysis/envelope.hpp"

#include <algorithm>

namespace mre::analysis {

std::vector<float> follow_envelope(std::span<const float> in, double rate_hz,
                                   const EnvelopeSettings& settings) {
    const auto dt = static_cast<float>(1.0 / rate_hz);
    const float attack = settings.attack_ms / 1000.0f;
    const float release = settings.release_ms / 1000.0f;
    const auto hold_samples = static_cast<int>(std::lround(settings.hold_ms / 1000.0 * rate_hz));

    std::vector<float> out(in.size());
    float env = 0.0f;
    int hold = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] >= env) {
            hold = hold_samples;
            follow(in[i], env, attack, release, dt);
        } else if (hold > 0) {
            --hold;
        } else {
            follow(in[i], env, attack, release, dt);
        }
        out[i] = env;
    }
    return out;
}

std::vector<float> resample_linear(std::span<const float> values, double src_rate,
                                   double dst_rate, std::size_t dst_count) {
    std::vector<float> out(dst_count, 0.0f);
    if (values.empty()) return out;
    const std::size_t last = values.size() - 1;
    for (std::size_t i = 0; i < dst_count; ++i) {
        const double pos = static_cast<double>(i) / dst_rate * src_rate;
        if (pos >= static_cast<double>(last)) {
            out[i] = values[last];
            continue;
        }
        const auto j = static_cast<std::size_t>(pos);
        const auto frac = static_cast<float>(pos - static_cast<double>(j));
        out[i] = values[j] + (values[j + 1] - values[j]) * frac;
    }
    return out;
}

} // namespace mre::analysis
