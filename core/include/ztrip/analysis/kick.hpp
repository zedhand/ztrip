#pragma once

#include "ztrip/analysis/onsets.hpp"

#include <span>
#include <vector>

namespace ztrip::analysis {

struct KickSettings {
    float cutoff_hz = 150.0f;   // 4th-order Butterworth low-pass before the envelope
    float window_ms = 10.0f;    // centred RMS window
    float hop_ms = 2.5f;        // envelope step
    float lag_ms = 20.0f;       // a jump is measured over this span
    float min_jump = 0.4f;      // smallest jump in low-band amplitude, as a fraction of the loudest level nearby
    float reference_s = 1.5f;   // "nearby": within this many seconds either side
    float gate_db = 5.0f;       // the hit must peak within this much of the loud low end
    float floor_db = 30.0f;     // the nearby reference never drops below (loud low end - floor_db)
    float min_gap_s = 0.06f;    // a weaker hit closer than this to a stronger one is dropped
};

// Finds kick drum hits in mono PCM. The signal is low-passed and its RMS
// amplitude taken over a short centred window. A kick is a jump in that
// amplitude over lag_ms of at least min_jump times the loudest low-band level
// within reference_s, peaking no more than gate_db below the track's loud low
// end (its 99th percentile); of hits closer than min_gap_s, the stronger
// jump wins. Measuring the jump in amplitude against the loudest level nearby,
// rather than in dB, keeps a kick that lands on a sustained or sidechained
// bass (a small jump in dB, a large one in amplitude) and drops a quieter bass
// pluck or snare body that rises from silence (a large jump in dB, a small
// one next to the kicks). Each hit is then timed at the steepest rise of the
// envelope in dB over 5 ms, sampled every 0.5 ms from 20 ms before to 10 ms
// after the coarse jump, so timing holds to a few ms. Velocity is the hit's peak amplitude relative to
// the loud low end, clamped to 0..1.
std::vector<Trigger> detect_kicks(std::span<const float> mono, int sample_rate,
                                  const KickSettings& settings);

} // namespace ztrip::analysis
