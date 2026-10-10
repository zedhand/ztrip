#include "ztrip/serialize/tempo.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <functional>
#include <sstream>

namespace ztrip::serialize {
namespace {

using Json = nlohmann::ordered_json;

// migrations[v] upgrades a version v document to v + 1 in place.
const std::vector<std::function<void(Json&)>>& migrations() {
    static const std::vector<std::function<void(Json&)>> list = {
        nullptr, // there is no version 0
    };
    return list;
}

[[noreturn]] void fail(const std::string& what) { throw TempoMapError("tempo: " + what); }

double number_member(const Json& obj, const char* key, const std::string& where, double fallback) {
    const auto it = obj.find(key);
    if (it == obj.end()) return fallback;
    if (!it->is_number()) fail(where + "." + key + " must be a number");
    return it->get<double>();
}

int int_member(const Json& obj, const char* key, const std::string& where, int fallback) {
    const auto it = obj.find(key);
    if (it == obj.end()) return fallback;
    if (!it->is_number_integer()) fail(where + "." + key + " must be an integer");
    return it->get<int>();
}

Json array_member(const Json& obj, const char* key) {
    const auto it = obj.find(key);
    if (it == obj.end()) return Json::array();
    if (!it->is_array()) fail(std::string("\"") + key + "\" must be an array");
    return *it;
}

} // namespace

time::TempoMap parse_tempo_map(std::string_view text) {
    Json root;
    try {
        root = Json::parse(text.begin(), text.end());
    } catch (const Json::parse_error& e) {
        fail(std::string("invalid JSON: ") + e.what());
    }
    if (!root.is_object()) fail("document must be an object");
    if (const auto it = root.find("format"); it != root.end() && *it != "ztrip.tempo") fail("not a ztrip.tempo document");
    const auto version_it = root.find("version");
    if (version_it == root.end() || !version_it->is_number_integer()) fail("\"version\" must be an integer");
    int version = version_it->get<int>();
    if (version < 1) fail("unsupported version " + std::to_string(version));
    if (version > kTempoVersion) {
        fail("version " + std::to_string(version) + " is newer than this build supports (" +
             std::to_string(kTempoVersion) + ")");
    }
    for (; version < kTempoVersion; ++version) migrations()[static_cast<std::size_t>(version)](root);

    std::vector<time::TempoSegment> segments;
    for (const Json& s : array_member(root, "segments")) {
        const std::string where = "segments[" + std::to_string(segments.size()) + "]";
        if (!s.is_object()) fail(where + " must be an object");
        time::TempoSegment seg;
        seg.start_beat = number_member(s, "start_beat", where, 0.0);
        seg.bpm_start = number_member(s, "bpm_start", where, 120.0);
        seg.bpm_end = number_member(s, "bpm_end", where, seg.bpm_start);
        if (const auto it = s.find("curve"); it != s.end()) {
            if (!it->is_string() || !time::parse_tempo_curve(it->get<std::string>(), seg.curve)) {
                fail(where + ".curve must be \"step\", \"linear\" or \"exponential\"");
            }
        }
        segments.push_back(seg);
    }
    std::vector<time::TimeSignature> signatures;
    for (const Json& t : array_member(root, "time_signatures")) {
        const std::string where = "time_signatures[" + std::to_string(signatures.size()) + "]";
        if (!t.is_object()) fail(where + " must be an object");
        signatures.push_back({int_member(t, "bar", where, 0), int_member(t, "numerator", where, 4),
                              int_member(t, "denominator", where, 4)});
    }
    try {
        time::TempoMap map;
        if (!segments.empty()) map.set_segments(std::move(segments));
        map.set_time_signatures(std::move(signatures));
        map.set_offset(number_member(root, "offset_s", "document", 0.0));
        map.set_nudge(number_member(root, "nudge_s", "document", 0.0));
        return map;
    } catch (const std::invalid_argument& e) {
        fail(e.what());
    }
}

std::string write_tempo_map(const time::TempoMap& tempo) {
    Json root;
    root["format"] = "ztrip.tempo";
    root["version"] = kTempoVersion;
    root["offset_s"] = tempo.offset();
    root["nudge_s"] = tempo.nudge();
    Json segments = Json::array();
    for (const auto& s : tempo.segments()) {
        segments.push_back({{"start_beat", s.start_beat},
                            {"bpm_start", s.bpm_start},
                            {"bpm_end", s.bpm_end},
                            {"curve", std::string(time::to_string(s.curve))}});
    }
    root["segments"] = std::move(segments);
    Json signatures = Json::array();
    for (const auto& t : tempo.time_signatures()) {
        signatures.push_back({{"bar", t.bar}, {"numerator", t.numerator}, {"denominator", t.denominator}});
    }
    root["time_signatures"] = std::move(signatures);
    return root.dump(2) + "\n";
}

time::TempoMap load_tempo_map(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("can't open " + path.string());
    std::stringstream ss;
    ss << in.rdbuf();
    return parse_tempo_map(ss.str());
}

void save_tempo_map(const std::filesystem::path& path, const time::TempoMap& tempo) {
    std::ofstream out(path, std::ios::binary);
    if (!out) fail("can't write " + path.string());
    out << write_tempo_map(tempo);
    if (!out) fail("failed writing " + path.string());
}

} // namespace ztrip::serialize
