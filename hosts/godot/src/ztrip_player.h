// ZtripPlayer: plays a song through a Motion Graph and drives scene properties.
#pragma once

#include "property_binder.h"
#include "ztrip_analysis.h"
#include "ztrip_timeline.h"

#include "ztrip/graph/evaluator.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include <future>
#include <map>
#include <memory>
#include <string>

namespace godot {

// Add one to a scene, point it at an AudioStreamPlayer (or a WAV file) and a
// .mgraph.json, and it analyses the song, evaluates the graph at the song's
// playback position every frame and writes the graph's PropertyOut targets,
// resolved relative to target_root. It runs in the editor as well, so the
// scene reacts while you scrub `time` or press `playing` in the inspector.
// Macros appear in the inspector under "macros/".
//
// The player also holds the song's timeline (tempo map and Note Sequence,
// see ZtripTimeline), which the graph's BeatClock and NoteLane nodes read.
// It loads tempo_file and sequence_file when they exist; otherwise it drafts
// both from the analysis, and drafts again when the song changes.
class ZtripPlayer : public Node {
    GDCLASS(ZtripPlayer, Node)

public:
    ZtripPlayer();
    ~ZtripPlayer() override;

    void set_audio_player(const NodePath& path);
    NodePath get_audio_player() const { return audio_player_; }
    void set_audio_file(const String& path);
    String get_audio_file() const { return audio_file_; }
    void set_graph_file(const String& path);
    String get_graph_file() const { return graph_file_; }
    void set_tempo_file(const String& path);
    String get_tempo_file() const { return tempo_file_; }
    void set_sequence_file(const String& path);
    String get_sequence_file() const { return sequence_file_; }
    void set_target_root(const NodePath& path);
    NodePath get_target_root() const { return target_root_; }
    void set_active(bool active);
    bool is_active() const { return active_; }
    void set_playing(bool playing);
    bool is_playing() const { return playing_; }
    void set_time(double t);
    double get_time() const { return time_; }
    void set_autoplay(bool autoplay) { autoplay_ = autoplay; }
    bool has_autoplay() const { return autoplay_; }
    void set_threaded_loading(bool threaded) { threaded_loading_ = threaded; }
    bool is_threaded_loading() const { return threaded_loading_; }
    void set_use_cache(bool use_cache) { use_cache_ = use_cache; }
    bool is_using_cache() const { return use_cache_; }
    String get_status() const;

    void play(double from = -1.0);
    void stop();
    void seek(double t);
    // Re-reads the audio file (re-analysing if it changed) and the graph.
    void reload();
    void reload_graph();
    // Waits for a background analysis to finish (for scripts and tests).
    void wait_until_loaded();

    bool is_ready() const { return evaluator_ != nullptr && analysis_ != nullptr; }
    bool is_loading() const { return pending_.valid(); }
    double get_duration() const;
    double get_bpm() const;
    Ref<ZtripAnalysis> get_analysis() const;
    Ref<ZtripTimeline> get_timeline() const { return timeline_; }
    // Re-reads tempo_file and sequence_file (drafting what is missing).
    void reload_timeline();
    // Drafts the tempo map and notes from the analysis again, keeping the nudge.
    Error draft_timeline(const String& grid = "1/16");
    // Writes the timeline to tempo_file and sequence_file.
    Error save_timeline();
    String get_resolved_audio_file() const { return resolved_audio_file_; }

    double get_output(const String& target) const;
    Dictionary get_outputs() const;
    // A node port's value after the last evaluation, or null if unknown.
    Variant get_port_value(const String& node, const String& port) const;
    PackedStringArray get_macro_names() const;
    bool set_macro(const String& name, double value);
    double get_macro(const String& name) const;
    PackedStringArray get_binding_problems() const { return binder_.problems(); }
    int get_bound_count() const { return binder_.bound_count(); }

    // Evaluates the graph at the current time and writes the targets now,
    // without waiting for the next frame.
    void update_now();

    PackedStringArray _get_configuration_warnings() const override;

protected:
    static void _bind_methods();
    void _notification(int what);
    bool _set(const StringName& name, const Variant& value);
    bool _get(const StringName& name, Variant& value) const;
    void _get_property_list(List<PropertyInfo>* list) const;

private:
    Node* find_audio_player() const;
    Node* find_target_root() const;
    String resolve_audio_file() const;
    void start_analysis();
    void finish_analysis(ztrip::godot_host::AnalysisOutcome outcome);
    void poll(double delta);
    void evaluate_and_apply();
    void apply_macros();
    void set_status_changed();
    void bind_timeline();
    void on_timeline_changed();

    NodePath audio_player_;
    String audio_file_;
    String graph_file_;
    String tempo_file_;
    String sequence_file_;
    NodePath target_root_ = NodePath("..");
    bool active_ = true;
    bool playing_ = false;
    bool autoplay_ = false;
    bool threaded_loading_ = true;
    bool use_cache_ = true;
    double time_ = 0.0;

    std::unique_ptr<ztrip::graph::Evaluator> evaluator_;
    std::shared_ptr<const ztrip::analysis::AnalysisResult> analysis_;
    Ref<ZtripAnalysis> analysis_ref_;
    ztrip::godot_host::PropertyBinder binder_;
    std::map<std::string, double> macro_values_;

    Ref<ZtripTimeline> timeline_;
    bool has_tempo_ = false;         // the timeline's tempo map came from a file or a draft
    bool tempo_from_file_ = false;
    bool sequence_from_file_ = false;
    String timeline_song_;           // the song the timeline was drafted for
    String timeline_error_;

    std::future<ztrip::godot_host::AnalysisOutcome> pending_;
    String pending_file_;
    bool reload_after_pending_ = false;
    String resolved_audio_file_;
    String analysis_error_;
    String graph_error_;
    PackedStringArray graph_warnings_;
    uint64_t graph_mtime_ = 0;
    double graph_poll_ = 0.0;
    bool applied_ = false; // targets have been written since the last restore
};

} // namespace godot
