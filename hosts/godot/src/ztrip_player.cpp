#include "ztrip_player.h"

#include "convert.h"

#include "ztrip/graph/compiler.hpp"
#include "ztrip/serialize/mgraph.hpp"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>

namespace godot {

using ztrip::godot_host::to_godot;
using ztrip::godot_host::to_std;

namespace {

constexpr double kGraphPollInterval = 0.5; // seconds between checks for an edited graph file
constexpr double kBackJitter = 0.1;        // audio clock jitter ignored while playing, seconds

bool in_editor() { return Engine::get_singleton()->is_editor_hint(); }

} // namespace

ZtripPlayer::ZtripPlayer() = default;

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
    ADD_SIGNAL(MethodInfo("finished"));
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
    if (!analysis_error_.is_empty()) lines.push_back("Audio: " + analysis_error_);
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
    for (const auto& w : graph_warnings_) lines.push_back("Graph warning: " + w);
    for (const auto& p : binder_.problems()) lines.push_back("Target " + p);
    return String("\n").join(lines);
}

PackedStringArray ZtripPlayer::_get_configuration_warnings() const {
    PackedStringArray out;
    if (graph_file_.is_empty()) out.push_back("Set graph_file to a .mgraph.json Motion Graph.");
    if (audio_file_.is_empty() && audio_player_.is_empty())
        out.push_back("Set audio_player (an AudioStreamPlayer playing a WAV) or audio_file.");
    if (!analysis_error_.is_empty()) out.push_back(analysis_error_);
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
        if (evaluator_) evaluator_->bind_source("main", nullptr);
        analysis_.reset();
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
    if (evaluator_) evaluator_->bind_source("main", outcome.result.get());
    analysis_ = std::move(outcome.result);
    resolved_audio_file_ = pending_file_;
    analysis_ref_.instantiate();
    analysis_ref_->set_result(analysis_, resolved_audio_file_, outcome.from_cache);
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
            evaluator_ = std::make_unique<ztrip::graph::Evaluator>(std::move(compiled.graph));
            evaluator_->bind_source("main", analysis_.get());
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

void ZtripPlayer::set_status_changed() {
    update_configuration_warnings();
    notify_property_list_changed();
}

// ---- Playback ------------------------------------------------------------------

void ZtripPlayer::play(double from) {
    if (from >= 0.0) time_ = from;
    playing_ = true;
    if (Node* ap = find_audio_player()) {
        const Ref<AudioStream> stream = ap->get("stream");
        if (stream.is_valid()) ap->call("play", time_);
    }
}

void ZtripPlayer::stop() {
    playing_ = false;
    if (Node* ap = find_audio_player()) {
        if (static_cast<bool>(ap->call("is_playing"))) ap->call("stop");
    }
}

void ZtripPlayer::seek(double t) {
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
    if (playing_ || !applied_) evaluate_and_apply();
}

void ZtripPlayer::_notification(int what) {
    switch (what) {
    case NOTIFICATION_READY:
        set_process_internal(true);
        reload();
        if (autoplay_ && !in_editor()) play(0.0);
        break;
    case NOTIFICATION_INTERNAL_PROCESS:
        poll(get_process_delta_time());
        break;
    case NOTIFICATION_EXIT_TREE:
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
