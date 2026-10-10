#pragma once

#include "ztrip/analysis/onsets.hpp"

#include <span>
#include <vector>

namespace ztrip::analysis {

struct KickSettings {
    float cutoff_hz = 150.0f;  // 4th-order Butterworth low-pass before the envelope
    float window_ms = 10.0f;   // centred energy window
    float hop_ms = 2.5f;       // envelope step
    float lag_ms = 10.0f;      // a rise is measured over this span
    float min_rise_db = 9.0f;  // smallest level jump that counts as a kick
    float gate_db = 20.0f;     // the hit must peak within this much of the loud low end
    float range_db = 60.0f;    // levels below (loud low end - range) are treated as silence
    float min_gap_s = 0.05f;   // minimum time between kicks
};

// Finds kick drum hits in mono PCM. The signal is low-passed, its energy over
// a short centred window is taken in dB, and a kick is a sharp jump in that
// level: at least min_rise_db within lag_ms, peaking no more than gate_db
// below the track's loud low end (its 99th percentile). Measuring the jump in
// dB rather than as spectral flux lets a kick that lands on silence or a
// decayed tail stand out from a bass note entering over the previous kick's
// ring, and the time-domain envelope keeps timing to a few ms instead of an
// FFT hop. Times are the centre of the steepest rise; velocity is the hit's
// peak amplitude relative to the loud low end, clamped to 0..1.
std::vector<Trigger> detect_kicks(std::span<const float> mono, int sample_rate,
                                  const KickSettings& settings);

} // namespace ztrip::analysis
