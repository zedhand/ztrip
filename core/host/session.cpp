#include "ztrip/host/session.hpp"

#include "ztrip/graph/compiler.hpp"
#include "ztrip/serialize/mgraph.hpp"
#include "ztrip/serialize/nseq.hpp"
#include "ztrip/serialize/tempo.hpp"

#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <system_error>

namespace ztrip::host {

namespace {

std::optional<std::vector<std::uint8_t>> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) return std::nullopt;
    return bytes;
}

std::optional<std::string> read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) return std::nullopt;
    return text;
}

std::string file_name(const std::string& label) {
    return path_to_utf8(path_from_utf8(label).filename());
}

std::string fixed(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

std::filesystem::file_time_type mtime(const std::filesystem::path& path) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(path, ec);
    return ec ? std::filesystem::file_time_type{} : t;
}

const std::vector<graph::TargetValue> kNoOutputs;

} // namespace

Session::Session(SessionSettings settings) : settings_(std::move(settings)), draft_lanes_(default_draft_lanes()) {
    sequence_.name = "main";
}

Session::~Session() {
    if (pending_.valid()) pending_.wait();
}

// ---- Song ------------------------------------------------------------------------

bool Session::load_audio_file(const std::filesystem::path& wav) {
    if (wav.empty()) {
        clear_audio();
        return true;
    }
    const std::string label = path_to_utf8(wav);
    auto bytes = read_bytes(wav);
    if (!bytes || bytes->empty()) {
        audio_error_ = std::filesystem::exists(wav) ? "Could not read audio file: " + label
                                                    : "Audio file not found: " + label;
        return false;
    }
    load_audio_bytes(std::move(*bytes), label);
    return true;
}

void Session::load_audio_bytes(std::vector<std::uint8_t> wav, std::string label) {
    AudioRequest request{std::move(wav), std::move(label)};
    if (pending_.valid()) {
        queued_ = std::move(request); // runs when the current analysis is done
        return;
    }
    start_analysis(std::move(request));
}

void Session::clear_audio() {
    queued_.reset();
    analysis_.reset();
    audio_label_.clear();
    audio_error_.clear();
    from_cache_ = false;
    bind_all();
}

void Session::start_analysis(AudioRequest request) {
    audio_error_.clear();
    pending_label_ = request.label;
    auto run = [request = std::move(request), cache = settings_.cache_dir]() {
        return analyze_wav(request.bytes, cache, request.label);
    };
    if (settings_.threaded) {
        pending_ = std::async(std::launch::async, std::move(run));
    } else {
        std::promise<AnalysisOutcome> done;
        done.set_value(run());
        pending_ = done.get_future();
    }
}

SessionChanges Session::finish_analysis(AnalysisOutcome outcome) {
    SessionChanges changes;
    changes.analysis = true;
    if (queued_) {
        // A newer song was asked for while this one ran.
        AudioRequest next = std::move(*queued_);
        queued_.reset();
        start_analysis(std::move(next));
        return changes;
    }
    if (!outcome.result) {
        audio_error_ = outcome.error;
        return changes;
    }
    analysis_ = std::move(outcome.result);
    from_cache_ = outcome.from_cache;
    audio_label_ = pending_label_;
    // A new song gets a fresh draft of whatever didn't come from a file.
    if (audio_label_ != timeline_song_ || !has_tempo_) {
        draft_missing();
        changes.timeline = true;
    }
    bind_all();
    return changes;
}

SessionChanges Session::wait() {
    SessionChanges changes;
    while (pending_.valid()) {
        auto c = finish_analysis(pending_.get()); // may start the queued song
        changes.analysis |= c.analysis;
        changes.timeline |= c.timeline;
    }
    return changes;
}

// ---- Graph -----------------------------------------------------------------------

SessionChanges Session::load_graph_file(const std::filesystem::path& path) {
    graph_file_ = path;
    graph_poll_ = 0.0;
    if (path.empty()) return clear_graph();
    const std::string label = path_to_utf8(path);
    graph_mtime_ = mtime(path);
    const auto text = read_text(path);
    if (!text) {
        graph_warnings_.clear();
        graph_error_ = std::filesystem::exists(path) ? "Could not read graph file: " + label
                                                     : "Graph file not found: " + label;
        return {};
    }
    return compile_graph(*text, label);
}

SessionChanges Session::load_graph_text(std::string_view json, std::string label) {
    graph_file_.clear();
    return compile_graph(json, label);
}

SessionChanges Session::clear_graph() {
    graph_error_.clear();
    graph_warnings_.clear();
    graph_label_.clear();
    SessionChanges changes;
    changes.graph = evaluator_ != nullptr;
    evaluator_.reset();
    return changes;
}

SessionChanges Session::reload_graph_if_changed() {
    graph_poll_ = 0.0;
    if (graph_file_.empty()) return {};
    std::error_code ec;
    if (!std::filesystem::exists(graph_file_, ec)) return {};
    if (mtime(graph_file_) == graph_mtime_) return {};
    return load_graph_file(graph_file_);
}

SessionChanges Session::compile_graph(std::string_view json, const std::string& label) {
    graph_error_.clear();
    graph_warnings_.clear();
    const std::string name = file_name(label);
    try {
        const auto doc = serialize::parse_mgraph(json);
        auto compiled = graph::compile(doc);
        for (const auto& d : compiled.diagnostics) {
            if (d.severity == graph::Diagnostic::Severity::Warning)
                graph_warnings_.push_back(d.node.empty() ? d.message : d.node + ": " + d.message);
        }
        if (!compiled.ok()) {
            std::string text = compiled.error_text();
            while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
            graph_error_ = name + ": " + text;
            return {};
        }
        if (evaluator_) {
            evaluator_->swap_graph(std::move(compiled.graph));
        } else {
            evaluator_ = std::make_unique<graph::Evaluator>(std::move(compiled.graph));
            bind_all();
        }
    } catch (const std::exception& e) {
        graph_error_ = name + ": " + e.what();
        return {};
    }
    graph_label_ = label;
    apply_macros();
    SessionChanges changes;
    changes.graph = true;
    return changes;
}

std::vector<std::string> Session::targets() const {
    std::vector<std::string> out;
    if (!evaluator_) return out;
    for (const auto& n : evaluator_->graph().nodes)
        if (n.spec->is_target) out.push_back(n.target);
    return out;
}

// ---- Timeline --------------------------------------------------------------------

SessionChanges Session::set_timeline_files(std::filesystem::path tempo_file, std::filesystem::path sequence_file) {
    tempo_file_ = std::move(tempo_file);
    sequence_file_ = std::move(sequence_file);
    return reload_timeline();
}

SessionChanges Session::reload_timeline() {
    timeline_error_.clear();
    tempo_from_file_ = false;
    sequence_from_file_ = false;
    std::error_code ec;
    if (!tempo_file_.empty() && std::filesystem::exists(tempo_file_, ec)) {
        try {
            tempo_ = serialize::load_tempo_map(tempo_file_);
            tempo_from_file_ = true;
            has_tempo_ = true;
        } catch (const std::exception& e) {
            timeline_error_ = e.what();
        }
    }
    if (!sequence_file_.empty() && std::filesystem::exists(sequence_file_, ec)) {
        try {
            sequence_ = serialize::load_nseq(sequence_file_);
            sequence_from_file_ = true;
        } catch (const std::exception& e) {
            timeline_error_ = e.what();
        }
    }
    // Draft what is missing now if the song is already analysed, else when it is.
    if (analysis_) draft_missing();
    bind_timeline();
    SessionChanges changes;
    changes.timeline = true;
    return changes;
}

void Session::draft_missing() {
    if (!analysis_) return;
    if (!tempo_from_file_) draft_tempo();
    if (!sequence_from_file_) draft_notes({});
    has_tempo_ = true;
    timeline_song_ = audio_label_;
}

void Session::draft_tempo() {
    const double nudge = tempo_.nudge();
    tempo_ = time::tempo_map_from_beats(analysis_->beats);
    tempo_.set_nudge(nudge);
}

void Session::draft_notes(const time::GridDivision& grid) {
    time::DraftSettings settings;
    settings.grid = grid;
    settings.lanes = draft_lanes_;
    time::NoteSequence drafted = time::auto_draft(*analysis_, tempo_, settings);
    for (auto& l : drafted.lanes) {
        if (time::Lane* existing = sequence_.find_lane(l.name)) {
            existing->notes = std::move(l.notes); // keep the lane's swing
        } else {
            sequence_.lanes.push_back(std::move(l));
        }
    }
}

SessionChanges Session::draft_timeline(std::string_view grid) {
    if (!analysis_) {
        timeline_error_ = "Nothing to draft from: the song isn't analysed yet.";
        return {};
    }
    const auto g = time::parse_grid(grid);
    if (!g) {
        timeline_error_ = "Unknown grid: " + std::string(grid);
        return {};
    }
    timeline_error_.clear();
    draft_tempo();
    draft_notes(*g);
    has_tempo_ = true;
    timeline_song_ = audio_label_;
    bind_timeline();
    SessionChanges changes;
    changes.timeline = true;
    return changes;
}

bool Session::save_timeline() {
    if (tempo_file_.empty() || sequence_file_.empty()) {
        timeline_error_ = "Set the tempo and sequence files to save the timeline.";
        return false;
    }
    try {
        serialize::save_tempo_map(tempo_file_, tempo_);
        serialize::save_nseq(sequence_file_, sequence_);
    } catch (const std::exception& e) {
        timeline_error_ = e.what();
        return false;
    }
    timeline_error_.clear();
    tempo_from_file_ = true;
    sequence_from_file_ = true;
    has_tempo_ = true;
    return true;
}

bool Session::save_timeline_as(std::filesystem::path tempo_file, std::filesystem::path sequence_file) {
    tempo_file_ = std::move(tempo_file);
    sequence_file_ = std::move(sequence_file);
    return save_timeline();
}

void Session::timeline_edited() { bind_timeline(); }

// ---- Evaluation ------------------------------------------------------------------

void Session::bind_all() {
    if (!evaluator_) return;
    evaluator_->bind_source("main", analysis_.get());
    bind_timeline();
}

void Session::bind_timeline() {
    if (!evaluator_) return;
    evaluator_->bind_tempo("main", has_tempo_ ? &tempo_ : nullptr);
    evaluator_->bind_sequence("main", &sequence_);
}

SessionChanges Session::poll(double dt) {
    SessionChanges changes;
    if (pending_.valid() && pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        changes = finish_analysis(pending_.get());
    }
    graph_poll_ += dt;
    if (graph_poll_ >= settings_.graph_poll_s && !graph_file_.empty()) {
        changes.graph |= reload_graph_if_changed().graph;
    }
    return changes;
}

void Session::evaluate(double t) {
    time_ = t;
    if (evaluator_) evaluator_->evaluate(t);
}

const std::vector<graph::TargetValue>& Session::outputs() const noexcept {
    return evaluator_ ? evaluator_->outputs() : kNoOutputs;
}

std::optional<double> Session::output(std::string_view target) const {
    if (!evaluator_) return std::nullopt;
    return evaluator_->output(target);
}

std::optional<graph::Value> Session::port_value(std::string_view node, std::string_view port) const {
    if (!evaluator_) return std::nullopt;
    return evaluator_->port_value(node, port);
}

// ---- Macros ----------------------------------------------------------------------

std::vector<MacroInfo> Session::macros() const {
    std::vector<MacroInfo> out;
    if (!evaluator_) return out;
    for (const auto& m : evaluator_->graph().macros)
        out.push_back({m.name, m.min, m.max, m.default_value, evaluator_->macro(m.name).value_or(m.default_value)});
    return out;
}

bool Session::set_macro(std::string_view name, double value) {
    const std::string key(name);
    macro_values_[key] = value;
    if (!evaluator_ || !evaluator_->set_macro(key, value)) return false;
    macro_values_[key] = *evaluator_->macro(key); // clamped
    return true;
}

std::optional<double> Session::macro(std::string_view name) const {
    if (evaluator_)
        if (const auto v = evaluator_->macro(name)) return v;
    const auto it = macro_values_.find(name);
    if (it == macro_values_.end()) return std::nullopt;
    return it->second;
}

void Session::apply_macros() {
    for (const auto& m : evaluator_->graph().macros) {
        const auto it = macro_values_.find(m.name);
        if (it != macro_values_.end()) evaluator_->set_macro(m.name, it->second);
        macro_values_[m.name] = *evaluator_->macro(m.name);
    }
}

// ---- Status ----------------------------------------------------------------------

std::vector<std::string> Session::status_lines() const {
    std::vector<std::string> lines;
    if (!audio_error_.empty()) lines.push_back("Audio: " + audio_error_);
    if (pending_.valid()) lines.push_back("Analysing " + file_name(pending_label_) + "...");
    if (analysis_) {
        lines.push_back(file_name(audio_label_) + ": " + fixed(analysis_->duration_s, 1) + " s, " +
                        fixed(analysis_->beats.bpm, 1) + " BPM, " + std::to_string(analysis_->beats.beats_per_bar) +
                        " beats per bar");
    } else if (!pending_.valid() && audio_error_.empty()) {
        lines.push_back("No audio: set the audio file.");
    }
    if (!graph_error_.empty()) lines.push_back("Graph: " + graph_error_);
    if (evaluator_) {
        const auto& g = evaluator_->graph();
        lines.push_back(file_name(graph_label_) + ": " + std::to_string(g.nodes.size()) + " nodes, " +
                        std::to_string(targets().size()) + " targets, " + std::to_string(g.macros.size()) +
                        " macros");
    } else if (graph_error_.empty()) {
        lines.push_back("No graph: set the graph file.");
    }
    if (!timeline_error_.empty()) lines.push_back("Timeline: " + timeline_error_);
    if (has_tempo_) {
        std::string lanes;
        for (const auto& l : sequence_.lanes) {
            if (!lanes.empty()) lanes += ", ";
            lanes += l.name + " " + std::to_string(l.notes.size());
        }
        lines.push_back("Timeline: " + fixed(tempo_.bpm_at(0.0), 2) + " BPM at bar 1, offset " +
                        fixed(tempo_.offset() + tempo_.nudge(), 3) + " s" + (tempo_from_file_ ? "" : " (drafted)") +
                        "; notes: " + (lanes.empty() ? std::string("none") : lanes));
    }
    for (const auto& w : graph_warnings_) lines.push_back("Graph warning: " + w);
    return lines;
}

} // namespace ztrip::host
