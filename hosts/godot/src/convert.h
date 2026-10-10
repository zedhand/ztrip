// Conversions between Godot and ztrip_core types.
#pragma once

#include "ztrip/graph/document.hpp"

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <filesystem>
#include <string>

namespace ztrip::godot_host {

std::string to_std(const godot::String& s);
godot::String to_godot(const std::string& s);

// A res:// or user:// path (or an absolute one) as a native filesystem path.
std::filesystem::path to_native_path(const godot::String& path);

godot::Variant to_variant(const graph::ParamValue& value);

} // namespace ztrip::godot_host
