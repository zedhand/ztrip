#include "ztrip/io/wav.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstring>
#include <stdexcept>

using ztrip::io::parse_wav;

namespace {

// Appends via insert rather than push_back, which trips a GCC 13
// -Wstringop-overflow false positive when inlined.
void put16(std::vector<std::uint8_t>& b, std::uint16_t v) {
    const std::uint8_t bytes[2] = {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8)};
    b.insert(b.end(), bytes, bytes + 2);
}
void put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    const std::uint8_t bytes[4] = {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8),
                                   static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 24)};
    b.insert(b.end(), bytes, bytes + 4);
}
void tag(std::vector<std::uint8_t>& b, const char* t) {
    b.insert(b.end(), t, t + 4);
}

std::vector<std::uint8_t> make_wav(std::uint16_t format, std::uint16_t channels, std::uint16_t bits,
                                   const std::vector<std::uint8_t>& data, bool extensible = false,
                                   bool extra_chunk = false) {
    std::vector<std::uint8_t> fmt;
    put16(fmt, extensible ? 0xFFFE : format);
    put16(fmt, channels);
    put32(fmt, 48000);
    put32(fmt, 48000u * channels * bits / 8);
    put16(fmt, static_cast<std::uint16_t>(channels * bits / 8));
    put16(fmt, bits);
    if (extensible) {
        put16(fmt, 22);
        put16(fmt, bits);
        put32(fmt, 0);
        put16(fmt, format); // GUID starts with the format code
        const std::uint8_t rest[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00,
                                       0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
        fmt.insert(fmt.end(), rest, rest + 14);
    }
    std::vector<std::uint8_t> body;
    tag(body, "WAVE");
    tag(body, "fmt ");
    put32(body, static_cast<std::uint32_t>(fmt.size()));
    body.insert(body.end(), fmt.begin(), fmt.end());
    if (extra_chunk) {
        tag(body, "LIST");
        put32(body, 3);
        body.insert(body.end(), {1, 2, 3, 0}); // odd size plus pad byte
    }
    tag(body, "data");
    put32(body, static_cast<std::uint32_t>(data.size()));
    body.insert(body.end(), data.begin(), data.end());
    std::vector<std::uint8_t> out;
    tag(out, "RIFF");
    put32(out, static_cast<std::uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

} // namespace

TEST_CASE("16-bit stereo PCM decodes and mixes to mono", "[wav]") {
    std::vector<std::uint8_t> data;
    put16(data, 16384);                              // L = 0.5
    put16(data, static_cast<std::uint16_t>(-32768)); // R = -1
    put16(data, 0);
    put16(data, 16384);
    const auto audio = parse_wav(make_wav(1, 2, 16, data, false, true));
    REQUIRE(audio.sample_rate == 48000);
    REQUIRE(audio.channels == 2);
    REQUIRE(audio.frames() == 2);
    CHECK(audio.samples[0] == Catch::Approx(0.5f));
    CHECK(audio.samples[1] == Catch::Approx(-1.0f));

    const auto mono = ztrip::io::to_mono(audio);
    CHECK(mono[0] == Catch::Approx(-0.25f));
    CHECK(mono[1] == Catch::Approx(0.25f));
    const auto right = ztrip::io::to_mono(audio, 1);
    CHECK(right[0] == Catch::Approx(-1.0f));
    CHECK_THROWS_AS(ztrip::io::to_mono(audio, 2), std::out_of_range);
}

TEST_CASE("24-bit and 32-bit PCM keep sign and scale", "[wav]") {
    const std::vector<std::uint8_t> d24 = {0x00, 0x00, 0x40, 0x00, 0x00, 0xC0}; // +0.5, -0.5
    const auto a24 = parse_wav(make_wav(1, 1, 24, d24, true));
    REQUIRE(a24.frames() == 2);
    CHECK(a24.samples[0] == Catch::Approx(0.5f));
    CHECK(a24.samples[1] == Catch::Approx(-0.5f));

    std::vector<std::uint8_t> d32;
    put32(d32, 0x40000000u);
    put32(d32, 0xC0000000u);
    const auto a32 = parse_wav(make_wav(1, 1, 32, d32));
    CHECK(a32.samples[0] == Catch::Approx(0.5f));
    CHECK(a32.samples[1] == Catch::Approx(-0.5f));
}

TEST_CASE("32-bit float decodes", "[wav]") {
    std::vector<std::uint8_t> data;
    const float values[2] = {0.25f, -0.75f};
    for (float v : values) {
        std::uint32_t u;
        std::memcpy(&u, &v, 4);
        put32(data, u);
    }
    const auto audio = parse_wav(make_wav(3, 1, 32, data));
    CHECK(audio.samples[0] == 0.25f);
    CHECK(audio.samples[1] == -0.75f);
}

TEST_CASE("malformed and unsupported files are rejected", "[wav]") {
    const std::vector<std::uint8_t> junk = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'A', 'V', 'I', ' '};
    CHECK_THROWS_AS(parse_wav(junk), std::runtime_error);
    CHECK_THROWS_AS(parse_wav(make_wav(2, 1, 4, {0, 0})), std::runtime_error); // ADPCM
    auto truncated = make_wav(1, 1, 16, {0, 0});
    truncated.resize(20);
    CHECK_THROWS_AS(parse_wav(truncated), std::runtime_error);
}

TEST_CASE("other audio formats are named in the error, with how to convert", "[wav]") {
    using Catch::Matchers::ContainsSubstring;
    const std::vector<std::uint8_t> mp3 = {'I', 'D', '3', 4, 0, 0, 0, 0, 0, 0, 0, 0};
    const std::vector<std::uint8_t> flac = {'f', 'L', 'a', 'C', 0, 0, 0, 34, 0, 0, 0, 0};
    CHECK_THROWS_WITH(parse_wav(mp3), ContainsSubstring("MP3") && ContainsSubstring("ffmpeg"));
    CHECK_THROWS_WITH(parse_wav(flac), ContainsSubstring("FLAC"));
    const std::vector<std::uint8_t> junk = {'n', 'o', 'p', 'e'};
    CHECK_THROWS_WITH(parse_wav(junk), ContainsSubstring("not a RIFF/WAVE file"));
}
