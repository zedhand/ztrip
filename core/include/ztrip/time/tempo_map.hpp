#pragma once

#include <span>
#include <string_view>
#include <vector>

namespace ztrip::analysis {
struct BeatGrid;
}

namespace ztrip::time {

// How tempo moves from bpm_start to bpm_end across a segment. Step holds
// bpm_start; Linear ramps BPM linearly in beats; Exponential ramps it by a
// constant ratio per beat.
enum class TempoCurve { Step, Linear, Exponential };

std::string_view to_string(TempoCurve curve) noexcept;
bool parse_tempo_curve(std::string_view name, TempoCurve& out) noexcept;

// One tempo segment. It runs from start_beat to the next segment's start;
// the last segment has no end, so it holds bpm_start whatever its curve.
struct TempoSegment {
    double start_beat = 0.0;
    double bpm_start = 120.0;
    double bpm_end = 120.0;
    TempoCurve curve = TempoCurve::Step;
};

// A meter change at a bar boundary. A beat is one count of the meter (one
// 1/denominator note) and BPM counts those beats.
struct TimeSignature {
    int bar = 0;         // first bar in this meter (bar 0 starts at beat 0)
    int numerator = 4;   // beats per bar
    int denominator = 4; // note value of one beat
};

// A musical position. Bars and beats count from 0; beat 0 is the first
// downbeat. Positions before it have negative bars.
struct MusicalPosition {
    int bar = 0;
    int beat = 0;             // beat within the bar, 0..numerator-1
    double beat_phase = 0.0;  // 0..1 within the beat
    double bar_phase = 0.0;   // 0..1 within the bar
    int numerator = 4;
    int denominator = 4;
};

// Beats <-> seconds. Authored timing is stored in beats; seconds come from
// integrating 60 / bpm(beat) per segment, anchored so that beat 0 (the
// first downbeat) sits at offset_s + nudge_s.
//
// Conversion uses each curve's closed-form integral and its inverse, with
// the seconds at every segment start precomputed on edit, so a lookup is a
// binary search over segments plus one formula, exact to rounding.
class TempoMap {
public:
    // A constant tempo with beat 0 at offset_s.
    explicit TempoMap(double bpm = 120.0, double offset_s = 0.0);

    // Replaces the segments. They are sorted by start_beat; BPMs must be
    // positive and finite and start beats distinct, or std::invalid_argument
    // is thrown. Beats before the first segment use its bpm_start.
    void set_segments(std::vector<TempoSegment> segments);
    const std::vector<TempoSegment>& segments() const noexcept { return segments_; }

    // Replaces the meter changes. Sorted by bar; bars must be distinct and
    // >= 0, and a 4/4 at bar 0 is added when none starts there (bars before
    // 0, the pickup, use the bar 0 meter). Numerators must be 1..64 and
    // denominators a power of two up to 64.
    void set_time_signatures(std::vector<TimeSignature> signatures);
    const std::vector<TimeSignature>& time_signatures() const noexcept { return signatures_; }

    // Song offset: the time of the first downbeat, in seconds from file start.
    double offset() const noexcept { return offset_s_; }
    void set_offset(double seconds);

    // Fine alignment by ear, added to the offset. Kept separate so a re-draft
    // of the tempo map can keep the user's nudge.
    double nudge() const noexcept { return nudge_s_; }
    void set_nudge(double seconds);
    void nudge_by(double seconds) { set_nudge(nudge_s_ + seconds); }

    // Scales every BPM by `factor` (e.g. 1.002 for +0.2 %), keeping beat 0
    // where it is, for matching a slightly fast or slow grid by ear.
    void scale_tempo(double factor);

    double seconds_at(double beat) const noexcept;
    double beat_at(double seconds) const noexcept;
    double bpm_at(double beat) const noexcept;

    // Bar/beat/phase at a beat position, using the meter in force there.
    MusicalPosition position_at(double beat) const noexcept;
    // Beat position of the start of a bar.
    double bar_start_beat(int bar) const noexcept;
    // Meter in force at a beat position.
    const TimeSignature& time_signature_at(double beat) const noexcept;

private:
    void rebuild();
    // Seconds from beat 0 to `beat`, without offset or nudge.
    double integrate(double beat) const noexcept;

    std::vector<TempoSegment> segments_;
    std::vector<double> segment_seconds_;   // seconds from beat 0 to each segment start
    std::vector<TimeSignature> signatures_;
    std::vector<double> signature_beats_;   // beat position of each meter change
    double offset_s_ = 0.0;
    double nudge_s_ = 0.0;
};

struct TempoFitSettings {
    // A run of detected beats becomes one constant-tempo segment while every
    // beat inside it is within this many seconds of the straight line between
    // the run's end beats. Larger values give fewer segments.
    double tolerance_s = 0.010;
};

// Builds a tempo map from detected beats: beat `downbeat_phase` of the grid
// becomes beat 0, earlier beats are a pickup (negative beats), and runs of
// steady beats become single Step segments whose ends sit exactly on
// detected beats, so a drifting live tempo is followed beat by beat.
// The meter is grid.beats_per_bar / 4. Returns a 120 BPM map at offset 0 when
// the grid has fewer than two beats.
TempoMap tempo_map_from_beats(const analysis::BeatGrid& grid, const TempoFitSettings& settings = {});

} // namespace ztrip::time
