#pragma once

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace ztrip::graph {

// A node parameter as authored. Numbers are the only kind macros can drive;
// lists hold flat number arrays such as curve points (x0, y0, x1, y1, ...).
using ParamValue = std::variant<double, bool, std::string, std::vector<double>>;

struct NodeDoc {
    std::string id;   // unique in the graph, no '.'
    std::string type; // registry name, e.g. "Smooth"
    std::map<std::string, ParamValue> params;
    float x = 0.0f;   // editor position
    float y = 0.0f;
};

// An edge from an output port to an input port, written "node.port" in JSON.
struct EdgeDoc {
    std::string from_node;
    std::string from_port;
    std::string to_node;
    std::string to_port;
};

// A node number param promoted to a graph-level control.
struct MacroDoc {
    std::string name;
    std::string node;
    std::string param;
    double min = 0.0;
    double max = 1.0;
    double default_value = 0.0;
};

// In-memory form of a .mgraph.json document.
struct GraphDoc {
    std::string name;
    std::vector<NodeDoc> nodes;
    std::vector<EdgeDoc> edges;
    std::vector<MacroDoc> macros;

    const NodeDoc* find_node(const std::string& id) const noexcept;
};

} // namespace ztrip::graph
