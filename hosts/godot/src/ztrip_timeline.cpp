#include "ztrip_timeline.h"

#include "convert.h"
#include "ztrip_analysis.h"

#include "ztrip/serialize/nseq.hpp"
#include "ztrip/serialize/tempo.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <algorithm>
#include <cmath>
#include <exception>

namespace godot {

using ztrip::godot_host::to_godot;
using ztrip::godot_host::to_std;
namespace zt = ztrip::time;

namespace {

Dictionary note_to_dict(const zt::Note& n) {
    Dictionary d;
    d["start_beat"] = n.start_beat;
    d["length_beats"] = n.length_beats;
    d["velocity"] = n.velocity;
    d["pitch"] = n.pitch;
    d["envelope_ref"] = to_godot(n.envelope_ref);
    return d;
}

zt::Note note_from_dict(const Dictionary& d) {
    zt::Note n;
    n.start_beat = d.get("start_beat", 0.0);
    n.length_beats = std::max(1e-6, static_cast<double>(d.get("length_beats", 0.25)));
    n.velocity = std::clamp(static_cast<float>(static_cast<double>(d.get("velocity", 1.0))), 0.0f, 1.0f);
    n.pitch = static_cast<int>(d.get("pitch", -1));
    n.envelope_ref = to_std(d.get("envelope_ref", String()));
    return n;
}

bool read_text(const String& path, String& out, String& error) {
    if (!FileAccess::file_exists(path)) {
        error = "File not found: " + path;
        return false;
    }
    out = FileAccess::get_file_as_string(path);
    return true;
}

Error write_text(const String& path, const String& text, String& error) {
    const Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
    if (f.is_null()) {
        error = "Could not write " + path;
        return ERR_FILE_CANT_WRITE;
    }
    f->store_string(text);
    f->close();
    return OK;
}

} // namespace

ZtripTimeline::ZtripTimeline() {
    sequence_.name = "main";
    // Kick follows the dedicated kick detector when the analysis has it;
    // snare and hats follow their bands' onsets, with the thresholds the
    // demo graph used for its onset lights.
    draft_lanes_ = {{"Kick", "kick", 0.0f, "onset.Sub"},
                    {"Snare", "onset.Mid", 0.35f, ""},
                    {"Hats", "onset.Air", 0.4f, ""}};
}

void ZtripTimeline::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_offset"), &ZtripTimeline::get_offset);
    ClassDB::bind_method(D_METHOD("set_offset", "seconds"), &ZtripTimeline::set_offset);
    ClassDB::bind_method(D_METHOD("get_nudge"), &ZtripTimeline::get_nudge);
    ClassDB::bind_method(D_METHOD("set_nudge", "seconds"), &ZtripTimeline::set_nudge);
    ClassDB::bind_method(D_METHOD("nudge_by", "seconds"), &ZtripTimeline::nudge_by);
    ClassDB::bind_method(D_METHOD("scale_tempo", "factor"), &ZtripTimeline::scale_tempo);
    ClassDB::bind_method(D_METHOD("set_constant_tempo", "bpm"), &ZtripTimeline::set_constant_tempo);
    ClassDB::bind_method(D_METHOD("seconds_at", "beat"), &ZtripTimeline::seconds_at);
    ClassDB::bind_method(D_METHOD("beat_at", "seconds"), &ZtripTimeline::beat_at);
    ClassDB::bind_method(D_METHOD("bpm_at", "beat"), &ZtripTimeline::bpm_at);
    ClassDB::bind_method(D_METHOD("bar_start_beat", "bar"), &ZtripTimeline::bar_start_beat);
    ClassDB::bind_method(D_METHOD("position_at", "beat"), &ZtripTimeline::position_at);
    ClassDB::bind_method(D_METHOD("get_segments"), &ZtripTimeline::get_segments);
    ClassDB::bind_method(D_METHOD("set_segments", "segments"), &ZtripTimeline::set_segments);
    ClassDB::bind_method(D_METHOD("get_time_signatures"), &ZtripTimeline::get_time_signatures);
    ClassDB::bind_method(D_METHOD("set_time_signatures", "signatures"), &ZtripTimeline::set_time_signatures);

    ClassDB::bind_method(D_METHOD("get_lane_names"), &ZtripTimeline::get_lane_names);
    ClassDB::bind_method(D_METHOD("has_lane", "lane"), &ZtripTimeline::has_lane);
    ClassDB::bind_method(D_METHOD("add_lane", "lane"), &ZtripTimeline::add_lane);
    ClassDB::bind_method(D_METHOD("remove_lane", "lane"), &ZtripTimeline::remove_lane);
    ClassDB::bind_method(D_METHOD("rename_lane", "lane", "new_name"), &ZtripTimeline::rename_lane);
    ClassDB::bind_method(D_METHOD("get_lane_swing", "lane"), &ZtripTimeline::get_lane_swing);
    ClassDB::bind_method(D_METHOD("set_lane_swing", "lane", "swing"), &ZtripTimeline::set_lane_swing);
    ClassDB::bind_method(D_METHOD("get_notes", "lane"), &ZtripTimeline::get_notes);
    ClassDB::bind_method(D_METHOD("set_notes", "lane", "notes"), &ZtripTimeline::set_notes);
    ClassDB::bind_method(D_METHOD("add_note", "lane", "start_beat", "length_beats", "velocity"),
                         &ZtripTimeline::add_note, DEFVAL(1.0));
    ClassDB::bind_method(D_METHOD("remove_note", "lane", "index"), &ZtripTimeline::remove_note);
    ClassDB::bind_method(D_METHOD("get_note_count", "lane"), &ZtripTimeline::get_note_count);
    ClassDB::bind_method(D_METHOD("note_seconds", "lane", "beat"), &ZtripTimeline::note_seconds);

    ClassDB::bind_static_method("ZtripTimeline", D_METHOD("grid_step", "grid", "beat_unit"),
                                &ZtripTimeline::grid_step, DEFVAL(4));
    ClassDB::bind_static_method("ZtripTimeline", D_METHOD("snap", "beat", "step_beats", "strength"),
                                &ZtripTimeline::snap, DEFVAL(1.0));

    ClassDB::bind_method(D_METHOD("get_draft_lanes"), &ZtripTimeline::get_draft_lanes);
    ClassDB::bind_method(D_METHOD("set_draft_lanes", "lanes"), &ZtripTimeline::set_draft_lanes);
    ClassDB::bind_method(D_METHOD("draft_tempo", "analysis"), &ZtripTimeline::draft_tempo);
    ClassDB::bind_method(D_METHOD("draft_notes", "analysis", "grid"), &ZtripTimeline::draft_notes,
                         DEFVAL("1/16"));

    ClassDB::bind_method(D_METHOD("tempo_to_json"), &ZtripTimeline::tempo_to_json);
    ClassDB::bind_method(D_METHOD("tempo_from_json", "json"), &ZtripTimeline::tempo_from_json);
    ClassDB::bind_method(D_METHOD("sequence_to_json"), &ZtripTimeline::sequence_to_json);
    ClassDB::bind_method(D_METHOD("sequence_from_json", "json"), &ZtripTimeline::sequence_from_json);
    ClassDB::bind_method(D_METHOD("load_tempo", "path"), &ZtripTimeline::load_tempo);
    ClassDB::bind_method(D_METHOD("save_tempo", "path"), &ZtripTimeline::save_tempo);
    ClassDB::bind_method(D_METHOD("load_sequence", "path"), &ZtripTimeline::load_sequence);
    ClassDB::bind_method(D_METHOD("save_sequence", "path"), &ZtripTimeline::save_sequence);
    ClassDB::bind_method(D_METHOD("get_error"), &ZtripTimeline::get_error);

    ADD_SIGNAL(MethodInfo("changed"));
}

void ZtripTimeline::emit_changed() { emit_signal("changed"); }

// ---- Tempo map -------------------------------------------------------------------

void ZtripTimeline::set_offset(double seconds) {
    if (!std::isfinite(seconds)) return;
    tempo_.set_offset(seconds);
    emit_changed();
}

void ZtripTimeline::set_nudge(double seconds) {
    if (!std::isfinite(seconds)) return;
    tempo_.set_nudge(seconds);
    emit_changed();
}

void ZtripTimeline::nudge_by(double seconds) { set_nudge(tempo_.nudge() + seconds); }

void ZtripTimeline::scale_tempo(double factor) {
    if (!(factor > 0.0) || !std::isfinite(factor)) return;
    tempo_.scale_tempo(factor);
    emit_changed();
}

void ZtripTimeline::set_constant_tempo(double bpm) {
    if (!(bpm > 0.0) || !std::isfinite(bpm)) return;
    tempo_.set_segments({zt::TempoSegment{0.0, bpm, bpm, zt::TempoCurve::Step}});
    emit_changed();
}

Dictionary ZtripTimeline::position_at(double beat) const {
    const zt::MusicalPosition p = tempo_.position_at(beat);
    Dictionary d;
    d["bar"] = p.bar;
    d["beat"] = p.beat;
    d["beat_phase"] = p.beat_phase;
    d["bar_phase"] = p.bar_phase;
    d["numerator"] = p.numerator;
    d["denominator"] = p.denominator;
    return d;
}

Array ZtripTimeline::get_segments() const {
    Array out;
    for (const auto& s : tempo_.segments()) {
        Dictionary d;
        d["start_beat"] = s.start_beat;
        d["bpm_start"] = s.bpm_start;
        d["bpm_end"] = s.bpm_end;
        d["curve"] = to_godot(std::string(zt::to_string(s.curve)));
        out.push_back(d);
    }
    return out;
}

Error ZtripTimeline::set_segments(const Array& segments) {
    std::vector<zt::TempoSegment> list;
    for (int i = 0; i < segments.size(); ++i) {
        const Dictionary d = segments[i];
        zt::TempoSegment s;
        s.start_beat = d.get("start_beat", 0.0);
        s.bpm_start = d.get("bpm_start", 120.0);
        s.bpm_end = d.get("bpm_end", s.bpm_start);
        if (!zt::parse_tempo_curve(to_std(d.get("curve", "step")), s.curve)) {
            error_ = "Unknown tempo curve: " + String(d.get("curve", ""));
            return ERR_INVALID_PARAMETER;
        }
        list.push_back(s);
    }
    try {
        tempo_.set_segments(std::move(list));
    } catch (const std::exception& e) {
        error_ = to_godot(e.what());
        return ERR_INVALID_PARAMETER;
    }
    emit_changed();
    return OK;
}

Array ZtripTimeline::get_time_signatures() const {
    Array out;
    for (const auto& s : tempo_.time_signatures()) {
        Dictionary d;
        d["bar"] = s.bar;
        d["numerator"] = s.numerator;
        d["denominator"] = s.denominator;
        out.push_back(d);
    }
    return out;
}

Error ZtripTimeline::set_time_signatures(const Array& signatures) {
    std::vector<zt::TimeSignature> list;
    for (int i = 0; i < signatures.size(); ++i) {
        const Dictionary d = signatures[i];
        list.push_back({static_cast<int>(d.get("bar", 0)), static_cast<int>(d.get("numerator", 4)),
                        static_cast<int>(d.get("denominator", 4))});
    }
    try {
        tempo_.set_time_signatures(std::move(list));
    } catch (const std::exception& e) {
        error_ = to_godot(e.what());
        return ERR_INVALID_PARAMETER;
    }
    emit_changed();
    return OK;
}

// ---- Lanes and notes ---------------------------------------------------------------

zt::Lane* ZtripTimeline::lane(const String& name) { return sequence_.find_lane(to_std(name)); }
const zt::Lane* ZtripTimeline::lane(const String& name) const { return sequence_.find_lane(to_std(name)); }

PackedStringArray ZtripTimeline::get_lane_names() const {
    PackedStringArray out;
    for (const auto& l : sequence_.lanes) out.push_back(to_godot(l.name));
    return out;
}

bool ZtripTimeline::has_lane(const String& name) const { return lane(name) != nullptr; }

bool ZtripTimeline::add_lane(const String& name) {
    if (name.strip_edges().is_empty() || lane(name)) return false;
    zt::Lane l;
    l.name = to_std(name);
    sequence_.lanes.push_back(std::move(l));
    emit_changed();
    return true;
}

bool ZtripTimeline::remove_lane(const String& name) {
    auto& lanes = sequence_.lanes;
    const auto it = std::find_if(lanes.begin(), lanes.end(), [&](const zt::Lane& l) { return l.name == to_std(name); });
    if (it == lanes.end()) return false;
    lanes.erase(it);
    emit_changed();
    return true;
}

bool ZtripTimeline::rename_lane(const String& name, const String& new_name) {
    zt::Lane* l = lane(name);
    if (!l || new_name.strip_edges().is_empty() || (new_name != name && lane(new_name))) return false;
    l->name = to_std(new_name);
    emit_changed();
    return true;
}

double ZtripTimeline::get_lane_swing(const String& name) const {
    const zt::Lane* l = lane(name);
    return l ? l->swing : 0.0;
}

void ZtripTimeline::set_lane_swing(const String& name, double swing) {
    zt::Lane* l = lane(name);
    if (!l || !std::isfinite(swing)) return;
    l->swing = std::clamp(swing, 0.0, 0.75);
    emit_changed();
}

Array ZtripTimeline::get_notes(const String& name) const {
    Array out;
    if (const zt::Lane* l = lane(name))
        for (const auto& n : l->notes) out.push_back(note_to_dict(n));
    return out;
}

void ZtripTimeline::set_notes(const String& name, const Array& notes) {
    zt::Lane* l = lane(name);
    if (!l) {
        sequence_.lanes.push_back(zt::Lane{to_std(name), {}});
        l = &sequence_.lanes.back();
    }
    l->notes.clear();
    l->notes.reserve(notes.size());
    for (int i = 0; i < notes.size(); ++i) l->notes.push_back(note_from_dict(notes[i]));
    l->sort();
    emit_changed();
}

int ZtripTimeline::add_note(const String& name, double start_beat, double length_beats, double velocity) {
    zt::Lane* l = lane(name);
    if (!l || !std::isfinite(start_beat) || !(length_beats > 0.0)) return -1;
    zt::Note n;
    n.start_beat = start_beat;
    n.length_beats = length_beats;
    n.velocity = static_cast<float>(std::clamp(velocity, 0.0, 1.0));
    // Insert after notes starting at the same beat, as sort() would place it.
    const auto at = std::upper_bound(l->notes.begin(), l->notes.end(), start_beat,
                                     [](double b, const zt::Note& x) { return b < x.start_beat; });
    const int index = static_cast<int>(at - l->notes.begin());
    l->notes.insert(at, n);
    emit_changed();
    return index;
}

bool ZtripTimeline::remove_note(const String& name, int index) {
    zt::Lane* l = lane(name);
    if (!l || index < 0 || index >= static_cast<int>(l->notes.size())) return false;
    l->notes.erase(l->notes.begin() + index);
    emit_changed();
    return true;
}

int ZtripTimeline::get_note_count(const String& name) const {
    const zt::Lane* l = lane(name);
    return l ? static_cast<int>(l->notes.size()) : 0;
}

double ZtripTimeline::note_seconds(const String& name, double beat) const {
    const zt::Lane* l = lane(name);
    return tempo_.seconds_at(l ? l->swung(beat) : beat);
}

// ---- Grid --------------------------------------------------------------------------

double ZtripTimeline::grid_step(const String& grid, int beat_unit) {
    const auto g = zt::parse_grid(to_std(grid));
    return g ? zt::grid_step_beats(*g, beat_unit) : 0.0;
}

double ZtripTimeline::snap(double beat, double step_beats, double strength) {
    return zt::snap(beat, step_beats, strength);
}

// ---- Auto-draft --------------------------------------------------------------------

Array ZtripTimeline::get_draft_lanes() const {
    Array out;
    for (const auto& s : draft_lanes_) {
        Dictionary d;
        d["lane"] = to_godot(s.lane);
        d["events"] = to_godot(s.events);
        d["fallback"] = to_godot(s.fallback);
        d["min_velocity"] = s.min_velocity;
        out.push_back(d);
    }
    return out;
}

void ZtripTimeline::set_draft_lanes(const Array& lanes) {
    draft_lanes_.clear();
    for (int i = 0; i < lanes.size(); ++i) {
        const Dictionary d = lanes[i];
        draft_lanes_.push_back({to_std(d.get("lane", "")), to_std(d.get("events", "")),
                                static_cast<float>(static_cast<double>(d.get("min_velocity", 0.0))),
                                to_std(d.get("fallback", ""))});
    }
}

Error ZtripTimeline::draft_tempo(const Ref<ZtripAnalysis>& analysis) {
    if (analysis.is_null() || !analysis->result()) {
        error_ = "No analysis to draft from.";
        return ERR_UNCONFIGURED;
    }
    const double nudge = tempo_.nudge();
    tempo_ = zt::tempo_map_from_beats(analysis->result()->beats);
    tempo_.set_nudge(nudge);
    emit_changed();
    return OK;
}

Error ZtripTimeline::draft_notes(const Ref<ZtripAnalysis>& analysis, const String& grid) {
    if (analysis.is_null() || !analysis->result()) {
        error_ = "No analysis to draft from.";
        return ERR_UNCONFIGURED;
    }
    zt::DraftSettings settings;
    if (const auto g = zt::parse_grid(to_std(grid))) {
        settings.grid = *g;
    } else {
        error_ = "Unknown grid: " + grid;
        return ERR_INVALID_PARAMETER;
    }
    settings.lanes = draft_lanes_;
    zt::NoteSequence drafted = zt::auto_draft(*analysis->result(), tempo_, settings);
    for (auto& l : drafted.lanes) {
        if (zt::Lane* existing = sequence_.find_lane(l.name)) {
            existing->notes = std::move(l.notes); // keep the lane's swing
        } else {
            sequence_.lanes.push_back(std::move(l));
        }
    }
    emit_changed();
    return OK;
}

// ---- Files -------------------------------------------------------------------------

String ZtripTimeline::tempo_to_json() const { return to_godot(ztrip::serialize::write_tempo_map(tempo_)); }

Error ZtripTimeline::tempo_from_json(const String& json) {
    try {
        tempo_ = ztrip::serialize::parse_tempo_map(to_std(json));
    } catch (const std::exception& e) {
        error_ = to_godot(e.what());
        return ERR_PARSE_ERROR;
    }
    emit_changed();
    return OK;
}

String ZtripTimeline::sequence_to_json() const { return to_godot(ztrip::serialize::write_nseq(sequence_)); }

Error ZtripTimeline::sequence_from_json(const String& json) {
    try {
        sequence_ = ztrip::serialize::parse_nseq(to_std(json));
    } catch (const std::exception& e) {
        error_ = to_godot(e.what());
        return ERR_PARSE_ERROR;
    }
    emit_changed();
    return OK;
}

Error ZtripTimeline::load_tempo(const String& path) {
    String text;
    if (!read_text(path, text, error_)) return ERR_FILE_NOT_FOUND;
    const Error err = tempo_from_json(text);
    if (err != OK) error_ = path.get_file() + ": " + error_;
    return err;
}

Error ZtripTimeline::save_tempo(const String& path) const { return write_text(path, tempo_to_json(), error_); }

Error ZtripTimeline::load_sequence(const String& path) {
    String text;
    if (!read_text(path, text, error_)) return ERR_FILE_NOT_FOUND;
    const Error err = sequence_from_json(text);
    if (err != OK) error_ = path.get_file() + ": " + error_;
    return err;
}

Error ZtripTimeline::save_sequence(const String& path) const { return write_text(path, sequence_to_json(), error_); }

} // namespace godot
