#pragma once

#include "ztrip/analysis/bands.hpp"
#include "ztrip/analysis/envelope.hpp"
#include "ztrip/analysis/offline.hpp"
#include "ztrip/analysis/onsets.hpp"
#include "ztrip/analysis/stft.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ztrip::analysis {

struct LiveSettings {
    // A smaller FFT than offline: the window is what the analysis waits for,
    // so 1024 at 48 kHz (hop 256) keeps the added latency near 10 ms.
    StftSettings stft{1024};
    std::vector<Band> bands = default_bands();
    EnvelopeSettings envelope;
    // As offline, except that window_s is a trailing window (no lookahead).
    OnsetSettings onsets;
    float gain_db = 0.0f;
    // Levels are normalised to 0..1 between a running floor and peak, which
    // jump to a new extreme at once and drift back towards the current level
    // with this time constant, so a quiet song after a loud one comes up again.
    float norm_release_s = 10.0f;
    float range_db = 60.0f;     // the floor stays within this of the peak
    float min_range_db = 12.0f; // and at least this far below it
    float min_peak_db = -60.0f; // the peak never drops below this, so silence reads 0
    double signal_rate_hz = 120.0;
};

// Causal analysis of a live audio stream. Audio is pushed in blocks of any
// size; every hop of new samples analyses the last fft_size samples and
// appends to result(), which has the same signal and event names as the
// offline analyze() (band.<Band>, env.<Band>, rms, centroid, onset.<Band>,
// onset.full), so a graph switches from a file to live input by rebinding a
// slot. Beat, downbeat and kick lists exist but stay empty for now.
//
// Times are on the stream's sample clock (sample count / sample rate) and
// every value and event is stamped when it becomes known, never earlier, so
// a live evaluator that has evaluated up to time() never misses an event that
// arrives later. The output depends only on the samples, not on how they were
// split into blocks.
//
// Not thread safe: push from the thread that evaluates the graph (feed it
// from a capture thread through a ring buffer). Signal buffers and event
// lists keep their addresses while they grow, so nodes bound to the result
// stay valid. They grow by about 7 KB per second of audio.
class LiveAnalyzer {
public:
    // Throws std::invalid_argument for bad STFT settings or sample rate.
    LiveAnalyzer(int sample_rate, LiveSettings settings = {});

    void push(std::span<const float> mono);
    // Appends silence, e.g. for samples a capture ring had to drop, so the
    // clock stays on the audio device's.
    void push_silence(std::size_t samples);

    const AnalysisResult& result() const noexcept { return result_; }
    // How far the result reaches: the time of the latest analysed frame. Every
    // signal tick up to here exists, so evaluate a live graph at this time.
    double time() const noexcept { return known_time_; }
    std::uint64_t samples() const noexcept { return samples_; }
    int sample_rate() const noexcept { return sample_rate_; }
    const LiveSettings& settings() const noexcept { return settings_; }

    // Back to time 0 with empty buffers.
    void reset();

private:
    // Running floor and peak of a level in dB, mapping it to 0..1.
    struct Normalizer {
        float lo = 0.0f, hi = 0.0f;
        float next(float db, const LiveSettings& s, float release_k);
    };

    // Causal onset picking over one flux curve.
    struct OnsetPicker {
        std::vector<float> history; // raw flux over the trailing window, circular
        std::size_t pos = 0, count = 0;
        float scale = 0.0f;
        float prev = 0.0f, prev2 = 0.0f;
        double last_onset = -1e9;
        std::vector<float> scratch;
    };

    void process_frame();
    void emit_ticks(double frame_time, std::span<const float> levels);
    void pick(OnsetPicker& p, float flux, double frame_time, EventList& out);
    void init_state();

    int sample_rate_;
    LiveSettings settings_;
    FrameTransform transform_;
    int hop_;
    double frame_rate_;
    float gain_;
    float release_k_; // per-frame drift factor of the normalisers

    std::vector<float> window_;   // last fft_size samples, circular
    std::size_t window_pos_ = 0;
    std::vector<float> frame_;    // window_ unrolled, oldest first
    std::vector<float> magnitude_;
    std::uint64_t samples_ = 0;
    int since_frame_ = 0;
    double known_time_ = 0.0;

    std::vector<BinRange> band_bins_;
    std::vector<BandFlux> band_flux_;
    BandFlux full_flux_;
    std::vector<Normalizer> band_norm_;
    Normalizer rms_norm_;
    std::vector<EnvelopeFollower> env_;
    std::vector<OnsetPicker> band_onsets_;
    OnsetPicker full_onsets_;

    // Per-frame levels (band levels, rms, centroid) of the previous frame,
    // for interpolating onto the signal-rate grid.
    std::vector<float> prev_levels_, levels_;
    double prev_frame_time_ = -1.0;
    std::uint64_t next_tick_ = 0;

    AnalysisResult result_;
    // Indices into result_.signals and result_.events.
    std::vector<std::size_t> band_signal_, env_signal_, band_events_;
    std::size_t rms_signal_ = 0, centroid_signal_ = 0, full_events_ = 0;
};

} // namespace ztrip::analysis
