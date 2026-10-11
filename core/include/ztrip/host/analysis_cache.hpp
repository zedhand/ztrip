#pragma once
// Song analysis for hosts: WAV bytes in, analysis out, through an on-disk
// .sigcache so a song is only analysed once.

#include "ztrip/analysis/offline.hpp"
#include "ztrip/time/autodraft.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ztrip::host {

struct AnalysisOutcome {
    std::shared_ptr<const analysis::AnalysisResult> result; // null on failure
    bool from_cache = false;
    std::string error; // "<label>: <reason>" on failure
};

// Decodes WAV bytes and analyses them with the default settings, reusing
// cache_dir/<audio hash>.sigcache when the audio and settings match and
// writing it otherwise. An empty cache_dir skips the cache. Thread-safe, and
// never throws: failures come back in `error`, prefixed with `label`.
AnalysisOutcome analyze_wav(std::span<const std::uint8_t> wav, const std::filesystem::path& cache_dir,
                            std::string_view label);

// The lanes hosts draft from a song: Kick from the kick detector (Sub band
// onsets without it), Snare and Hats from the Mid and Air band onsets.
std::vector<time::DraftLaneSpec> default_draft_lanes();

// UTF-8 <-> filesystem paths, whatever the platform's native encoding (song
// names are often outside the Windows code page).
std::filesystem::path path_from_utf8(std::string_view utf8);
std::string path_to_utf8(const std::filesystem::path& path);

} // namespace ztrip::host
