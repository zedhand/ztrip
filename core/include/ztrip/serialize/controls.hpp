#pragma once

#include "ztrip/io/controls.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ztrip::serialize {

// Controller profile JSON (version 1), saved as *.controls.json:
//   {
//     "format": "ztrip.controls", "version": 1,
//     "bindings": [{"macro": "Gain", "control": "midi/0/cc/7", "min": 0, "max": 8}]
//   }
inline constexpr int kControlsVersion = 1;

class ControlsError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::vector<io::ControlBinding> parse_controls(std::string_view json);
std::string write_controls(const std::vector<io::ControlBinding>& bindings);

std::vector<io::ControlBinding> load_controls(const std::filesystem::path& path);
void save_controls(const std::filesystem::path& path, const std::vector<io::ControlBinding>& bindings);

} // namespace ztrip::serialize
