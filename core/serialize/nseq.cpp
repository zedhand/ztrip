#include "ztrip/serialize/nseq.hpp"

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

[[noreturn]] void fail(const std::string& what) { throw NseqError("nseq: " + what); }

std::string string_member(const Json& obj, const char* key, const std::string& where, const char* fallback = nullptr) {
    const auto it = obj.find(key);
    if (it == obj.end()) {
        if (fallback) return fallback;
        fail(where + " is missing \"" + key + "\"");
    }
    if (!it->is_string()) fail(where + "." + key + " must be a string");
    return it->get<std::string>();
}

double number_member(const Json& obj, const char* key, const std::string& where, double fallback) {
    const auto it = obj.find(key);
    if (it == obj.end()) return fallback;
    if (!it->is_number()) fail(where + "." + key + " must be a number");
    return it->get<double>();
}

double required_number(const Json& obj, const char* key, const std::string& where) {
    if (obj.find(key) == obj.end()) fail(where + " is missing \"" + key + "\"");
    return number_member(obj, key, where, 0.0);
}

Json array_member(const Json& obj, const char* key, const std::string& where) {
    const auto it = obj.find(key);
    if (it == obj.end()) return Json::array();
    if (!it->is_array()) fail(where + "\"" + key + "\" must be an array");
    return *it;
}

} // namespace

time::NoteSequence parse_nseq(std::string_view text) {
    Json root;
    try {
        root = Json::parse(text.begin(), text.end());
    } catch (const Json::parse_error& e) {
        fail(std::string("invalid JSON: ") + e.what());
    }
    if (!root.is_object()) fail("document must be an object");
    if (const auto it = root.find("format"); it != root.end() && *it != "ztrip.nseq") fail("not a ztrip.nseq document");
    const auto version_it = root.find("version");
    if (version_it == root.end() || !version_it->is_number_integer()) fail("\"version\" must be an integer");
    int version = version_it->get<int>();
    if (version < 1) fail("unsupported version " + std::to_string(version));
    if (version > kNseqVersion) {
        fail("version " + std::to_string(version) + " is newer than this build supports (" +
             std::to_string(kNseqVersion) + ")");
    }
    for (; version < kNseqVersion; ++version) migrations()[static_cast<std::size_t>(version)](root);

    time::NoteSequence seq;
    seq.name = string_member(root, "name", "document", "");
    for (const Json& l : array_member(root, "lanes", "")) {
        const std::string where = "lanes[" + std::to_string(seq.lanes.size()) + "]";
        if (!l.is_object()) fail(where + " must be an object");
        time::Lane lane;
        lane.name = string_member(l, "name", where);
        if (seq.find_lane(lane.name)) fail(where + ": duplicate lane name \"" + lane.name + "\"");
        lane.swing = number_member(l, "swing", where, 0.0);
        lane.swing_division = number_member(l, "swing_division", where, 0.5);
        for (const Json& n : array_member(l, "notes", where + ".")) {
            const std::string nw = where + ".notes[" + std::to_string(lane.notes.size()) + "]";
            if (!n.is_object()) fail(nw + " must be an object");
            time::Note note;
            note.start_beat = required_number(n, "start_beat", nw);
            note.length_beats = number_member(n, "length_beats", nw, 0.25);
            note.velocity = static_cast<float>(number_member(n, "velocity", nw, 1.0));
            if (const auto it = n.find("pitch"); it != n.end()) {
                if (!it->is_number_integer()) fail(nw + ".pitch must be an integer");
                note.pitch = it->get<int>();
            }
            note.envelope_ref = string_member(n, "envelope_ref", nw, "");
            lane.notes.push_back(std::move(note));
        }
        lane.sort();
        seq.lanes.push_back(std::move(lane));
    }
    for (const Json& e : array_member(root, "envelopes", "")) {
        const std::string where = "envelopes[" + std::to_string(seq.envelopes.size()) + "]";
        if (!e.is_object()) fail(where + " must be an object");
        time::Envelope env;
        env.name = string_member(e, "name", where);
        const std::string kind = string_member(e, "kind", where, "adsr");
        if (kind == "adsr") {
            env.kind = time::Envelope::Kind::Adsr;
            env.attack_beats = number_member(e, "attack_beats", where, env.attack_beats);
            env.decay_beats = number_member(e, "decay_beats", where, env.decay_beats);
            env.sustain = number_member(e, "sustain", where, env.sustain);
            env.release_beats = number_member(e, "release_beats", where, env.release_beats);
        } else if (kind == "curve") {
            env.kind = time::Envelope::Kind::Curve;
            for (const Json& x : array_member(e, "points", where + ".")) {
                if (!x.is_number()) fail(where + ".points must hold numbers");
                env.points.push_back(x.get<double>());
            }
            if (env.points.size() % 2 != 0) fail(where + ".points must be x0, y0, x1, y1, ...");
        } else {
            fail(where + ".kind must be \"adsr\" or \"curve\"");
        }
        seq.envelopes.push_back(std::move(env));
    }
    return seq;
}

std::string write_nseq(const time::NoteSequence& seq) {
    Json root;
    root["format"] = "ztrip.nseq";
    root["version"] = kNseqVersion;
    root["name"] = seq.name;
    Json lanes = Json::array();
    for (const time::Lane& lane : seq.lanes) {
        Json notes = Json::array();
        for (const time::Note& n : lane.notes) {
            Json note = {{"start_beat", n.start_beat}, {"length_beats", n.length_beats},
                         {"velocity", static_cast<double>(n.velocity)}};
            if (n.pitch >= 0) note["pitch"] = n.pitch;
            if (!n.envelope_ref.empty()) note["envelope_ref"] = n.envelope_ref;
            notes.push_back(std::move(note));
        }
        lanes.push_back({{"name", lane.name},
                         {"swing", lane.swing},
                         {"swing_division", lane.swing_division},
                         {"notes", std::move(notes)}});
    }
    root["lanes"] = std::move(lanes);
    Json envelopes = Json::array();
    for (const time::Envelope& e : seq.envelopes) {
        if (e.kind == time::Envelope::Kind::Curve) {
            envelopes.push_back({{"name", e.name}, {"kind", "curve"}, {"points", e.points}});
        } else {
            envelopes.push_back({{"name", e.name},
                                 {"kind", "adsr"},
                                 {"attack_beats", e.attack_beats},
                                 {"decay_beats", e.decay_beats},
                                 {"sustain", e.sustain},
                                 {"release_beats", e.release_beats}});
        }
    }
    root["envelopes"] = std::move(envelopes);
    return root.dump(2) + "\n";
}

time::NoteSequence load_nseq(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("can't open " + path.string());
    std::stringstream ss;
    ss << in.rdbuf();
    return parse_nseq(ss.str());
}

void save_nseq(const std::filesystem::path& path, const time::NoteSequence& seq) {
    std::ofstream out(path, std::ios::binary);
    if (!out) fail("can't write " + path.string());
    out << write_nseq(seq);
    if (!out) fail("failed writing " + path.string());
}

} // namespace ztrip::serialize
