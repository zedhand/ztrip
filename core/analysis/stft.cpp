#include "mre/analysis/stft.hpp"

#include <pffft.h>

#include <cmath>
#include <memory>
#include <numbers>
#include <stdexcept>

namespace mre::analysis {
namespace {

struct AlignedFree {
    void operator()(float* p) const noexcept { pffft_aligned_free(p); }
};
using AlignedFloats = std::unique_ptr<float, AlignedFree>;

AlignedFloats aligned_floats(std::size_t n) {
    auto* p = static_cast<float*>(pffft_aligned_malloc(n * sizeof(float)));
    if (p == nullptr) throw std::bad_alloc();
    return AlignedFloats(p);
}

struct SetupFree {
    void operator()(PFFFT_Setup* s) const noexcept { pffft_destroy_setup(s); }
};

bool is_power_of_two(int n) {
    return n > 0 && (n & (n - 1)) == 0;
}

} // namespace

std::vector<float> make_window(Window window, int n) {
    std::vector<float> w(static_cast<std::size_t>(n));
    const double two_pi = 2.0 * std::numbers::pi;
    for (int i = 0; i < n; ++i) {
        const double x = two_pi * i / n;
        double v = 0.0;
        switch (window) {
        case Window::Hann: v = 0.5 - 0.5 * std::cos(x); break;
        case Window::Hamming: v = 0.54 - 0.46 * std::cos(x); break;
        case Window::BlackmanHarris:
            v = 0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2 * x) -
                0.01168 * std::cos(3 * x);
            break;
        }
        w[static_cast<std::size_t>(i)] = static_cast<float>(v);
    }
    return w;
}

Spectrogram compute_stft(std::span<const float> mono, int sample_rate,
                         const StftSettings& settings) {
    const int n = settings.fft_size;
    const int hop = settings.effective_hop();
    if (!is_power_of_two(n) || n < 512 || n > 8192) {
        throw std::invalid_argument("stft: fft_size must be a power of two in 512..8192");
    }
    if (hop <= 0 || hop > n) throw std::invalid_argument("stft: hop must be in 1..fft_size");
    if (sample_rate <= 0) throw std::invalid_argument("stft: invalid sample rate");
    if (mono.empty()) throw std::invalid_argument("stft: empty signal");

    const auto un = static_cast<std::size_t>(n);
    const auto uhop = static_cast<std::size_t>(hop);
    Spectrogram spec;
    spec.sample_rate = sample_rate;
    spec.fft_size = n;
    spec.hop = hop;
    spec.bins = un / 2 + 1;
    spec.frames = mono.size() / uhop + 1;
    spec.magnitude.resize(spec.frames * spec.bins);

    std::unique_ptr<PFFFT_Setup, SetupFree> setup(pffft_new_setup(n, PFFFT_REAL));
    if (!setup) throw std::runtime_error("stft: pffft setup failed");
    auto in = aligned_floats(un);
    auto out = aligned_floats(un);
    auto work = aligned_floats(un);
    const std::vector<float> window = make_window(settings.window, n);
    // Scale so a full-scale sine reads ~1.0 at its bin whatever the window or size.
    double window_sum = 0.0;
    for (float w : window) window_sum += w;
    const auto scale = static_cast<float>(2.0 / window_sum);

    const auto half = static_cast<std::ptrdiff_t>(un / 2);
    const auto len = static_cast<std::ptrdiff_t>(mono.size());
    for (std::size_t f = 0; f < spec.frames; ++f) {
        const std::ptrdiff_t start = static_cast<std::ptrdiff_t>(f * uhop) - half;
        for (std::size_t i = 0; i < un; ++i) {
            const std::ptrdiff_t s = start + static_cast<std::ptrdiff_t>(i);
            const float x = (s >= 0 && s < len) ? mono[static_cast<std::size_t>(s)] : 0.0f;
            in.get()[i] = x * window[i];
        }
        pffft_transform_ordered(setup.get(), in.get(), out.get(), work.get(), PFFFT_FORWARD);

        // Ordered real output: [DC, Nyquist, re1, im1, re2, im2, ...].
        const float* o = out.get();
        float* mag = spec.magnitude.data() + f * spec.bins;
        mag[0] = std::fabs(o[0]) * scale;
        mag[spec.bins - 1] = std::fabs(o[1]) * scale;
        for (std::size_t k = 1; k < spec.bins - 1; ++k) {
            mag[k] = std::hypot(o[2 * k], o[2 * k + 1]) * scale;
        }
    }
    return spec;
}

} // namespace mre::analysis
