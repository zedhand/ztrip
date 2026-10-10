#pragma once

#include "ztrip/graph/compiler.hpp"
#include "ztrip/graph/node.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ztrip::graph {

struct EvaluatorSettings {
    // Fixed tick rate. Stateful nodes always step on this grid, whatever the
    // host frame rate, so output at time t doesn't depend on how it was reached.
    double rate_hz = 120.0;
    double checkpoint_interval_s = 1.0;
    // Live: causal, no checkpoints; a backwards jump resets state instead of
    // replaying.
    bool live = false;
};

struct TargetValue {
    std::string target;
    double value = 0.0;
};

// Runs a compiled graph over time. Offline, evaluate(t) gives the same
// result whether t was reached by playing from 0 or by seeking: state is
// checkpointed every checkpoint_interval_s while playing, and a seek replays
// from the nearest checkpoint at or before t.
class Evaluator {
public:
    explicit Evaluator(std::unique_ptr<CompiledGraph> graph, EvaluatorSettings settings = {});

    // Binds an analysis result to a source slot (null unbinds). Clears
    // checkpoints, since earlier state was computed from other signals.
    void bind_source(std::string slot, const analysis::AnalysisResult* result);

    // Binds a tempo map (BeatClock and NoteLane read it in place of the
    // analysed beat grid) or a Note Sequence to a slot; null unbinds. Like
    // bind_source this clears checkpoints. Nodes resolve notes to seconds
    // when bound, so after editing a bound tempo map or sequence, bind it
    // again.
    void bind_tempo(std::string slot, const time::TempoMap* tempo);
    void bind_sequence(std::string slot, const time::NoteSequence* sequence);

    // Binds live controls (MIDI, OSC, DMX) to a slot; null unbinds. Recording
    // new control input needs no rebind: nodes look controls up by name.
    void bind_controls(std::string slot, const io::ControlSet* controls);

    // When to record control input that arrives now, if the next evaluate is
    // at t: the tick that evaluate reaches, or the first tick not evaluated
    // yet if that one already was. Input then shows on the next evaluate (or
    // the one after, never on a tick already passed), so no trigger is missed
    // and no change waits a frame longer than it must.
    double stamp_time(double t) const noexcept;

    // Sets a macro, clamped to its range. Returns false for unknown names.
    // Clears checkpoints: playing on continues from the current state, while
    // a seek, or evaluating the current time again, replays with the new value.
    bool set_macro(std::string_view name, double value);
    std::optional<double> macro(std::string_view name) const;

    // Hot-swaps the graph. Nodes whose id and type persist keep their state
    // and current port values; macros keep their values where the name
    // persists. Returns the old graph.
    std::unique_ptr<CompiledGraph> swap_graph(std::unique_ptr<CompiledGraph> graph);

    // Evaluates at time t (seconds): tick floor(t * rate_hz), clamped to >= 0.
    void evaluate(double t);

    // Back to the initial state with no tick evaluated.
    void reset();

    std::int64_t current_step() const noexcept { return step_; }
    double current_time() const noexcept;
    const CompiledGraph& graph() const noexcept { return *graph_; }
    const EvaluatorSettings& settings() const noexcept { return settings_; }

    // Target node values after the last evaluate, in evaluation order.
    const std::vector<TargetValue>& outputs() const noexcept { return outputs_; }
    std::optional<double> output(std::string_view target) const;

    // Any port's value after the last evaluate (for previews and tests).
    std::optional<Value> port_value(std::string_view node, std::string_view port) const;

    // Total ticks evaluated since construction (to observe replay cost).
    std::uint64_t ticks_evaluated() const noexcept { return ticks_; }

private:
    void tick(std::int64_t step);
    void clear_checkpoints();

    std::unique_ptr<CompiledGraph> graph_;
    EvaluatorSettings settings_;
    SourceSet sources_;
    std::int64_t checkpoint_every_ = 120;
    std::int64_t step_ = -1;          // last evaluated tick, -1 for none
    bool dirty_ = false;              // settings changed since the last evaluate
    std::vector<double> state_;       // state after tick step_
    std::vector<Value> values_;       // port values of tick step_
    std::vector<Value> scratch_;      // gathered inputs for one node
    std::map<std::int64_t, std::vector<double>> checkpoints_; // state before tick k
    std::vector<TargetValue> outputs_;
    std::uint64_t ticks_ = 0;
};

} // namespace ztrip::graph
