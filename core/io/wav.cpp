#include "mre/io/wav.hpp"

#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace mre::io {
namespace {

constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatFloat = 3;
constexpr std::uint16_t kFormatExtensible = 0xFFFE;

std::uint16_t read_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t read_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error("wav: " + what);
}

float decode_sample(const std::uint8_t* p, std::uint16_t format, int bits) {
    if (format == kFormatFloat) {
        if (bits == 32) {
            const std::uint32_t u = read_u32(p);
            float f;
            std::memcpy(&f, &u, sizeof f);
            return f;
        }
        const std::uint64_t u = static_cast<std::uint64_t>(read_u32(p)) |
                                (static_cast<std::uint64_t>(read_u32(p + 4)) << 32);
        double d;
        std::memcpy(&d, &u, sizeof d);
        return static_cast<float>(d);
    }
    switch (bits) {
    case 8: // unsigned, offset by 128
        return (static_cast<float>(p[0]) - 128.0f) / 128.0f;
    case 16:
        return static_cast<float>(static_cast<std::int16_t>(read_u16(p))) / 32768.0f;
    case 24: {
        std::int32_t v = static_cast<std::int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
        if (v & 0x800000) v -= 0x1000000;
        return static_cast<float>(v) / 8388608.0f;
    }
    default: // 32
        return static_cast<float>(static_cast<double>(static_cast<std::int32_t>(read_u32(p))) /
                                  2147483648.0);
    }
}

} // namespace

AudioBuffer parse_wav(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        fail("not a RIFF/WAVE file");
    }

    std::uint16_t format = 0;
    std::uint16_t channels = 0;
    std::uint32_t sample_rate = 0;
    std::uint16_t bits = 0;
    std::uint16_t block_align = 0;
    const std::uint8_t* data = nullptr;
    std::size_t data_size = 0;

    std::size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        const std::uint8_t* chunk = bytes.data() + pos;
        const std::size_t size = read_u32(chunk + 4);
        const std::size_t body = pos + 8;
        const std::size_t available = bytes.size() - body;
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (size < 16 || size > available) fail("truncated fmt chunk");
            const std::uint8_t* f = bytes.data() + body;
            format = read_u16(f);
            channels = read_u16(f + 2);
            sample_rate = read_u32(f + 4);
            block_align = read_u16(f + 12);
            bits = read_u16(f + 14);
            if (format == kFormatExtensible) {
                if (size < 40) fail("truncated extensible fmt chunk");
                format = read_u16(f + 24); // first two bytes of the subformat GUID
            }
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            data = bytes.data() + body;
            // Some writers leave the size at 0 or 0xFFFFFFFF when streaming.
            data_size = (size == 0 || size > available) ? available : size;
            break;
        }
        pos = body + size + (size & 1); // chunks are word aligned
    }

    if (channels == 0 || sample_rate == 0) fail("missing or invalid fmt chunk");
    if (data == nullptr) fail("missing data chunk");
    const bool pcm_ok = format == kFormatPcm && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    const bool float_ok = format == kFormatFloat && (bits == 32 || bits == 64);
    if (!pcm_ok && !float_ok) {
        fail("unsupported format " + std::to_string(format) + " with " + std::to_string(bits) +
             " bits");
    }
    const std::size_t bytes_per_sample = bits / 8u;
    if (block_align != channels * bytes_per_sample) fail("inconsistent block alignment");

    AudioBuffer out;
    out.sample_rate = static_cast<int>(sample_rate);
    out.channels = channels;
    const std::size_t frames = data_size / block_align;
    out.samples.resize(frames * channels);
    for (std::size_t i = 0; i < out.samples.size(); ++i) {
        out.samples[i] = decode_sample(data + i * bytes_per_sample, format, bits);
    }
    return out;
}

AudioBuffer read_wav(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("cannot open " + path.string());
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                          std::istreambuf_iterator<char>());
    return parse_wav(bytes);
}

std::vector<float> to_mono(const AudioBuffer& audio, int channel) {
    const std::size_t frames = audio.frames();
    const auto chans = static_cast<std::size_t>(audio.channels);
    std::vector<float> mono(frames);
    if (channel >= 0) {
        if (channel >= audio.channels) throw std::out_of_range("to_mono: channel out of range");
        for (std::size_t i = 0; i < frames; ++i) {
            mono[i] = audio.samples[i * chans + static_cast<std::size_t>(channel)];
        }
        return mono;
    }
    const float scale = 1.0f / static_cast<float>(chans);
    for (std::size_t i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (std::size_t c = 0; c < chans; ++c) sum += audio.samples[i * chans + c];
        mono[i] = sum * scale;
    }
    return mono;
}

} // namespace mre::io
