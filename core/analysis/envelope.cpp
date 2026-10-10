#include "ztrip/analysis/envelope.hpp"

#include <algorithm>

namespace ztrip::analysis {

EnvelopeFollower::EnvelopeFollower(double rate_hz, const EnvelopeSettings& settings)
    : dt_(static_cast<float>(1.0 / rate_hz)),
      attack_(settings.attack_ms / 1000.0f),
      release_(settings.release_ms / 1000.0f),
      hold_samples_(static_cast<int>(std::lround(settings.hold_ms / 1000.0 * rate_hz))) {}

float EnvelopeFollower::next(float in) {
    if (in >= env_) {
        hold_ = hold_samples_;
        follow(in, env_, attack_, release_, dt_);
    } else if (hold_ > 0) {
        --hold_;
    } else {
        follow(in, env_, attack_, release_, dt_);
    }
    return env_;
}

std::vector<float> follow_envelope(std::span<const float> in, double rate_hz,
                                   const EnvelopeSettings& settings) {
    EnvelopeFollower follower(rate_hz, settings);
    std::vector<float> out(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) out[i] = follower.next(in[i]);
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

} // namespace ztrip::analysis
