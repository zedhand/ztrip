#pragma once
// Session: everything a host needs to play a song through a Motion Graph,
// with no engine in sight. It owns the song's analysis (analysed in the
// background, cached on disk), the graph (hot-reloaded when its file
// changes, keeping the last good one when an edit doesn't compile), macro
// values that survive reloads, and the song's timeline (tempo map and Note
// Sequence, loaded from files or drafted from the analysis). A host adds
// only what is engine-specific: where the song time comes from, and writing
// outputs() to scene properties.

#include "ztrip/analysis/offline.hpp"
#include "ztrip/graph/evaluator.hpp"
#include "ztrip/host/analysis_cache.hpp"
#include "ztrip/time/autodraft.hpp"
#include "ztrip/time/note_sequence.hpp"
#include "ztrip/time/tempo_map.hpp"

#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ztrip::host {

struct SessionSettings {
    // Analyse songs on a worker thread (poll() picks the result up) or on
    // the calling thread.
    bool threaded = true;
    // Where .sigcache files go; empty for no cache.
    std::filesystem::path cache_dir;
    // How often poll() checks the graph file for edits, in seconds.
    double graph_poll_s = 0.5;
};

// What changed during a call, so a host knows what to refresh.
struct SessionChanges {
    bool analysis = false; // a song finished analysing, or failed to
    bool graph = false;    // the graph was loaded or cleared: its targets may differ, so rebind them
    bool timeline = false; // the tempo map or notes changed

    bool any() const noexcept { return analysis || graph || timeline; }
};

struct MacroInfo {
    std::string name;
    double min = 0.0;
    double max = 1.0;
    double default_value = 0.0;
    double value = 0.0;
};

class Session {
public:
    explicit Session(SessionSettings settings = {});
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    const SessionSettings& settings() const noexcept { return settings_; }
    void set_threaded(bool threaded) { settings_.threaded = threaded; }
    void set_cache_dir(std::filesystem::path dir) { settings_.cache_dir = std::move(dir); }

    // ---- Song ---------------------------------------------------------------

    // Starts analysing a WAV file: the file is read now, and analysed in the
    // background when threaded. A request made while another runs replaces
    // it once that one finishes. An empty path clears the song. Returns false
    // (see audio_error()) when the file can't be read.
    bool load_audio_file(const std::filesystem::path& wav);
    void load_audio_bytes(std::vector<std::uint8_t> wav, std::string label);
    void clear_audio();
    bool loading() const noexcept { return pending_.valid(); }
    // Blocks until background analysis is done and applies it.
    SessionChanges wait();

    const analysis::AnalysisResult* analysis() const noexcept { return analysis_.get(); }
    std::shared_ptr<const analysis::AnalysisResult> analysis_ptr() const { return analysis_; }
    bool from_cache() const noexcept { return from_cache_; }
    // The song's label (its path, for a file), or empty with no song.
    const std::string& audio_label() const noexcept { return audio_label_; }
    double duration() const noexcept { return analysis_ ? analysis_->duration_s : 0.0; }
    double bpm() const noexcept { return analysis_ ? analysis_->beats.bpm : 0.0; }

    // ---- Graph --------------------------------------------------------------

    // Loads and compiles a .mgraph.json. On an error the last good graph
    // keeps running (so a half-finished edit doesn't blank the scene) and
    // graph_error() says what is wrong. A graph replacing another keeps the
    // state of nodes whose id and type persist. An empty path clears it.
    SessionChanges load_graph_file(const std::filesystem::path& path);
    SessionChanges load_graph_text(std::string_view json, std::string label);
    SessionChanges clear_graph();
    // Reloads the graph file now if it changed on disk since it was read.
    SessionChanges reload_graph_if_changed();
    const std::filesystem::path& graph_file() const noexcept { return graph_file_; }

    bool has_graph() const noexcept { return evaluator_ != nullptr; }
    const graph::Evaluator* evaluator() const noexcept { return evaluator_.get(); }
    graph::Evaluator* evaluator() noexcept { return evaluator_.get(); }
    // PropertyOut targets of the current graph, in evaluation order.
    std::vector<std::string> targets() const;

    // ---- Timeline -----------------------------------------------------------

    // Sets where the tempo map (.tempo.json) and Note Sequence (.nseq.json)
    // live, and loads the ones that exist; the rest are drafted from the
    // analysis, now if the song is analysed or else when it is. Either path
    // may be empty.
    SessionChanges set_timeline_files(std::filesystem::path tempo_file, std::filesystem::path sequence_file);
    SessionChanges reload_timeline();
    // Drafts the tempo map and every draft lane again, keeping the nudge and
    // each lane's swing. Grid is "1/16", "1/8T" and so on.
    SessionChanges draft_timeline(std::string_view grid = "1/16");
    // Writes the timeline to the two files. What is on disk is then what the
    // user authored, so it is kept when the song is analysed again.
    bool save_timeline();
    // Saves to new files and keeps them as the timeline's files from then on.
    bool save_timeline_as(std::filesystem::path tempo_file, std::filesystem::path sequence_file);

    const std::filesystem::path& tempo_file() const noexcept { return tempo_file_; }
    const std::filesystem::path& sequence_file() const noexcept { return sequence_file_; }
    bool has_tempo() const noexcept { return has_tempo_; }
    const time::TempoMap& tempo() const noexcept { return tempo_; }
    time::TempoMap& tempo() noexcept { return tempo_; }
    const time::NoteSequence& sequence() const noexcept { return sequence_; }
    time::NoteSequence& sequence() noexcept { return sequence_; }
    // Call after editing tempo() or sequence() in place: nodes resolve notes
    // to seconds when bound, so the graph needs them bound again.
    void timeline_edited();
    // The lanes draft_timeline() and a new song draft (see default_draft_lanes()).
    std::vector<time::DraftLaneSpec>& draft_lanes() noexcept { return draft_lanes_; }

    // ---- Per frame ----------------------------------------------------------

    // Picks up finished analysis and, every graph_poll_s of `dt`, reloads an
    // edited graph file. Cheap when nothing changed.
    SessionChanges poll(double dt);
    // Evaluates the graph at song time t (seconds). Offline evaluation is
    // deterministic: the same t gives the same outputs however it was reached.
    void evaluate(double t);
    double time() const noexcept { return time_; }
    const std::vector<graph::TargetValue>& outputs() const noexcept;
    std::optional<double> output(std::string_view target) const;
    std::optional<graph::Value> port_value(std::string_view node, std::string_view port) const;

    // ---- Macros -------------------------------------------------------------

    // The current graph's macros. Values set here survive graph reloads where
    // the name persists, and are remembered for graphs not loaded yet.
    std::vector<MacroInfo> macros() const;
    bool set_macro(std::string_view name, double value);
    std::optional<double> macro(std::string_view name) const;
    const std::map<std::string, double, std::less<>>& macro_values() const noexcept { return macro_values_; }

    // ---- Status -------------------------------------------------------------

    const std::string& audio_error() const noexcept { return audio_error_; }
    const std::string& graph_error() const noexcept { return graph_error_; }
    const std::string& timeline_error() const noexcept { return timeline_error_; }
    const std::vector<std::string>& graph_warnings() const noexcept { return graph_warnings_; }
    // One line each for the song, the graph and the timeline, then problems.
    std::vector<std::string> status_lines() const;

private:
    struct AudioRequest {
        std::vector<std::uint8_t> bytes;
        std::string label;
    };

    void start_analysis(AudioRequest request);
    SessionChanges finish_analysis(AnalysisOutcome outcome);
    SessionChanges compile_graph(std::string_view json, const std::string& label);
    void draft_missing();
    void draft_tempo();
    void draft_notes(const time::GridDivision& grid);
    void bind_all();
    void bind_timeline();
    void apply_macros();

    SessionSettings settings_;

    std::shared_ptr<const analysis::AnalysisResult> analysis_;
    bool from_cache_ = false;
    std::string audio_label_;
    std::string audio_error_;
    std::future<AnalysisOutcome> pending_;
    std::string pending_label_;
    std::optional<AudioRequest> queued_;

    std::unique_ptr<graph::Evaluator> evaluator_;
    std::filesystem::path graph_file_;
    std::filesystem::file_time_type graph_mtime_{};
    std::string graph_label_;
    std::string graph_error_;
    std::vector<std::string> graph_warnings_;
    double graph_poll_ = 0.0;
    std::map<std::string, double, std::less<>> macro_values_;

    time::TempoMap tempo_;
    time::NoteSequence sequence_;
    std::vector<time::DraftLaneSpec> draft_lanes_;
    std::filesystem::path tempo_file_;
    std::filesystem::path sequence_file_;
    bool has_tempo_ = false;
    bool tempo_from_file_ = false;
    bool sequence_from_file_ = false;
    std::string timeline_song_; // the song the timeline was drafted for
    std::string timeline_error_;

    double time_ = 0.0;
};

} // namespace ztrip::host
