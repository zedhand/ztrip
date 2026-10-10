#pragma once

#include "ztrip/graph/document.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ztrip::analysis {
struct AnalysisResult;
}

namespace ztrip::time {
class TempoMap;
struct NoteSequence;
}

namespace ztrip::io {
class ControlSet;
}

namespace ztrip::graph {

// Port types. Continuous is a float per tick; Trigger fires on the tick an
// event lands in, carrying its velocity; Clock carries a musical position in
// beats (beat 0 is the first downbeat, so bars start at multiples of the
// beats per bar).
enum class PortType { Continuous, Trigger, Clock };

std::string_view to_string(PortType type) noexcept;

// The value on a port for one tick. Continuous: v. Trigger: fired, and v is
// the velocity (0 when not fired). Clock: v is the beat position.
struct Value {
    double v = 0.0;
    bool fired = false;
};

struct PortSpec {
    std::string name;
    PortType type = PortType::Continuous;
    // Unconnected Continuous inputs read the node's number param of the same
    // name, which defaults to this value (so they can also be macros).
    double default_value = 0.0;
};

enum class ParamKind { Number, Bool, String, List };

struct ParamSpec {
    std::string name;
    ParamKind kind = ParamKind::Number;
    ParamValue default_value = 0.0;
    std::vector<std::string> choices; // allowed values for String params, if restricted
};

// Named inputs a graph reads from, by slot (e.g. "main", "drums"): analysis
// results, tempo maps, Note Sequences and live controls (MIDI, OSC, DMX),
// each with its own slot names.
// Rebinding a slot repoints every source node that reads it.
class SourceSet {
public:
    void bind(std::string slot, const analysis::AnalysisResult* result);
    const analysis::AnalysisResult* find(std::string_view slot) const noexcept;

    void bind_tempo(std::string slot, const time::TempoMap* tempo);
    const time::TempoMap* find_tempo(std::string_view slot) const noexcept;

    void bind_sequence(std::string slot, const time::NoteSequence* sequence);
    const time::NoteSequence* find_sequence(std::string_view slot) const noexcept;

    void bind_controls(std::string slot, const io::ControlSet* controls);
    const io::ControlSet* find_controls(std::string_view slot) const noexcept;

private:
    std::vector<std::pair<std::string, const analysis::AnalysisResult*>> slots_;
    std::vector<std::pair<std::string, const time::TempoMap*>> tempos_;
    std::vector<std::pair<std::string, const time::NoteSequence*>> sequences_;
    std::vector<std::pair<std::string, const io::ControlSet*>> controls_;
};

// Everything a node may read while evaluating one tick.
struct EvalContext {
    std::int64_t step = 0; // tick index; tick n is at time n / rate_hz
    double time = 0.0;     // seconds
    double dt = 0.0;       // seconds per tick
    double rate_hz = 120.0;
    bool live = false;     // causal sources only, no lookahead
    const SourceSet* sources = nullptr;
};

struct NodeSpec;

// What a node sees when the graph is compiled.
struct NodeSetup {
    const NodeSpec* spec = nullptr;
    const NodeDoc* doc = nullptr;
    std::vector<bool> input_connected;   // per input port
    std::uint64_t seed = 0;              // stable per node id, for random nodes

    double number(std::string_view param) const;  // param or its spec default
    bool boolean(std::string_view param) const;
    const std::string& string(std::string_view param) const;
    const std::vector<double>& list(std::string_view param) const;
};

// Per-tick data a node reads and writes. Number params are resolved into an
// array (indexed like NodeSpec::params) so macros can change them without a
// recompile; state is a slice of the evaluator's state buffer, which is what
// gets checkpointed for seeking.
struct NodeIO {
    std::span<const Value> in;
    std::span<Value> out;
    std::span<double> state;
    std::span<const double> numbers;
};

// A node implementation. Evaluate must be a pure function of (context,
// inputs, numbers, state) and may only carry information between ticks in
// `state`, which is what makes seeking deterministic.
class Node {
public:
    virtual ~Node() = default;

    // Repoints source nodes when a slot is (re)bound. Called after compile and
    // on every rebind.
    virtual void bind(const SourceSet&) {}

    // Initial state, before the first tick. Zero-filled by default.
    virtual void reset(std::span<double> state) const;

    virtual void evaluate(const EvalContext& ctx, NodeIO& io) const = 0;
};

using NodeFactory = std::function<std::unique_ptr<Node>(const NodeSetup&)>;

struct NodeSpec {
    std::string type;
    std::string category;
    std::string summary;
    std::vector<PortSpec> inputs;
    std::vector<PortSpec> outputs;
    std::vector<ParamSpec> params; // includes one Number param per Continuous input
    std::size_t state_size = 0;    // doubles of per-node state
    bool is_target = false;        // a sink whose first input is a graph output, named by its "target" param
    NodeFactory factory;

    int input_index(std::string_view name) const noexcept;
    int output_index(std::string_view name) const noexcept;
    int param_index(std::string_view name) const noexcept;
};

} // namespace ztrip::graph
