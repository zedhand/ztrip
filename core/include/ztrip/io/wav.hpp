#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace ztrip::io {

// Decoded PCM audio, samples interleaved and scaled to [-1, 1].
struct AudioBuffer {
    int sample_rate = 0;
    int channels = 0;
    std::vector<float> samples;

    std::size_t frames() const noexcept {
        return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0;
    }
    double duration_seconds() const noexcept {
        return sample_rate > 0 ? static_cast<double>(frames()) / sample_rate : 0.0;
    }
};

// Parses a RIFF/WAVE file held in memory. Supports integer PCM (8, 16, 24,
// 32 bit) and IEEE float (32, 64 bit), including WAVE_FORMAT_EXTENSIBLE.
// Throws std::runtime_error on malformed or unsupported input.
AudioBuffer parse_wav(std::span<const std::uint8_t> bytes);

// Reads and parses a WAV file. Throws std::runtime_error on failure.
AudioBuffer read_wav(const std::filesystem::path& path);

// Selects the analysis input: channel < 0 averages all channels to mono,
// otherwise that single channel is returned.
std::vector<float> to_mono(const AudioBuffer& audio, int channel = -1);

} // namespace ztrip::io
