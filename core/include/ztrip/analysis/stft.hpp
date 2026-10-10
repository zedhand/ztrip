#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace ztrip::analysis {

enum class Window { Hann, Hamming, BlackmanHarris };

struct StftSettings {
    int fft_size = 2048;   // power of two, 512..8192
    int hop = 0;           // 0 means fft_size / 4
    Window window = Window::Hann;

    int effective_hop() const noexcept { return hop > 0 ? hop : fft_size / 4; }
};

// Magnitude spectrogram. Frame i is centred on sample i * hop (the signal is
// zero-padded by fft_size / 2 on both sides), so frame_time(i) = i * hop / sr.
struct Spectrogram {
    int sample_rate = 0;
    int fft_size = 0;
    int hop = 0;
    std::size_t frames = 0;
    std::size_t bins = 0; // fft_size / 2 + 1
    std::vector<float> magnitude; // frames * bins, row-major by frame

    double frame_rate() const noexcept { return static_cast<double>(sample_rate) / hop; }
    double frame_time(std::size_t i) const noexcept {
        return static_cast<double>(i) * hop / sample_rate;
    }
    double bin_hz(std::size_t bin) const noexcept {
        return static_cast<double>(bin) * sample_rate / fft_size;
    }
    std::span<const float> frame(std::size_t i) const noexcept {
        return {magnitude.data() + i * bins, bins};
    }
};

// Window coefficients of length n (periodic form, as used for spectral analysis).
std::vector<float> make_window(Window window, int n);

// Magnitude spectrum of one frame at a time, scaled so a full-scale sine reads
// about 1.0 at its bin whatever the window or size. compute_stft runs one over
// a whole signal; the live analyser runs one per hop as audio arrives.
class FrameTransform {
public:
    // Throws std::invalid_argument for an unsupported fft_size.
    FrameTransform(int fft_size, Window window);
    ~FrameTransform();
    FrameTransform(FrameTransform&&) noexcept;
    FrameTransform& operator=(FrameTransform&&) noexcept;

    int fft_size() const noexcept { return n_; }
    std::size_t bins() const noexcept { return static_cast<std::size_t>(n_) / 2 + 1; }

    // Windows `samples` (fft_size values, or fewer, zero-padded at the end)
    // and writes bins() magnitudes to `magnitude`.
    void magnitude(std::span<const float> samples, std::span<float> magnitude);

private:
    struct Impl;
    int n_ = 0;
    std::unique_ptr<Impl> impl_;
};

// Throws std::invalid_argument for unsupported sizes or an empty signal.
Spectrogram compute_stft(std::span<const float> mono, int sample_rate, const StftSettings& settings);

} // namespace ztrip::analysis
