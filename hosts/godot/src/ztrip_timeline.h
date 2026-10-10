// ZtripTimeline: a tempo map and a Note Sequence, editable from GDScript.
#pragma once

#include "ztrip/time/autodraft.hpp"
#include "ztrip/time/note_sequence.hpp"
#include "ztrip/time/tempo_map.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace godot {

class ZtripAnalysis;

// The musical time of one song: a tempo map (beats <-> seconds, meter,
// offset and nudge) and a Note Sequence of named lanes. All note timing is in
// beats. Every edit emits `changed`, which a ZtripPlayer playing this
// timeline listens to so it rebinds the graph at once. Saved as a
// .tempo.json and a .nseq.json.
class ZtripTimeline : public RefCounted {
    GDCLASS(ZtripTimeline, RefCounted)

public:
    ZtripTimeline();

    // ---- Tempo map ----
    double get_offset() const { return tempo_.offset(); }
    void set_offset(double seconds);
    double get_nudge() const { return tempo_.nudge(); }
    void set_nudge(double seconds);
    void nudge_by(double seconds);
    void scale_tempo(double factor);
    // Replaces the map with one constant tempo, keeping offset and nudge.
    void set_constant_tempo(double bpm);

    double seconds_at(double beat) const { return tempo_.seconds_at(beat); }
    double beat_at(double seconds) const { return tempo_.beat_at(seconds); }
    double bpm_at(double beat) const { return tempo_.bpm_at(beat); }
    double bar_start_beat(int bar) const { return tempo_.bar_start_beat(bar); }
    // {bar, beat, beat_phase, bar_phase, numerator, denominator}
    Dictionary position_at(double beat) const;
    // [{start_beat, bpm_start, bpm_end, curve}], curve "step", "linear" or "exponential"
    Array get_segments() const;
    Error set_segments(const Array& segments);
    // [{bar, numerator, denominator}]
    Array get_time_signatures() const;
    Error set_time_signatures(const Array& signatures);

    // ---- Lanes and notes ----
    PackedStringArray get_lane_names() const;
    bool has_lane(const String& lane) const;
    bool add_lane(const String& lane);
    bool remove_lane(const String& lane);
    bool rename_lane(const String& lane, const String& new_name);
    double get_lane_swing(const String& lane) const;
    void set_lane_swing(const String& lane, double swing);
    // Notes sorted by start: [{start_beat, length_beats, velocity, pitch, envelope_ref}]
    Array get_notes(const String& lane) const;
    // Replaces a lane's notes (creating the lane if needed); they are sorted.
    void set_notes(const String& lane, const Array& notes);
    // Adds one note and returns its index after sorting, or -1.
    int add_note(const String& lane, double start_beat, double length_beats, double velocity = 1.0);
    bool remove_note(const String& lane, int index);
    int get_note_count(const String& lane) const;
    // Seconds of a beat in a lane, its swing applied (what the graph plays).
    double note_seconds(const String& lane, double beat) const;

    // ---- Grid ----
    // Grid step in beats for "1/16", "1/8T", ...; 0 when malformed.
    static double grid_step(const String& grid, int beat_unit = 4);
    static double snap(double beat, double step_beats, double strength = 1.0);

    // ---- Auto-draft ----
    // Lanes the draft fills: [{lane, events, fallback, min_velocity}].
    Array get_draft_lanes() const;
    void set_draft_lanes(const Array& lanes);
    // Builds the tempo map from the analysed beats, keeping the nudge.
    Error draft_tempo(const Ref<ZtripAnalysis>& analysis);
    // Replaces the drafted lanes with notes from the analysed events,
    // quantized to `grid` through the current tempo map. Other lanes stay.
    Error draft_notes(const Ref<ZtripAnalysis>& analysis, const String& grid = "1/16");

    // ---- Files ----
    String tempo_to_json() const;
    Error tempo_from_json(const String& json);
    String sequence_to_json() const;
    Error sequence_from_json(const String& json);
    Error load_tempo(const String& path);
    Error save_tempo(const String& path) const;
    Error load_sequence(const String& path);
    Error save_sequence(const String& path) const;
    String get_error() const { return error_; }

    const ztrip::time::TempoMap& tempo() const { return tempo_; }
    const ztrip::time::NoteSequence& sequence() const { return sequence_; }

protected:
    static void _bind_methods();

private:
    void emit_changed();
    ztrip::time::Lane* lane(const String& name);
    const ztrip::time::Lane* lane(const String& name) const;

    ztrip::time::TempoMap tempo_;
    ztrip::time::NoteSequence sequence_;
    std::vector<ztrip::time::DraftLaneSpec> draft_lanes_;
    mutable String error_;
};

} // namespace godot
