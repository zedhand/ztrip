#pragma once

#include "ztrip/graph/document.hpp"
#include "ztrip/graph/node.hpp"
#include "ztrip/graph/registry.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ztrip::graph {

struct Diagnostic {
    enum class Severity { Error, Warning };
    Severity severity = Severity::Error;
    std::string node; // node id, or empty for graph-level problems
    std::string message;
};

// Where a compiled input reads from: another node's output value, or (when
// unconnected) one of its own number params.
struct InputSource {
    int value = -1; // index into the evaluator's value array, -1 if unconnected
    int param = -1; // number param read when unconnected, -1 for none (value 0)
};

struct CompiledNode {
    std::string id;
    const NodeSpec* spec = nullptr;
    std::unique_ptr<Node> impl;
    std::vector<InputSource> inputs;
    std::size_t value_offset = 0; // first output's index in the value array
    std::size_t state_offset = 0;
    std::vector<double> numbers;  // Number params by spec index (others 0)
    std::string target;           // for target nodes
};

struct CompiledMacro {
    std::string name;
    std::size_t node = 0;  // index into CompiledGraph::nodes
    std::size_t param = 0; // spec param index
    double min = 0.0;
    double max = 1.0;
    double default_value = 0.0;
};

// A validated graph in evaluation (topological) order with its value and
// state layout. Owned by one evaluator, since source nodes hold bindings.
struct CompiledGraph {
    std::vector<CompiledNode> nodes;
    std::vector<CompiledMacro> macros;
    std::size_t value_count = 0;
    std::size_t state_size = 0;

    int find_node(std::string_view id) const noexcept;
};

struct CompileResult {
    std::unique_ptr<CompiledGraph> graph; // null when there are errors
    std::vector<Diagnostic> diagnostics;

    bool ok() const noexcept { return graph != nullptr; }
    std::string error_text() const; // one line per diagnostic
};

// Validates node types, ids, params, edges, port types and macros, rejects
// cycles, and topo-sorts (ties keep document order, so compilation is
// deterministic). Trigger and Clock outputs may feed Continuous inputs (as
// velocity and beat position) and Continuous may feed Clock; Trigger inputs
// only accept Trigger outputs.
CompileResult compile(const GraphDoc& doc, const NodeRegistry& registry = NodeRegistry::builtin());

} // namespace ztrip::graph
