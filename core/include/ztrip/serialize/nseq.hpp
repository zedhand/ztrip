#pragma once

#include "ztrip/time/note_sequence.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ztrip::serialize {

// .nseq.json layout (version 1):
//   {
//     "format": "ztrip.nseq", "version": 1, "name": "...",
//     "lanes": [{"name": "Kick", "swing": 0, "swing_division": 0.5,
//                "notes": [{"start_beat": 0, "length_beats": 0.25, "velocity": 1,
//                           "pitch": 36, "envelope_ref": "hit"}]}],
//     "envelopes": [{"name": "hit", "kind": "adsr", "attack_beats": 0, "decay_beats": 0.25,
//                    "sustain": 0.6, "release_beats": 0.25},
//                   {"name": "swell", "kind": "curve", "points": [0, 0, 1, 1]}]
//   }
// "pitch" and "envelope_ref" are optional. Older versions are migrated on
// load; newer ones are rejected. Notes are sorted by start on load.
inline constexpr int kNseqVersion = 1;

class NseqError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

time::NoteSequence parse_nseq(std::string_view json);
std::string write_nseq(const time::NoteSequence& sequence);

time::NoteSequence load_nseq(const std::filesystem::path& path);
void save_nseq(const std::filesystem::path& path, const time::NoteSequence& sequence);

} // namespace ztrip::serialize
