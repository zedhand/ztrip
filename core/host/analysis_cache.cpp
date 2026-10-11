#include "ztrip/host/analysis_cache.hpp"

#include "ztrip/io/wav.hpp"
#include "ztrip/serialize/sigcache.hpp"

#include <cstdio>
#include <exception>
#include <system_error>

namespace ztrip::host {

AnalysisOutcome analyze_wav(std::span<const std::uint8_t> wav, const std::filesystem::path& cache_dir,
                            std::string_view label) {
    AnalysisOutcome out;
    try {
        const io::AudioBuffer audio = io::parse_wav(wav);
        const analysis::AnalysisSettings settings;
        const serialize::SigcacheKey key{analysis::audio_hash(audio), analysis::settings_hash(settings)};

        std::filesystem::path cache;
        if (!cache_dir.empty()) {
            char name[40];
            std::snprintf(name, sizeof name, "%016llx.sigcache", static_cast<unsigned long long>(key.audio_hash));
            cache = cache_dir / name;
            if (auto cached = serialize::read_sigcache(cache, key)) {
                out.result = std::make_shared<analysis::AnalysisResult>(std::move(*cached));
                out.from_cache = true;
                return out;
            }
        }
        out.result = std::make_shared<analysis::AnalysisResult>(analysis::analyze(audio, settings));
        if (!cache.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(cache_dir, ec);
            try {
                serialize::write_sigcache(cache, key, *out.result);
            } catch (const std::exception&) {
                // A cache we can't write only costs a re-analysis next time.
            }
        }
    } catch (const std::exception& e) {
        out.result.reset();
        out.error = std::string(label) + ": " + e.what();
    }
    return out;
}

std::vector<time::DraftLaneSpec> default_draft_lanes() {
    // Kick reads the dedicated kick detector (Sub onsets as a fallback);
    // snare and hats follow their bands' onsets, with the thresholds the
    // demo graph used for its onset lights.
    return {{"Kick", "kick", 0.0f, "onset.Sub"}, {"Snare", "onset.Mid", 0.35f, ""}, {"Hats", "onset.Air", 0.4f, ""}};
}

std::filesystem::path path_from_utf8(std::string_view utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
}

} // namespace ztrip::host
