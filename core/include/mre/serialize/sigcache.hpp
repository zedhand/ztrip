#pragma once

#include "mre/analysis/offline.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace mre::serialize {

// Identifies the analysis a cache holds; a cache is only reused when both match.
struct SigcacheKey {
    std::uint64_t audio_hash = 0;
    std::uint64_t settings_hash = 0;

    bool operator==(const SigcacheKey&) const = default;
};

// Binary .sigcache layout, all little-endian:
//   "MRESIGC\0", u32 format version, u64 audio hash, u64 settings hash,
//   f64 signal rate, f64 duration, f64 bpm, i32 beats per bar, i32 downbeat phase,
//   u32 signal count, then per signal: u32 name length, name, u64 count, f32[count]
//   u32 event list count, then per list: u32 name length, name, u64 count,
//     (f64 time, f32 velocity)[count]
//   u64 beat count, f64[count], u64 downbeat count, f64[count]
inline constexpr std::uint32_t kSigcacheVersion = 1;

std::vector<std::uint8_t> encode_sigcache(const SigcacheKey& key, const analysis::AnalysisResult& result);

// Returns nullopt when the bytes are malformed, from another format version,
// or were produced for a different key.
std::optional<analysis::AnalysisResult> decode_sigcache(std::span<const std::uint8_t> bytes,
                                                        const SigcacheKey& expected);

// Throws std::runtime_error if the file can't be written.
void write_sigcache(const std::filesystem::path& path, const SigcacheKey& key,
                    const analysis::AnalysisResult& result);

// Returns nullopt if the file is missing, malformed or stale.
std::optional<analysis::AnalysisResult> read_sigcache(const std::filesystem::path& path,
                                                      const SigcacheKey& expected);

} // namespace mre::serialize
