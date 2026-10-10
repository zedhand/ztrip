#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ztrip::time {

class TempoMap;

// A per-note envelope, output as a Continuous signal. ADSR times are in
// beats, like all authored timing; release runs after the note ends. A curve
// is drawn over the note's length: points (x0, y0, x1, y1, ...) with x in
// 0..1 of the note, linearly interpolated, and 0 after the note ends.
struct Envelope {
    enum class Kind { Adsr, Curve };
    std::string name;
    Kind kind = Kind::Adsr;
    double attack_beats = 0.0;
    double decay_beats = 0.25;
    double sustain = 0.6;   // 0..1 of the note velocity
    double release_beats = 0.25;
    std::vector<double> points; // Curve only
};

struct Note {
    double start_beat = 0.0;
    double length_beats = 0.25;
    float velocity = 1.0f;   // 0..1
    int pitch = -1;          // MIDI note number, or -1 for none
    std::string envelope_ref; // name of an Envelope in the sequence, or empty
};

// A named lane of notes. Notes are kept sorted by start; overlapping notes
// are allowed (the gate stays open while any note is held). Swing delays the
// second half of every pair of `swing_division` beats: 0 is straight, 1/3 is
// triplet swing, and it is clamped to 0..0.75.
struct Lane {
    std::string name;
    std::vector<Note> notes;
    double swing = 0.0;
    double swing_division = 0.5; // beats; 0.5 swings eighth notes in x/4

    void sort();
    // Beat position after swing.
    double swung(double beat) const noexcept;
};

// A Note Sequence: lanes and the envelopes their notes can reference.
struct NoteSequence {
    std::string name;
    std::vector<Lane> lanes;
    std::vector<Envelope> envelopes;

    Lane* find_lane(std::string_view lane_name) noexcept;
    const Lane* find_lane(std::string_view lane_name) const noexcept;
    const Envelope* find_envelope(std::string_view envelope_name) const noexcept;
};

// Envelope level `beats_since_start` into a note of `length_beats`, scaled
// by velocity. With no envelope it is the velocity while the note is held.
double envelope_level(const Envelope* envelope, const Note& note, double beats_since_start) noexcept;

// ---- Grid ----------------------------------------------------------------

// A snap grid: 1/1 to 1/64 notes, optionally triplets.
struct GridDivision {
    int denominator = 16; // 1, 2, 4, 8, 16, 32 or 64
    bool triplet = false;
};

// Parses "1/16", "1/8T" or "1/8t". Returns nullopt when malformed.
std::optional<GridDivision> parse_grid(std::string_view text) noexcept;
std::string to_string(const GridDivision& grid);

// Grid step in beats, for a meter whose beat is a 1/beat_unit note.
double grid_step_beats(const GridDivision& grid, int beat_unit = 4) noexcept;

// Moves `beat` towards the nearest multiple of step_beats by `strength`
// (0 = unchanged, 1 = on the grid).
double snap(double beat, double step_beats, double strength = 1.0) noexcept;

// ---- Timeline lookup -----------------------------------------------------

// A note resolved to seconds through a tempo map, swing applied.
struct TimedNote {
    double start_s = 0.0;
    double end_s = 0.0;
    double start_beat = 0.0; // after swing
    double end_beat = 0.0;
    const Note* note = nullptr;
    const Envelope* envelope = nullptr;
};

// Notes of one lane in seconds, sorted by start, for evaluation at any time
// by lookup. Rebuild it when the lane, the envelopes or the tempo map change.
class LaneTimeline {
public:
    LaneTimeline() = default;
    LaneTimeline(const NoteSequence& sequence, const Lane& lane, const TempoMap& tempo);

    const std::vector<TimedNote>& notes() const noexcept { return notes_; }

    // 1 while any note is held at time t, else 0.
    bool gate(double t) const noexcept;
    // Strongest note starting in (t0, t1], or nullptr.
    const TimedNote* started_in(double t0, double t1) const noexcept;
    // The latest note started at or before t (the one whose envelope and
    // velocity apply), or nullptr.
    const TimedNote* current(double t) const noexcept;
    // Max envelope level of every note sounding at t, release tails included.
    double envelope(double t) const noexcept;

private:
    const TempoMap* tempo_ = nullptr;
    std::vector<TimedNote> notes_;
    std::vector<double> max_end_s_; // running max of end_s (plus release) up to each note
};

} // namespace ztrip::time
