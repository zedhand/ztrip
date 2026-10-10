// ZtripAnalysis: offline analysis of a WAV file, as a Godot object.
#pragma once

#include "ztrip/analysis/offline.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ztrip::godot_host {

// Inputs for one analysis run, gathered on the main thread so the run itself
// makes no Godot calls and can go to a worker thread.
struct AnalysisJob {
    std::vector<std::uint8_t> wav_bytes;
    std::filesystem::path cache_dir; // empty for no cache
    std::string label;               // the file, for messages
};

struct AnalysisOutcome {
    std::shared_ptr<const analysis::AnalysisResult> result; // null on failure
    bool from_cache = false;
    std::string error;
};

// Reads the file (res://, user:// or absolute) on the calling thread. Returns
// false with `error` set when it can't be read.
bool prepare_analysis(const godot::String& path, bool use_cache, AnalysisJob& job, godot::String& error);

// Decodes and analyses, reusing user://ztrip_cache/<audio hash>.sigcache when
// the audio and settings match. Thread-safe.
AnalysisOutcome run_analysis(const AnalysisJob& job);

} // namespace ztrip::godot_host

namespace godot {

class ZtripAnalysis : public RefCounted {
    GDCLASS(ZtripAnalysis, RefCounted)

public:
    // Analyses a WAV file now, on this thread. Returns OK or an error code;
    // get_error() has the message.
    Error load_file(const String& path, bool use_cache = true);

    bool is_loaded() const { return result_ != nullptr; }
    bool is_from_cache() const { return from_cache_; }
    String get_error() const { return error_; }
    String get_path() const { return path_; }

    double get_duration() const;
    double get_bpm() const;
    int get_beats_per_bar() const;
    PackedStringArray get_signal_names() const;
    PackedStringArray get_event_names() const;
    // A Continuous signal (e.g. "env.Bass", "rms") at time t; 0 if unknown.
    double sample(const String& signal, double t) const;
    PackedFloat64Array get_event_times(const String& events) const;
    PackedFloat32Array get_event_velocities(const String& events) const;
    PackedFloat64Array get_beats() const;
    PackedFloat64Array get_downbeats() const;

    void set_result(std::shared_ptr<const ztrip::analysis::AnalysisResult> result, const String& path, bool from_cache);
    const std::shared_ptr<const ztrip::analysis::AnalysisResult>& result() const { return result_; }

protected:
    static void _bind_methods();

private:
    std::shared_ptr<const ztrip::analysis::AnalysisResult> result_;
    String path_;
    String error_;
    bool from_cache_ = false;
};

} // namespace godot
