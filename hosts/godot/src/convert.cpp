#include "convert.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>

namespace ztrip::godot_host {

std::string to_std(const godot::String& s) {
    const godot::CharString utf8 = s.utf8();
    return {utf8.get_data(), static_cast<std::size_t>(utf8.length())};
}

godot::String to_godot(const std::string& s) { return godot::String::utf8(s.data(), static_cast<int64_t>(s.size())); }

std::filesystem::path to_native_path(const godot::String& path) {
    const godot::String global = godot::ProjectSettings::get_singleton()->globalize_path(path);
    const std::string utf8 = to_std(global);
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

godot::Variant to_variant(const graph::ParamValue& value) {
    if (const auto* d = std::get_if<double>(&value)) return *d;
    if (const auto* b = std::get_if<bool>(&value)) return *b;
    if (const auto* s = std::get_if<std::string>(&value)) return to_godot(*s);
    godot::PackedFloat64Array list;
    for (double x : std::get<std::vector<double>>(value)) list.push_back(x);
    return list;
}

} // namespace ztrip::godot_host
