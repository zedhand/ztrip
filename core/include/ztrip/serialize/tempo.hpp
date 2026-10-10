#pragma once

#include "ztrip/time/tempo_map.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ztrip::serialize {

// Tempo map JSON (version 1), saved on its own as *.tempo.json and meant to
// be embedded as the "tempo_map" object of project.json:
//   {
//     "format": "ztrip.tempo", "version": 1,
//     "offset_s": 0.12, "nudge_s": 0.0,
//     "segments": [{"start_beat": 0, "bpm_start": 120, "bpm_end": 120, "curve": "step"}],
//     "time_signatures": [{"bar": 0, "numerator": 4, "denominator": 4}]
//   }
// "curve" is step, linear or exponential.
inline constexpr int kTempoVersion = 1;

class TempoMapError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

time::TempoMap parse_tempo_map(std::string_view json);
std::string write_tempo_map(const time::TempoMap& tempo);

time::TempoMap load_tempo_map(const std::filesystem::path& path);
void save_tempo_map(const std::filesystem::path& path, const time::TempoMap& tempo);

} // namespace ztrip::serialize
