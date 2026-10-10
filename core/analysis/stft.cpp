#include "ztrip/analysis/stft.hpp"

#include <pffft.h>

#include <cmath>
#include <memory>
#include <numbers>
#include <stdexcept>

namespace ztrip::analysis {
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

struct FrameTransform::Impl {
    std::unique_ptr<PFFFT_Setup, SetupFree> setup;
    AlignedFloats in, out, work;
    std::vector<float> window;
    float scale = 1.0f;
};

FrameTransform::FrameTransform(int fft_size, Window window) : n_(fft_size) {
    if (!is_power_of_two(n_) || n_ < 512 || n_ > 8192) {
        throw std::invalid_argument("stft: fft_size must be a power of two in 512..8192");
    }
    const auto un = static_cast<std::size_t>(n_);
    impl_ = std::make_unique<Impl>();
    impl_->setup.reset(pffft_new_setup(n_, PFFFT_REAL));
    if (!impl_->setup) throw std::runtime_error("stft: pffft setup failed");
    impl_->in = aligned_floats(un);
    impl_->out = aligned_floats(un);
    impl_->work = aligned_floats(un);
    impl_->window = make_window(window, n_);
    double window_sum = 0.0;
    for (float w : impl_->window) window_sum += w;
    impl_->scale = static_cast<float>(2.0 / window_sum);
}

FrameTransform::~FrameTransform() = default;
FrameTransform::FrameTransform(FrameTransform&&) noexcept = default;
FrameTransform& FrameTransform::operator=(FrameTransform&&) noexcept = default;

void FrameTransform::magnitude(std::span<const float> samples, std::span<float> mag) {
    const auto un = static_cast<std::size_t>(n_);
    const std::size_t bins = un / 2 + 1;
    float* in = impl_->in.get();
    for (std::size_t i = 0; i < un; ++i) {
        in[i] = i < samples.size() ? samples[i] * impl_->window[i] : 0.0f;
    }
    pffft_transform_ordered(impl_->setup.get(), in, impl_->out.get(), impl_->work.get(), PFFFT_FORWARD);

    // Ordered real output: [DC, Nyquist, re1, im1, re2, im2, ...].
    const float* o = impl_->out.get();
    const float scale = impl_->scale;
    mag[0] = std::fabs(o[0]) * scale;
    mag[bins - 1] = std::fabs(o[1]) * scale;
    for (std::size_t k = 1; k < bins - 1; ++k) {
        mag[k] = std::hypot(o[2 * k], o[2 * k + 1]) * scale;
    }
}

Spectrogram compute_stft(std::span<const float> mono, int sample_rate,
                         const StftSettings& settings) {
    const int n = settings.fft_size;
    const int hop = settings.effective_hop();
    FrameTransform transform(n, settings.window);
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

    std::vector<float> frame(un);
    const auto half = static_cast<std::ptrdiff_t>(un / 2);
    const auto len = static_cast<std::ptrdiff_t>(mono.size());
    for (std::size_t f = 0; f < spec.frames; ++f) {
        const std::ptrdiff_t start = static_cast<std::ptrdiff_t>(f * uhop) - half;
        for (std::size_t i = 0; i < un; ++i) {
            const std::ptrdiff_t s = start + static_cast<std::ptrdiff_t>(i);
            frame[i] = (s >= 0 && s < len) ? mono[static_cast<std::size_t>(s)] : 0.0f;
        }
        transform.magnitude(frame, std::span<float>(spec.magnitude.data() + f * spec.bins, spec.bins));
    }
    return spec;
}

} // namespace ztrip::analysis
