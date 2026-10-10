#pragma once
// Deterministic test signals: drum-like hits at known times.

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace mre::test {

class Noise {
public:
    explicit Noise(std::uint32_t seed) : state_(seed) {}
    float next() { // uniform in [-1, 1)
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<float>(state_ >> 8) / 8388608.0f - 1.0f;
    }

private:
    std::uint32_t state_;
};

inline void add_kick(std::vector<float>& x, int sr, double t, float gain = 0.8f) {
    const auto start = static_cast<std::size_t>(std::lround(t * sr));
    double phase = 0.0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(0.35 * sr) && start + i < x.size(); ++i) {
        const double s = static_cast<double>(i) / sr;
        const double f = 45.0 + 110.0 * std::exp(-s / 0.035);
        phase += 2.0 * std::numbers::pi * f / sr;
        x[start + i] += gain * static_cast<float>(std::sin(phase) * std::exp(-s / 0.15));
    }
}

inline void add_snare(std::vector<float>& x, int sr, double t, Noise& noise, float gain = 0.5f) {
    const auto start = static_cast<std::size_t>(std::lround(t * sr));
    float prev = 0.0f;
    for (std::size_t i = 0; i < static_cast<std::size_t>(0.2 * sr) && start + i < x.size(); ++i) {
        const double s = static_cast<double>(i) / sr;
        const float n = noise.next();
        const float hp = n - 0.9f * prev; // tilt towards highs
        prev = n;
        x[start + i] += gain * static_cast<float>(
            (0.4 * std::sin(2.0 * std::numbers::pi * 190.0 * s) + 0.6 * hp) * std::exp(-s / 0.06));
    }
}

inline void add_hat(std::vector<float>& x, int sr, double t, Noise& noise, float gain = 0.2f) {
    const auto start = static_cast<std::size_t>(std::lround(t * sr));
    float prev = 0.0f;
    for (std::size_t i = 0; i < static_cast<std::size_t>(0.05 * sr) && start + i < x.size(); ++i) {
        const double s = static_cast<double>(i) / sr;
        const float n = noise.next();
        x[start + i] += gain * (n - prev) * static_cast<float>(std::exp(-s / 0.01));
        prev = n;
    }
}

// A 4/4 groove: kick on every beat, snare on 2 and 4, hats on eighths.
// `first_beat` delays the pattern; returns the signal.
inline std::vector<float> groove(int sr, double bpm, int bars, double first_beat = 0.0,
                                 bool backbeat = true, bool hats = true) {
    const double spb = 60.0 / bpm;
    std::vector<float> x(static_cast<std::size_t>((first_beat + bars * 4 * spb + 1.0) * sr), 0.0f);
    Noise noise(1234);
    for (int b = 0; b < bars * 4; ++b) {
        const double t = first_beat + b * spb;
        add_kick(x, sr, t, b % 4 == 0 ? 0.9f : 0.6f);
        if (backbeat && b % 2 == 1) add_snare(x, sr, t, noise);
        if (hats) {
            add_hat(x, sr, t, noise);
            add_hat(x, sr, t + spb / 2, noise);
        }
    }
    return x;
}

} // namespace mre::test
