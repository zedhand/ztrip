#include "ztrip/serialize/controls.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace ztrip::serialize {
namespace {

using Json = nlohmann::ordered_json;

[[noreturn]] void fail(const std::string& what) { throw ControlsError("controls: " + what); }

} // namespace

std::vector<io::ControlBinding> parse_controls(std::string_view text) {
    Json root;
    try {
        root = Json::parse(text.begin(), text.end());
    } catch (const Json::parse_error& e) {
        fail(std::string("invalid JSON: ") + e.what());
    }
    if (!root.is_object()) fail("document must be an object");
    if (const auto it = root.find("format"); it != root.end() && *it != "ztrip.controls") {
        fail("not a ztrip.controls document");
    }
    const auto version = root.find("version");
    if (version == root.end() || !version->is_number_integer()) fail("\"version\" must be an integer");
    if (version->get<int>() < 1 || version->get<int>() > kControlsVersion) {
        fail("unsupported version " + std::to_string(version->get<int>()));
    }
    std::vector<io::ControlBinding> out;
    const auto list = root.find("bindings");
    if (list == root.end()) return out;
    if (!list->is_array()) fail("\"bindings\" must be an array");
    for (std::size_t i = 0; i < list->size(); ++i) {
        const Json& b = (*list)[i];
        const std::string where = "bindings[" + std::to_string(i) + "]";
        if (!b.is_object()) fail(where + " must be an object");
        io::ControlBinding binding;
        for (const char* key : {"macro", "control"}) {
            const auto it = b.find(key);
            if (it == b.end() || !it->is_string() || it->get<std::string>().empty()) {
                fail(where + "." + key + " must be a non-empty string");
            }
            (std::string_view(key) == "macro" ? binding.macro : binding.control) = it->get<std::string>();
        }
        for (const char* key : {"min", "max"}) {
            const auto it = b.find(key);
            if (it == b.end()) continue;
            if (!it->is_number()) fail(where + "." + key + " must be a number");
            (std::string_view(key) == "min" ? binding.min : binding.max) = it->get<double>();
        }
        out.push_back(std::move(binding));
    }
    return out;
}

std::string write_controls(const std::vector<io::ControlBinding>& bindings) {
    Json root = Json::object();
    root["format"] = "ztrip.controls";
    root["version"] = kControlsVersion;
    Json list = Json::array();
    for (const auto& b : bindings) {
        list.push_back({{"macro", b.macro}, {"control", b.control}, {"min", b.min}, {"max", b.max}});
    }
    root["bindings"] = std::move(list);
    return root.dump(2) + "\n";
}

std::vector<io::ControlBinding> load_controls(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("cannot open " + path.string());
    std::stringstream ss;
    ss << in.rdbuf();
    return parse_controls(ss.str());
}

void save_controls(const std::filesystem::path& path, const std::vector<io::ControlBinding>& bindings) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) fail("cannot write " + path.string());
    out << write_controls(bindings);
}

} // namespace ztrip::serialize
