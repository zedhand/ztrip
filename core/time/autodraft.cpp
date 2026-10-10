#include "ztrip/time/autodraft.hpp"

#include "ztrip/analysis/offline.hpp"
#include "ztrip/time/tempo_map.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ztrip::time {

Lane draft_lane(std::span<const analysis::Trigger> events, const TempoMap& tempo, const DraftSettings& settings,
                std::string name) {
    Lane lane;
    lane.name = std::move(name);
    float floor = 0.0f;
    if (settings.ghost_ratio > 0.0 && !events.empty()) {
        std::vector<float> v;
        v.reserve(events.size());
        for (const auto& e : events) v.push_back(e.velocity);
        const auto mid = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
        std::nth_element(v.begin(), mid, v.end());
        floor = static_cast<float>(settings.ghost_ratio) * *mid;
    }
    for (const analysis::Trigger& e : events) {
        if (e.velocity < floor) continue;
        const double beat = tempo.beat_at(e.time);
        const double step = grid_step_beats(settings.grid, tempo.time_signature_at(beat).denominator);
        double placed = snap(beat, step, settings.quantize);
        if (std::abs(tempo.seconds_at(placed) - e.time) > settings.max_shift_s) placed = beat;

        Note note;
        note.start_beat = placed;
        note.length_beats = std::max(settings.length_steps, 0.0) * step;
        note.velocity = std::clamp(e.velocity, 0.0f, 1.0f);
        // Same grid step as the previous note: keep the stronger one.
        if (!lane.notes.empty() && std::abs(placed - lane.notes.back().start_beat) < 0.5 * step) {
            if (note.velocity > lane.notes.back().velocity) lane.notes.back() = note;
            continue;
        }
        lane.notes.push_back(note);
    }
    lane.sort();
    return lane;
}

NoteSequence auto_draft(const analysis::AnalysisResult& analysis, const TempoMap& tempo,
                        const DraftSettings& settings) {
    NoteSequence seq;
    seq.name = "auto-draft";
    for (const DraftLaneSpec& spec : settings.lanes) {
        std::vector<analysis::Trigger> events;
        const auto* list = analysis.find_events(spec.events);
        if (!list && !spec.fallback.empty()) list = analysis.find_events(spec.fallback);
        if (list) {
            for (const auto& e : list->events) {
                if (e.velocity >= spec.min_velocity) events.push_back(e);
            }
        }
        seq.lanes.push_back(draft_lane(events, tempo, settings, spec.lane));
    }
    return seq;
}

} // namespace ztrip::time
