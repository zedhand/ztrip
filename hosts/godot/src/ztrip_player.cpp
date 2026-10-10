#include "ztrip_player.h"

#include "convert.h"

#include "ztrip/graph/compiler.hpp"
#include "ztrip/serialize/controls.hpp"
#include "ztrip/serialize/mgraph.hpp"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <span>
#include <stdexcept>

namespace godot {

using ztrip::godot_host::to_godot;
using ztrip::godot_host::to_std;

namespace {

constexpr double kGraphPollInterval = 0.5; // seconds between checks for an edited graph file
constexpr double kBackJitter = 0.1;        // audio clock jitter ignored while playing, seconds

bool in_editor() { return Engine::get_singleton()->is_editor_hint(); }

} // namespace

ZtripPlayer::ZtripPlayer() {
    timeline_.instantiate();
    timeline_->connect("changed", callable_mp(this, &ZtripPlayer::on_timeline_changed));
}

ZtripPlayer::~ZtripPlayer() {
    if (pending_.valid()) pending_.wait();
}

void ZtripPlayer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_audio_player", "path"), &ZtripPlayer::set_audio_player);
    ClassDB::bind_method(D_METHOD("get_audio_player"), &ZtripPlayer::get_audio_player);
    ClassDB::bind_method(D_METHOD("set_audio_file", "path"), &ZtripPlayer::set_audio_file);
    ClassDB::bind_method(D_METHOD("get_audio_file"), &ZtripPlayer::get_audio_file);
    ClassDB::bind_method(D_METHOD("set_graph_file", "path"), &ZtripPlayer::set_graph_file);
    ClassDB::bind_method(D_METHOD("get_graph_file"), &ZtripPlayer::get_graph_file);
    ClassDB::bind_method(D_METHOD("set_tempo_file", "path"), &ZtripPlayer::set_tempo_file);
    ClassDB::bind_method(D_METHOD("get_tempo_file"), &ZtripPlayer::get_tempo_file);
    ClassDB::bind_method(D_METHOD("set_sequence_file", "path"), &ZtripPlayer::set_sequence_file);
    ClassDB::bind_method(D_METHOD("get_sequence_file"), &ZtripPlayer::get_sequence_file);
    ClassDB::bind_method(D_METHOD("set_target_root", "path"), &ZtripPlayer::set_target_root);
    ClassDB::bind_method(D_METHOD("get_target_root"), &ZtripPlayer::get_target_root);
    ClassDB::bind_method(D_METHOD("set_active", "active"), &ZtripPlayer::set_active);
    ClassDB::bind_method(D_METHOD("is_active"), &ZtripPlayer::is_active);
    ClassDB::bind_method(D_METHOD("set_playing", "playing"), &ZtripPlayer::set_playing);
    ClassDB::bind_method(D_METHOD("is_playing"), &ZtripPlayer::is_playing);
    ClassDB::bind_method(D_METHOD("set_time", "time"), &ZtripPlayer::set_time);
    ClassDB::bind_method(D_METHOD("get_time"), &ZtripPlayer::get_time);
    ClassDB::bind_method(D_METHOD("set_autoplay", "autoplay"), &ZtripPlayer::set_autoplay);
    ClassDB::bind_method(D_METHOD("has_autoplay"), &ZtripPlayer::has_autoplay);
    ClassDB::bind_method(D_METHOD("set_threaded_loading", "threaded"), &ZtripPlayer::set_threaded_loading);
    ClassDB::bind_method(D_METHOD("is_threaded_loading"), &ZtripPlayer::is_threaded_loading);
    ClassDB::bind_method(D_METHOD("set_use_cache", "use_cache"), &ZtripPlayer::set_use_cache);
    ClassDB::bind_method(D_METHOD("is_using_cache"), &ZtripPlayer::is_using_cache);
    ClassDB::bind_method(D_METHOD("get_status"), &ZtripPlayer::get_status);
    ClassDB::bind_method(D_METHOD("set_live_input", "live"), &ZtripPlayer::set_live_input);
    ClassDB::bind_method(D_METHOD("is_live_input"), &ZtripPlayer::is_live_input);
    ClassDB::bind_method(D_METHOD("set_live_device", "device"), &ZtripPlayer::set_live_device);
    ClassDB::bind_method(D_METHOD("get_live_device"), &ZtripPlayer::get_live_device);
    ClassDB::bind_method(D_METHOD("set_live_loopback", "loopback"), &ZtripPlayer::set_live_loopback);
    ClassDB::bind_method(D_METHOD("is_live_loopback"), &ZtripPlayer::is_live_loopback);
    ClassDB::bind_method(D_METHOD("set_live_simulate_file", "path"), &ZtripPlayer::set_live_simulate_file);
    ClassDB::bind_method(D_METHOD("get_live_simulate_file"), &ZtripPlayer::get_live_simulate_file);
    ClassDB::bind_method(D_METHOD("set_live_gain_db", "gain"), &ZtripPlayer::set_live_gain_db);
    ClassDB::bind_method(D_METHOD("get_live_gain_db"), &ZtripPlayer::get_live_gain_db);
    ClassDB::bind_method(D_METHOD("is_live_running"), &ZtripPlayer::is_live_running);
    ClassDB::bind_static_method("ZtripPlayer", D_METHOD("get_live_devices"), &ZtripPlayer::get_live_devices);
    ClassDB::bind_method(D_METHOD("set_midi_input", "on"), &ZtripPlayer::set_midi_input);
    ClassDB::bind_method(D_METHOD("is_midi_input"), &ZtripPlayer::is_midi_input);
    ClassDB::bind_method(D_METHOD("set_midi_port", "port"), &ZtripPlayer::set_midi_port);
    ClassDB::bind_method(D_METHOD("get_midi_port"), &ZtripPlayer::get_midi_port);
    ClassDB::bind_method(D_METHOD("set_osc_input", "on"), &ZtripPlayer::set_osc_input);
    ClassDB::bind_method(D_METHOD("is_osc_input"), &ZtripPlayer::is_osc_input);
    ClassDB::bind_method(D_METHOD("set_osc_port", "port"), &ZtripPlayer::set_osc_port);
    ClassDB::bind_method(D_METHOD("get_osc_port"), &ZtripPlayer::get_osc_port);
    ClassDB::bind_method(D_METHOD("set_dmx_input", "protocol"), &ZtripPlayer::set_dmx_input);
    ClassDB::bind_method(D_METHOD("get_dmx_input"), &ZtripPlayer::get_dmx_input);
    ClassDB::bind_method(D_METHOD("set_dmx_port", "port"), &ZtripPlayer::set_dmx_port);
    ClassDB::bind_method(D_METHOD("get_dmx_port"), &ZtripPlayer::get_dmx_port);
    ClassDB::bind_method(D_METHOD("set_dmx_universes", "universes"), &ZtripPlayer::set_dmx_universes);
    ClassDB::bind_method(D_METHOD("get_dmx_universes"), &ZtripPlayer::get_dmx_universes);
    ClassDB::bind_static_method("ZtripPlayer", D_METHOD("get_midi_ports"), &ZtripPlayer::get_midi_ports);
    ClassDB::bind_method(D_METHOD("set_control_bindings", "bindings"), &ZtripPlayer::set_control_bindings);
    ClassDB::bind_method(D_METHOD("get_control_bindings"), &ZtripPlayer::get_control_bindings);
    ClassDB::bind_method(D_METHOD("bind_control", "macro", "control", "min", "max"), &ZtripPlayer::bind_control);
    ClassDB::bind_method(D_METHOD("unbind_control", "macro"), &ZtripPlayer::unbind_control);
    ClassDB::bind_method(D_METHOD("learn_macro", "macro"), &ZtripPlayer::learn_macro);
    ClassDB::bind_method(D_METHOD("cancel_learn"), &ZtripPlayer::cancel_learn);
    ClassDB::bind_method(D_METHOD("is_learning"), &ZtripPlayer::is_learning);
    ClassDB::bind_method(D_METHOD("load_controls", "path"), &ZtripPlayer::load_controls);
    ClassDB::bind_method(D_METHOD("save_controls", "path"), &ZtripPlayer::save_controls);
    ClassDB::bind_method(D_METHOD("get_control_value", "control"), &ZtripPlayer::get_control_value);
    ClassDB::bind_method(D_METHOD("get_control_names"), &ZtripPlayer::get_control_names);
    ClassDB::bind_method(D_METHOD("inject_control", "control", "value", "fire"), &ZtripPlayer::inject_control);

    ClassDB::bind_method(D_METHOD("play", "from"), &ZtripPlayer::play, DEFVAL(-1.0));
    ClassDB::bind_method(D_METHOD("stop"), &ZtripPlayer::stop);
    ClassDB::bind_method(D_METHOD("seek", "time"), &ZtripPlayer::seek);
    ClassDB::bind_method(D_METHOD("reload"), &ZtripPlayer::reload);
    ClassDB::bind_method(D_METHOD("reload_graph"), &ZtripPlayer::reload_graph);
    ClassDB::bind_method(D_METHOD("wait_until_loaded"), &ZtripPlayer::wait_until_loaded);
    ClassDB::bind_method(D_METHOD("update_now"), &ZtripPlayer::update_now);
    ClassDB::bind_method(D_METHOD("is_ready"), &ZtripPlayer::is_ready);
    ClassDB::bind_method(D_METHOD("is_loading"), &ZtripPlayer::is_loading);
    ClassDB::bind_method(D_METHOD("get_duration"), &ZtripPlayer::get_duration);
    ClassDB::bind_method(D_METHOD("get_bpm"), &ZtripPlayer::get_bpm);
    ClassDB::bind_method(D_METHOD("get_analysis"), &ZtripPlayer::get_analysis);
    ClassDB::bind_method(D_METHOD("get_timeline"), &ZtripPlayer::get_timeline);
    ClassDB::bind_method(D_METHOD("reload_timeline"), &ZtripPlayer::reload_timeline);
    ClassDB::bind_method(D_METHOD("draft_timeline", "grid"), &ZtripPlayer::draft_timeline, DEFVAL("1/16"));
    ClassDB::bind_method(D_METHOD("save_timeline"), &ZtripPlayer::save_timeline);
    ClassDB::bind_method(D_METHOD("get_resolved_audio_file"), &ZtripPlayer::get_resolved_audio_file);
    ClassDB::bind_method(D_METHOD("get_output", "target"), &ZtripPlayer::get_output);
    ClassDB::bind_method(D_METHOD("get_outputs"), &ZtripPlayer::get_outputs);
    ClassDB::bind_method(D_METHOD("get_port_value", "node", "port"), &ZtripPlayer::get_port_value);
    ClassDB::bind_method(D_METHOD("get_macro_names"), &ZtripPlayer::get_macro_names);
    ClassDB::bind_method(D_METHOD("set_macro", "name", "value"), &ZtripPlayer::set_macro);
    ClassDB::bind_method(D_METHOD("get_macro", "name"), &ZtripPlayer::get_macro);
    ClassDB::bind_method(D_METHOD("get_binding_problems"), &ZtripPlayer::get_binding_problems);
    ClassDB::bind_method(D_METHOD("get_bound_count"), &ZtripPlayer::get_bound_count);

    ADD_GROUP("Sources", "");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "audio_player", PROPERTY_HINT_NODE_PATH_VALID_TYPES,
                              "AudioStreamPlayer,AudioStreamPlayer2D,AudioStreamPlayer3D"),
                 "set_audio_player", "get_audio_player");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "audio_file", PROPERTY_HINT_FILE, "*.wav"), "set_audio_file",
                 "get_audio_file");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "graph_file", PROPERTY_HINT_FILE, "*.mgraph.json"),
                 "set_graph_file", "get_graph_file");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "target_root"), "set_target_root", "get_target_root");
    ADD_GROUP("Timeline", "");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "tempo_file", PROPERTY_HINT_SAVE_FILE, "*.tempo.json"),
                 "set_tempo_file", "get_tempo_file");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "sequence_file", PROPERTY_HINT_SAVE_FILE, "*.nseq.json"),
                 "set_sequence_file", "get_sequence_file");
    ADD_GROUP("Live input", "live_");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "live_input"), "set_live_input", "is_live_input");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "live_device", PROPERTY_HINT_PLACEHOLDER_TEXT, "default input"),
                 "set_live_device", "get_live_device");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "live_loopback"), "set_live_loopback", "is_live_loopback");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "live_simulate_file", PROPERTY_HINT_FILE, "*.wav"),
                 "set_live_simulate_file", "get_live_simulate_file");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "live_gain_db", PROPERTY_HINT_RANGE, "-24,24,0.5,suffix:dB"),
                 "set_live_gain_db", "get_live_gain_db");
    ADD_GROUP("Controls", "");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "midi_input"), "set_midi_input", "is_midi_input");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "midi_port", PROPERTY_HINT_PLACEHOLDER_TEXT, "first port"),
                 "set_midi_port", "get_midi_port");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "osc_input"), "set_osc_input", "is_osc_input");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "osc_port", PROPERTY_HINT_RANGE, "1,65535"), "set_osc_port",
                 "get_osc_port");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "dmx_input", PROPERTY_HINT_ENUM, "Off,Art-Net,sACN"), "set_dmx_input",
                 "get_dmx_input");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "dmx_port", PROPERTY_HINT_RANGE, "0,65535"), "set_dmx_port",
                 "get_dmx_port");
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "dmx_universes"), "set_dmx_universes",
                 "get_dmx_universes");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "control_bindings"), "set_control_bindings", "get_control_bindings");
    ADD_GROUP("Playback", "");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "active"), "set_active", "is_active");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "playing", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR),
                 "set_playing", "is_playing");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time", PROPERTY_HINT_RANGE, "0,600,0.01,or_greater,suffix:s"),
                 "set_time", "get_time");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autoplay"), "set_autoplay", "has_autoplay");
    ADD_GROUP("Loading", "");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "threaded_loading"), "set_threaded_loading", "is_threaded_loading");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_cache"), "set_use_cache", "is_using_cache");
    ADD_GROUP("", "");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "status", PROPERTY_HINT_MULTILINE_TEXT, "",
                              PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY),
                 "", "get_status");

    ADD_SIGNAL(MethodInfo("analysis_loaded"));
    ADD_SIGNAL(MethodInfo("graph_loaded"));
    ADD_SIGNAL(MethodInfo("timeline_loaded"));
    ADD_SIGNAL(MethodInfo("finished"));
    ADD_SIGNAL(MethodInfo("control_learned", PropertyInfo(Variant::STRING, "macro"),
                          PropertyInfo(Variant::STRING, "control")));
}

// ---- Properties --------------------------------------------------------------

void ZtripPlayer::set_audio_player(const NodePath& path) {
    if (path == audio_player_) return;
    audio_player_ = path;
    if (is_node_ready() && audio_file_.is_empty()) start_analysis();
    update_configuration_warnings();
}

void ZtripPlayer::set_audio_file(const String& path) {
    if (path == audio_file_) return;
    audio_file_ = path;
    if (is_node_ready()) start_analysis();
    update_configuration_warnings();
}

void ZtripPlayer::set_graph_file(const String& path) {
    if (path == graph_file_) return;
    graph_file_ = path;
    if (is_node_ready()) reload_graph();
    update_configuration_warnings();
}

void ZtripPlayer::set_tempo_file(const String& path) {
    if (path == tempo_file_) return;
    tempo_file_ = path;
    // A new path with no file yet keeps the timeline, ready to be saved there.
    if (is_node_ready() && FileAccess::file_exists(path)) reload_timeline();
}

void ZtripPlayer::set_sequence_file(const String& path) {
    if (path == sequence_file_) return;
    sequence_file_ = path;
    if (is_node_ready() && FileAccess::file_exists(path)) reload_timeline();
}

void ZtripPlayer::set_target_root(const NodePath& path) {
    if (path == target_root_) return;
    target_root_ = path;
    binder_.restore();
    applied_ = false;
    if (is_node_ready()) evaluate_and_apply();
}

void ZtripPlayer::set_active(bool active) {
    if (active == active_) return;
    active_ = active;
    if (!active_) {
        binder_.restore();
        applied_ = false;
    } else if (is_node_ready()) {
        evaluate_and_apply();
    }
}

void ZtripPlayer::set_playing(bool playing) {
    if (playing) {
        play();
    } else {
        stop();
    }
}

void ZtripPlayer::set_time(double t) { seek(t); }

String ZtripPlayer::get_status() const {
    PackedStringArray lines;
    if (live_input_) {
        if (!live_error_.is_empty()) {
            lines.push_back("Live: " + live_error_);
        } else if (live_) {
            lines.push_back(String("Live: {0} at {1} Hz, {2} s").format(Array::make(
                to_godot(live_->source_name()), live_->analyzer().sample_rate(), String::num(live_->time(), 1))));
            if (live_->dropped() > 0)
                lines.push_back(String("Live: {0} samples dropped").format(Array::make(static_cast<int64_t>(live_->dropped()))));
        } else {
            lines.push_back("Live: starts on play.");
        }
    }
    if (!analysis_error_.is_empty()) lines.push_back("Audio: " + analysis_error_);
    {
        PackedStringArray inputs;
        if (midi_) inputs.push_back("MIDI " + to_godot(midi_->source_name()));
        if (osc_) inputs.push_back(to_godot(osc_->source_name()));
        if (dmx_) inputs.push_back(to_godot(dmx_->source_name()));
        if (!inputs.is_empty() || !bindings_.empty()) {
            lines.push_back(String("Controls: {0}; {1} bindings{2}")
                                .format(Array::make(inputs.is_empty() ? String("no inputs") : String(", ").join(inputs),
                                                    static_cast<int64_t>(bindings_.size()),
                                                    learn_.armed() ? ", learning " + to_godot(learn_.macro()) : String())));
        }
        for (const auto& e : control_errors_) lines.push_back("Controls: " + e);
    }
    if (!graph_error_.is_empty()) lines.push_back("Graph: " + graph_error_);
    if (pending_.valid()) lines.push_back("Analysing " + pending_file_.get_file() + "...");
    if (analysis_) {
        lines.push_back(String("{0}: {1} s, {2} BPM, {3} beats per bar")
                            .format(Array::make(resolved_audio_file_.get_file(),
                                                String::num(analysis_->duration_s, 1),
                                                String::num(analysis_->beats.bpm, 1),
                                                analysis_->beats.beats_per_bar)));
    } else if (!pending_.valid() && analysis_error_.is_empty()) {
        lines.push_back("No audio: set audio_player or audio_file.");
    }
    if (evaluator_) {
        const auto& g = evaluator_->graph();
        int targets = 0;
        for (const auto& n : g.nodes) targets += n.spec->is_target ? 1 : 0;
        lines.push_back(String("{0}: {1} nodes, {2} targets ({3} bound), {4} macros")
                            .format(Array::make(graph_file_.get_file(), static_cast<int64_t>(g.nodes.size()),
                                                targets, binder_.bound_count(),
                                                static_cast<int64_t>(g.macros.size()))));
    } else if (graph_error_.is_empty()) {
        lines.push_back("No graph: set graph_file.");
    }
    if (!timeline_error_.is_empty()) lines.push_back("Timeline: " + timeline_error_);
    if (has_tempo_) {
        PackedStringArray lanes;
        for (const auto& l : timeline_->sequence().lanes)
            lanes.push_back(String("{0} {1}").format(Array::make(to_godot(l.name), static_cast<int64_t>(l.notes.size()))));
        lines.push_back(String("Timeline: {0} BPM at bar 1, offset {1} s{2}; notes: {3}")
                            .format(Array::make(String::num(timeline_->bpm_at(0.0), 2),
                                                String::num(timeline_->get_offset() + timeline_->get_nudge(), 3),
                                                tempo_from_file_ ? "" : " (drafted)",
                                                lanes.is_empty() ? String("none") : String(", ").join(lanes))));
    }
    for (const auto& w : graph_warnings_) lines.push_back("Graph warning: " + w);
    for (const auto& p : binder_.problems()) lines.push_back("Target " + p);
    return String("\n").join(lines);
}

PackedStringArray ZtripPlayer::_get_configuration_warnings() const {
    PackedStringArray out;
    if (graph_file_.is_empty()) out.push_back("Set graph_file to a .mgraph.json Motion Graph.");
    if (audio_file_.is_empty() && audio_player_.is_empty() && !live_input_)
        out.push_back("Set audio_player (an AudioStreamPlayer playing a WAV) or audio_file.");
    if (!analysis_error_.is_empty()) out.push_back(analysis_error_);
    if (!live_error_.is_empty()) out.push_back(live_error_);
    for (const auto& e : control_errors_) out.push_back(e);
    if (!graph_error_.is_empty()) out.push_back(graph_error_);
    for (const auto& p : binder_.problems()) out.push_back(p);
    return out;
}

// ---- Macros as dynamic properties -------------------------------------------

bool ZtripPlayer::_set(const StringName& name, const Variant& value) {
    const String n = name;
    if (!n.begins_with("macros/")) return false;
    set_macro(n.substr(7), static_cast<double>(value));
    return true;
}

bool ZtripPlayer::_get(const StringName& name, Variant& value) const {
    const String n = name;
    if (!n.begins_with("macros/")) return false;
    const auto it = macro_values_.find(to_std(n.substr(7)));
    if (it == macro_values_.end()) return false;
    value = it->second;
    return true;
}

void ZtripPlayer::_get_property_list(List<PropertyInfo>* list) const {
    if (!evaluator_) return;
    for (const auto& m : evaluator_->graph().macros) {
        const String hint = String::num(m.min) + "," + String::num(m.max) + ",0.001";
        list->push_back(PropertyInfo(Variant::FLOAT, "macros/" + to_godot(m.name), PROPERTY_HINT_RANGE, hint));
    }
}

PackedStringArray ZtripPlayer::get_macro_names() const {
    PackedStringArray out;
    if (evaluator_)
        for (const auto& m : evaluator_->graph().macros) out.push_back(to_godot(m.name));
    return out;
}

bool ZtripPlayer::set_macro(const String& name, double value) {
    const std::string key = to_std(name);
    macro_values_[key] = value;
    if (!evaluator_ || !evaluator_->set_macro(key, value)) return false;
    macro_values_[key] = *evaluator_->macro(key); // clamped
    return true;
}

double ZtripPlayer::get_macro(const String& name) const {
    if (evaluator_)
        if (const auto v = evaluator_->macro(to_std(name))) return *v;
    const auto it = macro_values_.find(to_std(name));
    return it == macro_values_.end() ? 0.0 : it->second;
}

void ZtripPlayer::apply_macros() {
    for (const auto& m : evaluator_->graph().macros) {
        const auto it = macro_values_.find(m.name);
        if (it != macro_values_.end()) {
            evaluator_->set_macro(m.name, it->second);
        }
        macro_values_[m.name] = *evaluator_->macro(m.name);
    }
}

// ---- Loading -------------------------------------------------------------------

Node* ZtripPlayer::find_audio_player() const {
    if (audio_player_.is_empty() || !is_inside_tree()) return nullptr;
    return get_node_or_null(audio_player_);
}

Node* ZtripPlayer::find_target_root() const {
    if (!is_inside_tree()) return nullptr;
    return get_node_or_null(target_root_);
}

String ZtripPlayer::resolve_audio_file() const {
    if (!audio_file_.is_empty()) return audio_file_;
    if (Node* ap = find_audio_player()) {
        const Ref<AudioStream> stream = ap->get("stream");
        if (stream.is_valid()) return stream->get_path();
    }
    return {};
}

void ZtripPlayer::reload() {
    reload_timeline();
    start_analysis();
    reload_graph();
}

void ZtripPlayer::start_analysis() {
    if (pending_.valid()) {
        reload_after_pending_ = true;
        return;
    }
    analysis_error_ = String();
    const String file = resolve_audio_file();
    if (file.is_empty()) {
        analysis_.reset();
        if (evaluator_) evaluator_->bind_source("main", main_source());
        analysis_ref_.unref();
        resolved_audio_file_ = String();
        set_status_changed();
        return;
    }
    if (!file.get_extension().to_lower().begins_with("wav")) {
        analysis_error_ = file.get_file() + " is not a WAV file; ztrip analyses WAV only for now.";
        set_status_changed();
        return;
    }
    ztrip::godot_host::AnalysisJob job;
    if (!ztrip::godot_host::prepare_analysis(file, use_cache_, job, analysis_error_)) {
        set_status_changed();
        return;
    }
    pending_file_ = file;
    if (threaded_loading_) {
        pending_ = std::async(std::launch::async,
                              [job = std::move(job)]() { return ztrip::godot_host::run_analysis(job); });
    } else {
        finish_analysis(ztrip::godot_host::run_analysis(job));
    }
    set_status_changed();
}

void ZtripPlayer::finish_analysis(ztrip::godot_host::AnalysisOutcome outcome) {
    if (reload_after_pending_) {
        reload_after_pending_ = false;
        start_analysis();
        return;
    }
    if (!outcome.result) {
        analysis_error_ = to_godot(outcome.error);
        UtilityFunctions::push_warning("ZtripPlayer: ", analysis_error_);
        set_status_changed();
        return;
    }
    analysis_ = std::move(outcome.result);
    if (evaluator_) evaluator_->bind_source("main", main_source());
    resolved_audio_file_ = pending_file_;
    analysis_ref_.instantiate();
    analysis_ref_->set_result(analysis_, resolved_audio_file_, outcome.from_cache);
    // A new song gets a fresh draft of whatever didn't come from a file.
    if (resolved_audio_file_ != timeline_song_ || !has_tempo_) {
        timeline_song_ = resolved_audio_file_;
        if (!tempo_from_file_) timeline_->draft_tempo(analysis_ref_);
        if (!sequence_from_file_) timeline_->draft_notes(analysis_ref_);
        has_tempo_ = true;
        bind_timeline();
        emit_signal("timeline_loaded");
    }
    set_status_changed();
    evaluate_and_apply();
    emit_signal("analysis_loaded");
}

void ZtripPlayer::wait_until_loaded() {
    while (pending_.valid()) {
        auto outcome = pending_.get();
        finish_analysis(std::move(outcome)); // may start another run
    }
}

void ZtripPlayer::reload_graph() {
    graph_error_ = String();
    graph_warnings_.clear();
    if (graph_file_.is_empty()) {
        binder_.restore();
        applied_ = false;
        evaluator_.reset();
        notify_property_list_changed();
        set_status_changed();
        return;
    }
    if (!FileAccess::file_exists(graph_file_)) {
        graph_error_ = "Graph file not found: " + graph_file_;
        set_status_changed();
        return;
    }
    graph_mtime_ = FileAccess::get_modified_time(graph_file_);
    const String text = FileAccess::get_file_as_string(graph_file_);
    try {
        const auto doc = ztrip::serialize::parse_mgraph(to_std(text));
        auto compiled = ztrip::graph::compile(doc);
        for (const auto& d : compiled.diagnostics) {
            if (d.severity == ztrip::graph::Diagnostic::Severity::Warning)
                graph_warnings_.push_back(to_godot(d.node.empty() ? d.message : d.node + ": " + d.message));
        }
        if (!compiled.ok()) {
            // Keep running the last good graph, so a half-finished edit doesn't blank the scene.
            graph_error_ = graph_file_.get_file() + ": " + to_godot(compiled.error_text()).strip_edges();
        } else if (evaluator_) {
            evaluator_->swap_graph(std::move(compiled.graph));
        } else {
            ztrip::graph::EvaluatorSettings settings;
            settings.live = live_ != nullptr;
            evaluator_ = std::make_unique<ztrip::graph::Evaluator>(std::move(compiled.graph), settings);
            evaluator_->bind_source("main", main_source());
            evaluator_->bind_controls("main", &controls_);
            bind_timeline();
        }
    } catch (const std::exception& e) {
        graph_error_ = graph_file_.get_file() + ": " + to_godot(e.what());
    }
    if (!graph_error_.is_empty()) {
        UtilityFunctions::push_warning("ZtripPlayer: ", graph_error_);
        set_status_changed();
        return;
    }
    apply_macros();
    // Targets may have changed: put the scene back and bind afresh.
    binder_.restore();
    applied_ = false;
    notify_property_list_changed();
    set_status_changed();
    evaluate_and_apply();
    emit_signal("graph_loaded");
}

// ---- Timeline ------------------------------------------------------------------

void ZtripPlayer::reload_timeline() {
    timeline_error_ = String();
    tempo_from_file_ = false;
    sequence_from_file_ = false;
    if (!tempo_file_.is_empty() && FileAccess::file_exists(tempo_file_)) {
        if (timeline_->load_tempo(tempo_file_) == OK) {
            tempo_from_file_ = true;
            has_tempo_ = true;
        } else {
            timeline_error_ = timeline_->get_error();
        }
    }
    if (!sequence_file_.is_empty() && FileAccess::file_exists(sequence_file_)) {
        if (timeline_->load_sequence(sequence_file_) == OK) {
            sequence_from_file_ = true;
        } else {
            timeline_error_ = timeline_->get_error();
        }
    }
    if (!timeline_error_.is_empty()) UtilityFunctions::push_warning("ZtripPlayer: ", timeline_error_);
    // Draft what is missing now if the song is already analysed, else when it is.
    if (analysis_ref_.is_valid()) {
        if (!tempo_from_file_) timeline_->draft_tempo(analysis_ref_);
        if (!sequence_from_file_) timeline_->draft_notes(analysis_ref_);
        has_tempo_ = true;
        timeline_song_ = resolved_audio_file_;
    }
    bind_timeline();
    set_status_changed();
    emit_signal("timeline_loaded");
}

Error ZtripPlayer::draft_timeline(const String& grid) {
    if (analysis_ref_.is_null()) {
        timeline_error_ = "Nothing to draft from: the song isn't analysed yet.";
        set_status_changed();
        return ERR_UNCONFIGURED;
    }
    timeline_->draft_tempo(analysis_ref_);
    const Error err = timeline_->draft_notes(analysis_ref_, grid);
    timeline_error_ = err == OK ? String() : timeline_->get_error();
    has_tempo_ = true;
    timeline_song_ = resolved_audio_file_;
    bind_timeline();
    set_status_changed();
    emit_signal("timeline_loaded");
    return err;
}

Error ZtripPlayer::save_timeline() {
    if (tempo_file_.is_empty() || sequence_file_.is_empty()) {
        timeline_error_ = "Set tempo_file and sequence_file to save the timeline.";
        set_status_changed();
        return ERR_FILE_BAD_PATH;
    }
    Error err = timeline_->save_tempo(tempo_file_);
    if (err == OK) err = timeline_->save_sequence(sequence_file_);
    timeline_error_ = err == OK ? String() : timeline_->get_error();
    if (err == OK) {
        // What is on disk now is what the user authored: keep it across re-analysis.
        tempo_from_file_ = true;
        sequence_from_file_ = true;
    }
    set_status_changed();
    return err;
}

void ZtripPlayer::bind_timeline() {
    if (!evaluator_) return;
    if (live_) {
        // The song's timeline means nothing to live input: BeatClock runs on
        // its own tempo and NoteLane stays silent.
        evaluator_->bind_tempo("main", nullptr);
        evaluator_->bind_sequence("main", nullptr);
        return;
    }
    evaluator_->bind_tempo("main", has_tempo_ ? &timeline_->tempo() : nullptr);
    evaluator_->bind_sequence("main", &timeline_->sequence());
}

void ZtripPlayer::on_timeline_changed() {
    // Nodes resolve notes to seconds when bound, and an edit may have moved
    // the notes in memory, so rebind before anything evaluates again.
    bind_timeline();
    if (is_node_ready()) evaluate_and_apply();
}

void ZtripPlayer::set_status_changed() {
    update_configuration_warnings();
    notify_property_list_changed();
}

// ---- Live input ----------------------------------------------------------------

void ZtripPlayer::set_live_input(bool live) {
    if (live == live_input_) return;
    live_input_ = live;
    if (!live_input_) {
        stop_live();
        playing_ = false;
    } else if (playing_) {
        // Switching a playing song over to live input.
        stop();
        play();
    }
    set_status_changed();
}

void ZtripPlayer::set_live_device(const String& device) {
    if (device == live_device_) return;
    live_device_ = device;
    restart_live();
}

void ZtripPlayer::set_live_loopback(bool loopback) {
    if (loopback == live_loopback_) return;
    live_loopback_ = loopback;
    restart_live();
}

void ZtripPlayer::set_live_simulate_file(const String& path) {
    if (path == live_simulate_file_) return;
    live_simulate_file_ = path;
    restart_live();
}

void ZtripPlayer::set_live_gain_db(double gain) {
    if (gain == live_gain_db_) return;
    live_gain_db_ = gain;
    restart_live();
}

Array ZtripPlayer::get_live_devices() {
    Array out;
    for (const auto& d : ztrip::io::list_capture_devices()) {
        Dictionary info;
        info["name"] = to_godot(d.name);
        info["default"] = d.is_default;
        info["loopback"] = d.loopback;
        out.push_back(info);
    }
    return out;
}

const ztrip::analysis::AnalysisResult* ZtripPlayer::main_source() const {
    return live_ ? &live_->result() : analysis_.get();
}

void ZtripPlayer::restart_live() {
    if (!live_) return;
    stop_live();
    start_live();
}

void ZtripPlayer::start_live() {
    live_error_ = String();
    ztrip::analysis::LiveSettings settings;
    settings.gain_db = static_cast<float>(live_gain_db_);
    try {
        if (!live_simulate_file_.is_empty()) {
            const PackedByteArray bytes = FileAccess::get_file_as_bytes(live_simulate_file_);
            if (bytes.is_empty()) throw std::runtime_error("cannot read " + to_std(live_simulate_file_));
            const auto audio = ztrip::io::parse_wav(std::span<const std::uint8_t>(bytes.ptr(), bytes.size()));
            live_ = std::make_unique<ztrip::io::LiveAudioInput>(
                audio, to_std(live_simulate_file_.get_file()) + " (simulated input)", true, settings);
        } else {
            ztrip::io::CaptureSettings capture;
            capture.device = to_std(live_device_);
            capture.loopback = live_loopback_;
            live_ = std::make_unique<ztrip::io::LiveAudioInput>(capture, settings);
        }
    } catch (const std::exception& e) {
        live_.reset();
        live_error_ = to_godot(e.what());
        UtilityFunctions::push_warning("ZtripPlayer: ", live_error_);
    }
    time_ = 0.0;
    rebuild_evaluator();
    set_status_changed();
}

void ZtripPlayer::stop_live() {
    if (!live_) return;
    live_.reset();
    time_ = 0.0;
    rebuild_evaluator();
    set_status_changed();
}

// Live and file playback need different evaluators (live has no checkpoints
// and resets on a jump back), so switching rebuilds it from the graph file.
void ZtripPlayer::rebuild_evaluator() {
    if (!evaluator_) return;
    evaluator_.reset();
    reload_graph();
}

// ---- Controls (MIDI, OSC, DMX) -------------------------------------------------

void ZtripPlayer::set_midi_input(bool on) {
    if (on == midi_input_) return;
    midi_input_ = on;
    restart_control_inputs();
}

void ZtripPlayer::set_midi_port(const String& port) {
    if (port == midi_port_) return;
    midi_port_ = port;
    restart_control_inputs();
}

void ZtripPlayer::set_osc_input(bool on) {
    if (on == osc_input_) return;
    osc_input_ = on;
    restart_control_inputs();
}

void ZtripPlayer::set_osc_port(int port) {
    if (port == osc_port_) return;
    osc_port_ = port;
    restart_control_inputs();
}

void ZtripPlayer::set_dmx_input(int protocol) {
    if (protocol == dmx_input_) return;
    dmx_input_ = std::clamp(protocol, 0, 2);
    restart_control_inputs();
}

void ZtripPlayer::set_dmx_port(int port) {
    if (port == dmx_port_) return;
    dmx_port_ = port;
    restart_control_inputs();
}

void ZtripPlayer::set_dmx_universes(const PackedInt32Array& universes) {
    dmx_universes_ = universes;
    restart_control_inputs();
}

PackedStringArray ZtripPlayer::get_midi_ports() {
    PackedStringArray out;
    for (const auto& p : ztrip::io::list_midi_inputs()) out.push_back(to_godot(p));
    return out;
}

void ZtripPlayer::restart_control_inputs() {
    if (!is_inside_tree()) return;
    midi_.reset();
    osc_.reset();
    dmx_.reset();
    control_errors_.clear();
    auto attempt = [&](auto&& open) {
        try {
            open();
        } catch (const std::exception& e) {
            control_errors_.push_back(to_godot(e.what()));
            UtilityFunctions::push_warning("ZtripPlayer: ", control_errors_[control_errors_.size() - 1]);
        }
    };
    if (midi_input_) attempt([&] { midi_ = std::make_unique<ztrip::io::MidiInput>(to_std(midi_port_)); });
    if (osc_input_) {
        attempt([&] {
            ztrip::io::NetworkInputSettings net;
            net.port = osc_port_;
            osc_ = std::make_unique<ztrip::io::OscInput>(net);
        });
    }
    if (dmx_input_ != 0) {
        attempt([&] {
            ztrip::io::NetworkInputSettings net;
            net.port = dmx_port_;
            if (!dmx_universes_.is_empty()) {
                net.sacn_universes.clear();
                for (int64_t i = 0; i < dmx_universes_.size(); ++i) net.sacn_universes.push_back(dmx_universes_[i]);
            }
            dmx_ = std::make_unique<ztrip::io::DmxInput>(
                dmx_input_ == 1 ? ztrip::io::DmxProtocol::ArtNet : ztrip::io::DmxProtocol::Sacn, net);
        });
    }
    set_status_changed();
}

void ZtripPlayer::update_controls() {
    const double stamp = evaluator_->stamp_time(time_);
    if (midi_) midi_->update(controls_, stamp);
    if (osc_) osc_->update(controls_, stamp);
    if (dmx_) dmx_->update(controls_, stamp);

    ztrip::io::ControlBinding learned;
    if (learn_.poll(controls_, learned)) {
        bind_control(to_godot(learned.macro), to_godot(learned.control), learned.min, learned.max);
        emit_signal("control_learned", to_godot(learned.macro), to_godot(learned.control));
    }
    for (const auto& b : bindings_) {
        const ztrip::io::Control* c = controls_.find(b.control);
        if (!c || c->changes.empty()) continue; // never moved: leave the macro as set
        const double v = b.map(c->value_at(stamp));
        const auto it = binding_values_.find(b.macro);
        if (it != binding_values_.end() && it->second == v) continue;
        binding_values_[b.macro] = v;
        set_macro(to_godot(b.macro), v);
    }
}

void ZtripPlayer::set_control_bindings(const Array& bindings) {
    bindings_.clear();
    binding_values_.clear();
    for (int64_t i = 0; i < bindings.size(); ++i) {
        const Dictionary d = bindings[i];
        ztrip::io::ControlBinding b;
        b.macro = to_std(String(d.get("macro", "")));
        b.control = to_std(String(d.get("control", "")));
        b.min = d.get("min", 0.0);
        b.max = d.get("max", 1.0);
        if (!b.macro.empty() && !b.control.empty()) bindings_.push_back(std::move(b));
    }
    set_status_changed();
}

Array ZtripPlayer::get_control_bindings() const {
    Array out;
    for (const auto& b : bindings_) {
        Dictionary d;
        d["macro"] = to_godot(b.macro);
        d["control"] = to_godot(b.control);
        d["min"] = b.min;
        d["max"] = b.max;
        out.push_back(d);
    }
    return out;
}

void ZtripPlayer::bind_control(const String& macro, const String& control, double min, double max) {
    unbind_control(macro); // one control per macro
    bindings_.push_back({to_std(macro), to_std(control), min, max});
    set_status_changed();
}

void ZtripPlayer::unbind_control(const String& macro) {
    const std::string m = to_std(macro);
    std::erase_if(bindings_, [&](const ztrip::io::ControlBinding& b) { return b.macro == m; });
    binding_values_.erase(m);
    set_status_changed();
}

void ZtripPlayer::learn_macro(const String& macro) {
    double min = 0.0, max = 1.0;
    if (evaluator_) {
        for (const auto& m : evaluator_->graph().macros) {
            if (m.name == to_std(macro)) {
                min = m.min;
                max = m.max;
            }
        }
    }
    learn_.arm(to_std(macro), controls_, min, max);
    set_status_changed();
}

void ZtripPlayer::cancel_learn() {
    learn_.cancel();
    set_status_changed();
}

Error ZtripPlayer::load_controls(const String& path) {
    try {
        const String text = FileAccess::get_file_as_string(path);
        if (text.is_empty()) return ERR_FILE_CANT_OPEN;
        bindings_ = ztrip::serialize::parse_controls(to_std(text));
        binding_values_.clear();
    } catch (const std::exception& e) {
        UtilityFunctions::push_warning("ZtripPlayer: ", to_godot(e.what()));
        return ERR_PARSE_ERROR;
    }
    set_status_changed();
    return OK;
}

Error ZtripPlayer::save_controls(const String& path) const {
    const Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
    if (f.is_null()) return FileAccess::get_open_error();
    f->store_string(to_godot(ztrip::serialize::write_controls(bindings_)));
    return OK;
}

double ZtripPlayer::get_control_value(const String& control) const {
    const ztrip::io::Control* c = controls_.find(to_std(control));
    return c && !c->changes.empty() ? c->changes.back().second : 0.0;
}

PackedStringArray ZtripPlayer::get_control_names() const {
    PackedStringArray out;
    for (const auto& [name, c] : controls_.controls()) out.push_back(to_godot(name));
    return out;
}

void ZtripPlayer::inject_control(const String& control, double value, bool fire) {
    const double stamp = evaluator_ ? evaluator_->stamp_time(time_) : time_;
    const std::string name = to_std(control);
    controls_.set(name, stamp, static_cast<float>(value));
    if (fire) controls_.trigger(name, stamp, static_cast<float>(value));
}

// ---- Playback ------------------------------------------------------------------

void ZtripPlayer::play(double from) {
    if (live_input_) {
        playing_ = true;
        if (!live_) start_live();
        return;
    }
    if (from >= 0.0) time_ = from;
    playing_ = true;
    if (Node* ap = find_audio_player()) {
        const Ref<AudioStream> stream = ap->get("stream");
        if (stream.is_valid()) ap->call("play", time_);
    }
}

void ZtripPlayer::stop() {
    playing_ = false;
    if (live_) {
        stop_live();
        return;
    }
    if (Node* ap = find_audio_player()) {
        if (static_cast<bool>(ap->call("is_playing"))) ap->call("stop");
    }
}

void ZtripPlayer::seek(double t) {
    if (live_) return; // live time is the input's clock
    time_ = std::max(0.0, std::isfinite(t) ? t : 0.0);
    if (Node* ap = find_audio_player()) {
        if (static_cast<bool>(ap->call("is_playing"))) ap->call("seek", time_);
    }
    if (is_node_ready()) evaluate_and_apply();
}

double ZtripPlayer::get_duration() const { return analysis_ ? analysis_->duration_s : 0.0; }
double ZtripPlayer::get_bpm() const { return analysis_ ? analysis_->beats.bpm : 0.0; }
Ref<ZtripAnalysis> ZtripPlayer::get_analysis() const { return analysis_ref_; }

double ZtripPlayer::get_output(const String& target) const {
    if (!evaluator_) return 0.0;
    return evaluator_->output(to_std(target)).value_or(0.0);
}

Dictionary ZtripPlayer::get_outputs() const {
    Dictionary out;
    if (evaluator_)
        for (const auto& o : evaluator_->outputs()) out[to_godot(o.target)] = o.value;
    return out;
}

Variant ZtripPlayer::get_port_value(const String& node, const String& port) const {
    if (!evaluator_) return {};
    const auto v = evaluator_->port_value(to_std(node), to_std(port));
    if (!v) return {};
    return v->v;
}

void ZtripPlayer::update_now() { evaluate_and_apply(); }

void ZtripPlayer::evaluate_and_apply() {
    if (!evaluator_) return;
    update_controls();
    evaluator_->evaluate(time_);
    if (!active_) return;
    binder_.set_root(find_target_root());
    const int problems_before = binder_.problems().size();
    binder_.apply(evaluator_->outputs());
    applied_ = true;
    if (binder_.problems().size() != problems_before) {
        for (int i = problems_before; i < binder_.problems().size(); ++i)
            UtilityFunctions::push_warning("ZtripPlayer target ", binder_.problems()[i]);
        set_status_changed();
    }
}

void ZtripPlayer::poll(double delta) {
    if (pending_.valid() && pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto outcome = pending_.get();
        finish_analysis(std::move(outcome));
    }

    graph_poll_ += delta;
    if (graph_poll_ >= kGraphPollInterval && !graph_file_.is_empty()) {
        graph_poll_ = 0.0;
        if (FileAccess::file_exists(graph_file_) && FileAccess::get_modified_time(graph_file_) != graph_mtime_)
            reload_graph();
    }

    if (live_) {
        live_->update();
        time_ = live_->time();
        evaluate_and_apply();
        return;
    }
    if (live_input_) {
        // Live input that is stopped or failed to open: hold still.
        if (!applied_) evaluate_and_apply();
        return;
    }

    // The song's audio clock leads whenever it is running; otherwise playback
    // runs on frame time.
    Node* ap = find_audio_player();
    const bool audio_running = ap && static_cast<bool>(ap->call("is_playing"));
    if (audio_running) {
        double pos = static_cast<double>(ap->call("get_playback_position"));
        if (Engine::get_singleton()->get_write_movie_path().is_empty()) {
            pos += AudioServer::get_singleton()->get_time_since_last_mix();
            pos -= AudioServer::get_singleton()->get_output_latency();
        }
        pos = std::max(pos, 0.0);
        // The mix clock moves in blocks; don't let its jitter step time back.
        if (!(pos < time_ && time_ - pos < kBackJitter)) time_ = pos;
        playing_ = true;
    } else if (playing_) {
        const Ref<AudioStream> stream = ap ? ap->get("stream") : Variant();
        if (stream.is_valid()) {
            playing_ = false; // the song ended (or was stopped on the player)
            emit_signal("finished");
        } else {
            time_ += delta;
            if (analysis_ && time_ >= analysis_->duration_s) {
                time_ = analysis_->duration_s;
                playing_ = false;
                emit_signal("finished");
            }
        }
    }
    // Paused, control input still reaches bound macros (graph nodes reading
    // controls directly see it once playing, at the time it arrived).
    const bool controls_waiting = (midi_ && midi_->pending()) || (osc_ && osc_->pending()) || (dmx_ && dmx_->pending());
    if (playing_ || !applied_ || controls_waiting) evaluate_and_apply();
}

void ZtripPlayer::_notification(int what) {
    switch (what) {
    case NOTIFICATION_READY:
        set_process_internal(true);
        restart_control_inputs();
        reload();
        if (autoplay_ && !in_editor()) play(0.0);
        break;
    case NOTIFICATION_INTERNAL_PROCESS:
        poll(get_process_delta_time());
        break;
    case NOTIFICATION_ENTER_TREE:
        if (is_node_ready()) restart_control_inputs();
        break;
    case NOTIFICATION_EXIT_TREE:
        stop_live();
        midi_.reset();
        osc_.reset();
        dmx_.reset();
        binder_.restore();
        applied_ = false;
        break;
    case NOTIFICATION_EDITOR_PRE_SAVE:
        // Never save graph-driven values into the scene.
        binder_.restore();
        break;
    case NOTIFICATION_EDITOR_POST_SAVE:
        applied_ = false;
        evaluate_and_apply();
        break;
    default: break;
    }
}

} // namespace godot
