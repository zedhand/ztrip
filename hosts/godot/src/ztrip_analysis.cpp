#include "ztrip_analysis.h"

#include "convert.h"

#include "ztrip/io/wav.hpp"
#include "ztrip/serialize/sigcache.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <cstdio>
#include <exception>
#include <system_error>

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
    AnalysisOutcome out;
    try {
        const io::AudioBuffer audio = io::parse_wav(job.wav_bytes);
        const analysis::AnalysisSettings settings;
        const serialize::SigcacheKey key{analysis::audio_hash(audio), analysis::settings_hash(settings)};

        std::filesystem::path cache;
        if (!job.cache_dir.empty()) {
            char name[40];
            std::snprintf(name, sizeof name, "%016llx.sigcache", static_cast<unsigned long long>(key.audio_hash));
            cache = job.cache_dir / name;
            if (auto cached = serialize::read_sigcache(cache, key)) {
                out.result = std::make_shared<analysis::AnalysisResult>(std::move(*cached));
                out.from_cache = true;
                return out;
            }
        }
        out.result = std::make_shared<analysis::AnalysisResult>(analysis::analyze(audio, settings));
        if (!cache.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(job.cache_dir, ec);
            try {
                serialize::write_sigcache(cache, key, *out.result);
            } catch (const std::exception&) {
                // A cache we can't write only costs a re-analysis next time.
            }
        }
    } catch (const std::exception& e) {
        out.result.reset();
        out.error = job.label + ": " + e.what();
    }
    return out;
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
