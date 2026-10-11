// Synthesises a 120 BPM drum loop, then runs it and a graph through the
// staged plugin core (see entry.cpp).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

extern "C" int ztrip_unreal_check(const char* wav, const char* graph, const char* cache);

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: ztrip_unreal_check <graph.mgraph.json>\n");
        return 2;
    }
    const int sr = 44100;
    const double spb = 0.5;
    std::vector<float> x(static_cast<std::size_t>(17 * sr), 0.0f);
    std::uint32_t noise = 1234;
    for (int b = 0; b < 32; ++b) {
        const auto start = static_cast<std::size_t>((0.5 + b * spb) * sr);
        double phase = 0.0;
        for (std::size_t i = 0; i < static_cast<std::size_t>(0.35 * sr) && start + i < x.size(); ++i) {
            const double s = static_cast<double>(i) / sr;
            phase += 2.0 * 3.14159265358979 * (45.0 + 110.0 * std::exp(-s / 0.035)) / sr;
            x[start + i] += 0.8f * static_cast<float>(std::sin(phase) * std::exp(-s / 0.15));
        }
        if (b % 2 == 1) {
            for (std::size_t i = 0; i < static_cast<std::size_t>(0.2 * sr) && start + i < x.size(); ++i) {
                noise = noise * 1664525u + 1013904223u;
                const float n = static_cast<float>(noise >> 8) / 8388608.0f - 1.0f;
                x[start + i] += 0.4f * n * static_cast<float>(std::exp(-static_cast<double>(i) / sr / 0.06));
            }
        }
    }
    const auto dir = std::filesystem::temp_directory_path() / "ztrip_unreal_check";
    std::filesystem::create_directories(dir);
    const auto wav = dir / "loop.wav";
    std::ofstream out(wav, std::ios::binary);
    const auto put = [&](std::uint32_t v, int n) {
        for (int i = 0; i < n; ++i) out.put(static_cast<char>((v >> (8 * i)) & 0xff));
    };
    const auto data = static_cast<std::uint32_t>(x.size() * 2);
    out.write("RIFF", 4);
    put(36 + data, 4);
    out.write("WAVEfmt ", 8);
    put(16, 4);
    put(1, 2);
    put(1, 2);
    put(sr, 4);
    put(sr * 2, 4);
    put(2, 2);
    put(16, 2);
    out.write("data", 4);
    put(data, 4);
    for (float s : x) {
        const float c = s > 1.0f ? 1.0f : (s < -1.0f ? -1.0f : s);
        put(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(c * 32767.0f))), 2);
    }
    out.close();
    const int result = ztrip_unreal_check(wav.string().c_str(), argv[1], (dir / "cache").string().c_str());
    std::printf(result == 0 ? "ok\n" : "FAILED\n");
    return result;
}
