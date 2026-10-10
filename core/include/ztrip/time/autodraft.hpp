#pragma once

#include "ztrip/analysis/onsets.hpp"
#include "ztrip/time/note_sequence.hpp"

#include <span>
#include <string>
#include <vector>

namespace ztrip::analysis {
struct AnalysisResult;
}

namespace ztrip::time {

class TempoMap;

// Which detected events become which lane.
struct DraftLaneSpec {
    std::string lane;          // e.g. "Kick"
    std::string events;        // AnalysisResult event list, e.g. "onset.Sub"
    float min_velocity = 0.0f; // weaker events are dropped
};

struct DraftSettings {
    GridDivision grid{16, false};
    // 0 keeps detected times, 1 puts notes on the grid.
    double quantize = 1.0;
    // A hit moves to the grid only when the grid point is within this many
    // seconds of it, so a stretch where the beat grid is off keeps hits where
    // they were heard instead of snapping them a 16th away.
    double max_shift_s = 0.035;
    // Note length in grid steps.
    double length_steps = 1.0;
    // Events weaker than this fraction of the lane's median event velocity
    // are dropped as ghosts (bass bleeding into the kick band, hat spill),
    // so a draft holds the hits a listener would mark. 0 keeps everything.
    double ghost_ratio = 0.3;
    // Lanes to draft; the default is the drum kit's low end.
    std::vector<DraftLaneSpec> lanes = {{"Kick", "onset.Sub", 0.0f}};
};

// Drafts one lane from events: ghosts are dropped (see ghost_ratio), each
// event's time becomes a beat through the tempo map and is quantized to the
// grid (see DraftSettings), and events that land in the same grid step keep
// only the strongest. Velocity comes from the event.
Lane draft_lane(std::span<const analysis::Trigger> events, const TempoMap& tempo, const DraftSettings& settings,
                std::string name);

// Drafts every lane in settings.lanes from the analysis result's event
// lists; lanes whose events are missing come out empty.
NoteSequence auto_draft(const analysis::AnalysisResult& analysis, const TempoMap& tempo,
                        const DraftSettings& settings = {});

} // namespace ztrip::time
