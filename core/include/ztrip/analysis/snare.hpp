#pragma once

#include "ztrip/analysis/onsets.hpp"

#include <span>
#include <vector>

namespace ztrip::analysis {

struct SnareSettings {
    float window_ms = 10.0f;     // centred RMS window
    float hop_ms = 2.5f;         // envelope step
    float lag_ms = 15.0f;        // a jump is measured over this span
    float min_jump = 0.3f;       // smallest 1-4 kHz jump, as a fraction of the loudest 1-4 kHz level nearby
    float min_high_jump = 0.2f;  // the 4-10 kHz band must jump by this much too, within 7.5 ms
    float reference_s = 1.5f;    // "nearby": within this many seconds either side
    float floor_db = 30.0f;      // the nearby reference never drops below (loud end - floor_db)
    float gate_db = 4.0f;        // 1-4 kHz peak within this much of that band's loud end
    float high_gate_db = 6.0f;   // same for 4-10 kHz
    float body_gate_db = 6.0f;   // same for the 150-350 Hz drum body
    float max_decay_db = 15.0f;  // 1-4 kHz level 40-60 ms later may be at most this far below the peak
    float min_gap_s = 0.05f;     // minimum time between snares
};

// Finds snare (and clap) hits in mono PCM. A snare is a jump in 1-4 kHz
// amplitude, measured like the kick detector's jump (against the loudest
// level nearby), that comes with a jump in 4-10 kHz, peaks near the loud end
// of 1-4 kHz, 4-10 kHz and the 150-350 Hz body, and is still ringing 40-60 ms
// later. The ring (the snare wires) is what sets it apart from a kick's
// beater click, which is gone within a few ms; the body and the 1-4 kHz
// level set it apart from hi-hats. Times are half a lag before the jump's
// peak; velocity is the 1-4 kHz peak relative to that band's loud end.
std::vector<Trigger> detect_snares(std::span<const float> mono, int sample_rate,
                                   const SnareSettings& settings);

} // namespace ztrip::analysis
