#pragma once

#include "ztrip/graph/document.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ztrip::serialize {

// .mgraph.json layout (version 1):
//   {
//     "format": "ztrip.mgraph", "version": 1, "name": "...",
//     "nodes":  [{"id": "kick", "type": "AudioOnset", "params": {"band": "Sub"}, "pos": [0, 0]}],
//     "edges":  [{"from": "kick.trigger", "to": "flash.trigger"}],
//     "macros": [{"name": "Intensity", "node": "gain", "param": "b", "min": 0, "max": 2, "default": 1}]
//   }
// Param values are numbers, bools, strings or number lists (written flat;
// a list of [x, y] pairs is also accepted and flattened). Older versions are
// migrated on load; newer ones are rejected.
inline constexpr int kMgraphVersion = 1;

class MgraphError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Throws MgraphError on malformed JSON or structure. Graph semantics (node
// types, port names, cycles) are checked by graph::compile, not here.
graph::GraphDoc parse_mgraph(std::string_view json);

// Pretty-printed JSON with a stable key order, so files diff cleanly.
std::string write_mgraph(const graph::GraphDoc& doc);

graph::GraphDoc load_mgraph(const std::filesystem::path& path);
void save_mgraph(const std::filesystem::path& path, const graph::GraphDoc& doc);

} // namespace ztrip::serialize
