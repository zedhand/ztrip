#include "ztrip_analysis.h"

#include "convert.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <algorithm>
#include <cmath>

namespace ztrip::godot_host {

bool prepare_analysis(const godot::String& path, bool use_cache, AnalysisJob& job, godot::String& error) {
    if (path.is_empty()) {
        error = "No audio file set.";
        return false;
    }
    if (!godot::FileAccess::file_exists(path)) {
        error = "Audio file not found: " + path;
        return false;
    }
    const godot::PackedByteArray bytes = godot::FileAccess::get_file_as_bytes(path);
    if (bytes.is_empty()) {
        error = "Could not read audio file: " + path;
        return false;
    }
    job.wav_bytes.assign(bytes.ptr(), bytes.ptr() + bytes.size());
    job.cache_dir = use_cache ? to_native_path("user://ztrip_cache") : std::filesystem::path{};
    job.label = to_std(path);
    return true;
}

AnalysisOutcome run_analysis(const AnalysisJob& job) {
    return host::analyze_wav(job.wav_bytes, job.cache_dir, job.label);
}

} // namespace ztrip::godot_host

namespace godot {

using ztrip::godot_host::to_godot;
using ztrip::godot_host::to_std;

void ZtripAnalysis::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load_file", "path", "use_cache"), &ZtripAnalysis::load_file, DEFVAL(true));
    ClassDB::bind_method(D_METHOD("is_loaded"), &ZtripAnalysis::is_loaded);
    ClassDB::bind_method(D_METHOD("is_from_cache"), &ZtripAnalysis::is_from_cache);
    ClassDB::bind_method(D_METHOD("get_error"), &ZtripAnalysis::get_error);
    ClassDB::bind_method(D_METHOD("get_path"), &ZtripAnalysis::get_path);
    ClassDB::bind_method(D_METHOD("get_duration"), &ZtripAnalysis::get_duration);
    ClassDB::bind_method(D_METHOD("get_bpm"), &ZtripAnalysis::get_bpm);
    ClassDB::bind_method(D_METHOD("get_beats_per_bar"), &ZtripAnalysis::get_beats_per_bar);
    ClassDB::bind_method(D_METHOD("get_signal_names"), &ZtripAnalysis::get_signal_names);
    ClassDB::bind_method(D_METHOD("get_event_names"), &ZtripAnalysis::get_event_names);
    ClassDB::bind_method(D_METHOD("sample", "signal", "time"), &ZtripAnalysis::sample);
    ClassDB::bind_method(D_METHOD("get_signal_peaks", "signal", "t0", "t1", "count"),
                         &ZtripAnalysis::get_signal_peaks);
    ClassDB::bind_method(D_METHOD("get_event_times", "events"), &ZtripAnalysis::get_event_times);
    ClassDB::bind_method(D_METHOD("get_event_velocities", "events"), &ZtripAnalysis::get_event_velocities);
    ClassDB::bind_method(D_METHOD("get_beats"), &ZtripAnalysis::get_beats);
    ClassDB::bind_method(D_METHOD("get_downbeats"), &ZtripAnalysis::get_downbeats);
}

Error ZtripAnalysis::load_file(const String& path, bool use_cache) {
    ztrip::godot_host::AnalysisJob job;
    error_ = String();
    if (!ztrip::godot_host::prepare_analysis(path, use_cache, job, error_)) return ERR_FILE_CANT_OPEN;
    auto outcome = ztrip::godot_host::run_analysis(job);
    if (!outcome.result) {
        error_ = to_godot(outcome.error);
        return ERR_PARSE_ERROR;
    }
    set_result(std::move(outcome.result), path, outcome.from_cache);
    return OK;
}

void ZtripAnalysis::set_result(std::shared_ptr<const ztrip::analysis::AnalysisResult> result, const String& path,
                               bool from_cache) {
    result_ = std::move(result);
    path_ = path;
    from_cache_ = from_cache;
}

double ZtripAnalysis::get_duration() const { return result_ ? result_->duration_s : 0.0; }
double ZtripAnalysis::get_bpm() const { return result_ ? result_->beats.bpm : 0.0; }
int ZtripAnalysis::get_beats_per_bar() const { return result_ ? result_->beats.beats_per_bar : 4; }

PackedStringArray ZtripAnalysis::get_signal_names() const {
    PackedStringArray names;
    if (result_)
        for (const auto& s : result_->signals) names.push_back(to_godot(s.name));
    return names;
}

PackedStringArray ZtripAnalysis::get_event_names() const {
    PackedStringArray names;
    if (result_)
        for (const auto& e : result_->events) names.push_back(to_godot(e.name));
    return names;
}

double ZtripAnalysis::sample(const String& signal, double t) const {
    if (!result_) return 0.0;
    const auto* s = result_->find_signal(to_std(signal));
    return s ? result_->sample(*s, t) : 0.0;
}

PackedFloat32Array ZtripAnalysis::get_signal_peaks(const String& signal, double t0, double t1, int count) const {
    PackedFloat32Array out;
    if (count <= 0 || !(t1 > t0)) return out;
    out.resize(count);
    out.fill(0.0f);
    if (!result_) return out;
    const auto* s = result_->find_signal(to_std(signal));
    if (!s || s->values.empty()) return out;
    const double rate = result_->signal_rate_hz;
    const auto n = static_cast<int64_t>(s->values.size());
    const double slice = (t1 - t0) / count;
    for (int i = 0; i < count; ++i) {
        const double a = t0 + i * slice;
        // Each slice covers at least one sample, so zoomed-in views interpolate.
        auto first = static_cast<int64_t>(std::floor(a * rate));
        auto last = static_cast<int64_t>(std::ceil((a + slice) * rate));
        if (last < 0 || first >= n) continue;
        first = std::max<int64_t>(first, 0);
        last = std::min<int64_t>(std::max(last, first + 1), n);
        float peak = 0.0f;
        for (int64_t k = first; k < last; ++k) peak = std::max(peak, s->values[static_cast<std::size_t>(k)]);
        out.set(i, peak);
    }
    return out;
}

PackedFloat64Array ZtripAnalysis::get_event_times(const String& events) const {
    PackedFloat64Array out;
    if (!result_) return out;
    if (const auto* list = result_->find_events(to_std(events)))
        for (const auto& e : list->events) out.push_back(e.time);
    return out;
}

PackedFloat32Array ZtripAnalysis::get_event_velocities(const String& events) const {
    PackedFloat32Array out;
    if (!result_) return out;
    if (const auto* list = result_->find_events(to_std(events)))
        for (const auto& e : list->events) out.push_back(e.velocity);
    return out;
}

PackedFloat64Array ZtripAnalysis::get_beats() const {
    PackedFloat64Array out;
    if (result_)
        for (double t : result_->beats.beats) out.push_back(t);
    return out;
}

PackedFloat64Array ZtripAnalysis::get_downbeats() const {
    PackedFloat64Array out;
    if (result_)
        for (double t : result_->beats.downbeats) out.push_back(t);
    return out;
}

} // namespace godot
