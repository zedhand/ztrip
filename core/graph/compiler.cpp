#include "ztrip/graph/compiler.hpp"

#include "ztrip/analysis/offline.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace ztrip::graph {
namespace {

bool kind_matches(ParamKind kind, const ParamValue& v) {
    switch (kind) {
    case ParamKind::Number: return std::holds_alternative<double>(v);
    case ParamKind::Bool: return std::holds_alternative<bool>(v);
    case ParamKind::String: return std::holds_alternative<std::string>(v);
    case ParamKind::List: return std::holds_alternative<std::vector<double>>(v);
    }
    return false;
}

std::string_view kind_name(ParamKind kind) {
    switch (kind) {
    case ParamKind::Number: return "a number";
    case ParamKind::Bool: return "a bool";
    case ParamKind::String: return "a string";
    case ParamKind::List: return "a list of numbers";
    }
    return "?";
}

bool can_connect(PortType from, PortType to) {
    switch (to) {
    case PortType::Continuous: return true;
    case PortType::Trigger: return from == PortType::Trigger;
    case PortType::Clock: return from != PortType::Trigger;
    }
    return false;
}

class Builder {
public:
    Builder(const GraphDoc& doc, const NodeRegistry& registry) : doc_(doc), registry_(registry) {}

    CompileResult run() {
        check_nodes();
        check_edges();
        const std::vector<std::size_t> order = topo_order();
        if (has_errors()) return finish(nullptr);
        auto graph = std::make_unique<CompiledGraph>();
        build(*graph, order);
        check_macros(*graph);
        return finish(has_errors() ? nullptr : std::move(graph));
    }

private:
    void error(std::string node, std::string message) {
        diags_.push_back({Diagnostic::Severity::Error, std::move(node), std::move(message)});
    }
    void warning(std::string node, std::string message) {
        diags_.push_back({Diagnostic::Severity::Warning, std::move(node), std::move(message)});
    }
    bool has_errors() const {
        return std::any_of(diags_.begin(), diags_.end(),
                           [](const Diagnostic& d) { return d.severity == Diagnostic::Severity::Error; });
    }
    CompileResult finish(std::unique_ptr<CompiledGraph> graph) {
        return {std::move(graph), std::move(diags_)};
    }

    void check_nodes() {
        specs_.resize(doc_.nodes.size(), nullptr);
        for (std::size_t i = 0; i < doc_.nodes.size(); ++i) {
            const NodeDoc& n = doc_.nodes[i];
            if (n.id.empty()) {
                error("", "node " + std::to_string(i) + " has an empty id");
                continue;
            }
            if (n.id.find('.') != std::string::npos) error(n.id, "node ids can't contain '.'");
            if (!index_.emplace(n.id, i).second) {
                error(n.id, "duplicate node id");
                continue;
            }
            const NodeSpec* spec = registry_.find(n.type);
            if (!spec) {
                error(n.id, "unknown node type '" + n.type + "'");
                continue;
            }
            specs_[i] = spec;
            for (const auto& [name, value] : n.params) {
                const int p = spec->param_index(name);
                if (p < 0) {
                    warning(n.id, "unknown param '" + name + "' ignored");
                    continue;
                }
                const ParamSpec& ps = spec->params[static_cast<std::size_t>(p)];
                if (!kind_matches(ps.kind, value)) {
                    error(n.id, "param '" + name + "' must be " + std::string(kind_name(ps.kind)));
                } else if (const double* d = std::get_if<double>(&value); d && !std::isfinite(*d)) {
                    error(n.id, "param '" + name + "' must be finite");
                } else if (const auto* s = std::get_if<std::string>(&value);
                           s && !ps.choices.empty() &&
                           std::find(ps.choices.begin(), ps.choices.end(), *s) == ps.choices.end()) {
                    std::string list;
                    for (const auto& c : ps.choices) list += (list.empty() ? "" : ", ") + c;
                    error(n.id, "param '" + name + "' must be one of: " + list);
                }
            }
        }
    }

    void check_edges() {
        incoming_.resize(doc_.nodes.size());
        for (const EdgeDoc& e : doc_.edges) {
            const std::string label = e.from_node + "." + e.from_port + " -> " + e.to_node + "." + e.to_port;
            const auto from = index_.find(e.from_node);
            const auto to = index_.find(e.to_node);
            if (from == index_.end() || to == index_.end()) {
                error("", "edge " + label + " names a missing node");
                continue;
            }
            const NodeSpec* fs = specs_[from->second];
            const NodeSpec* ts = specs_[to->second];
            if (!fs || !ts) continue; // already reported
            const int out = fs->output_index(e.from_port);
            const int in = ts->input_index(e.to_port);
            if (out < 0) {
                error(e.from_node, "edge " + label + ": no output port '" + e.from_port + "'");
                continue;
            }
            if (in < 0) {
                error(e.to_node, "edge " + label + ": no input port '" + e.to_port + "'");
                continue;
            }
            const PortType ft = fs->outputs[static_cast<std::size_t>(out)].type;
            const PortType tt = ts->inputs[static_cast<std::size_t>(in)].type;
            if (!can_connect(ft, tt)) {
                error(e.to_node, "edge " + label + ": can't connect " + std::string(to_string(ft)) + " to " +
                                     std::string(to_string(tt)));
                continue;
            }
            auto& slots = incoming_[to->second];
            const bool taken = std::any_of(slots.begin(), slots.end(), [&](const Incoming& s) { return s.in == in; });
            if (taken) {
                error(e.to_node, "input '" + e.to_port + "' has more than one edge");
                continue;
            }
            slots.push_back({in, from->second, out});
        }
    }

    std::vector<std::size_t> topo_order() {
        const std::size_t n = doc_.nodes.size();
        std::vector<int> pending(n, 0);
        std::vector<std::vector<std::size_t>> users(n);
        for (std::size_t i = 0; i < n; ++i) {
            for (const Incoming& s : incoming_[i]) {
                ++pending[i];
                users[s.from].push_back(i);
            }
        }
        std::set<std::size_t> ready;
        for (std::size_t i = 0; i < n; ++i) {
            if (pending[i] == 0) ready.insert(i);
        }
        std::vector<std::size_t> order;
        while (!ready.empty()) {
            const std::size_t i = *ready.begin();
            ready.erase(ready.begin());
            order.push_back(i);
            for (std::size_t u : users[i]) {
                if (--pending[u] == 0) ready.insert(u);
            }
        }
        if (order.size() != n) {
            std::string ids;
            for (std::size_t i = 0; i < n; ++i) {
                if (pending[i] > 0) ids += (ids.empty() ? "" : ", ") + doc_.nodes[i].id;
            }
            error("", "graph has a cycle through: " + ids);
        }
        return order;
    }

    void build(CompiledGraph& g, const std::vector<std::size_t>& order) {
        std::vector<std::size_t> value_offset(doc_.nodes.size(), 0);
        for (std::size_t i : order) {
            const NodeDoc& doc = doc_.nodes[i];
            const NodeSpec& spec = *specs_[i];
            CompiledNode cn;
            cn.id = doc.id;
            cn.spec = &spec;
            cn.value_offset = g.value_count;
            value_offset[i] = g.value_count;
            g.value_count += spec.outputs.size();
            cn.state_offset = g.state_size;
            g.state_size += spec.state_size;

            NodeSetup setup;
            setup.spec = &spec;
            setup.doc = &doc;
            setup.input_connected.assign(spec.inputs.size(), false);
            setup.seed = analysis::fnv1a(doc.id.data(), doc.id.size());

            cn.inputs.resize(spec.inputs.size());
            for (std::size_t k = 0; k < spec.inputs.size(); ++k) {
                if (spec.inputs[k].type == PortType::Continuous) cn.inputs[k].param = spec.param_index(spec.inputs[k].name);
            }
            for (const Incoming& s : incoming_[i]) {
                auto& src = cn.inputs[static_cast<std::size_t>(s.in)];
                src.value = static_cast<int>(value_offset[s.from]) + s.out;
                setup.input_connected[static_cast<std::size_t>(s.in)] = true;
            }
            cn.numbers.assign(spec.params.size(), 0.0);
            for (std::size_t p = 0; p < spec.params.size(); ++p) {
                if (spec.params[p].kind == ParamKind::Number) cn.numbers[p] = setup.number(spec.params[p].name);
            }
            if (spec.is_target) {
                cn.target = setup.string("target");
                if (cn.target.empty()) warning(doc.id, "target node has no 'target' set");
            }
            try {
                cn.impl = spec.factory(setup);
            } catch (const std::exception& ex) {
                error(doc.id, ex.what());
            }
            g.nodes.push_back(std::move(cn));
        }
    }

    void check_macros(CompiledGraph& g) {
        std::set<std::string> names;
        for (const MacroDoc& m : doc_.macros) {
            if (m.name.empty() || !names.insert(m.name).second) {
                error("", "macro '" + m.name + "' needs a unique, non-empty name");
                continue;
            }
            const int node = g.find_node(m.node);
            if (node < 0) {
                error("", "macro '" + m.name + "' names missing node '" + m.node + "'");
                continue;
            }
            CompiledNode& cn = g.nodes[static_cast<std::size_t>(node)];
            const int p = cn.spec->param_index(m.param);
            if (p < 0 || cn.spec->params[static_cast<std::size_t>(p)].kind != ParamKind::Number) {
                error(m.node, "macro '" + m.name + "' must drive a number param, not '" + m.param + "'");
                continue;
            }
            if (!(m.min <= m.max) || !std::isfinite(m.min) || !std::isfinite(m.max) || !std::isfinite(m.default_value)) {
                error("", "macro '" + m.name + "' needs finite min <= max and a finite default");
                continue;
            }
            const double value = std::clamp(m.default_value, m.min, m.max);
            cn.numbers[static_cast<std::size_t>(p)] = value;
            g.macros.push_back({m.name, static_cast<std::size_t>(node), static_cast<std::size_t>(p), m.min, m.max, value});
        }
    }

    struct Incoming {
        int in = 0;           // input port on this node
        std::size_t from = 0; // source node (document index)
        int out = 0;          // output port on the source node
    };

    const GraphDoc& doc_;
    const NodeRegistry& registry_;
    std::vector<Diagnostic> diags_;
    std::map<std::string, std::size_t> index_;
    std::vector<const NodeSpec*> specs_;
    std::vector<std::vector<Incoming>> incoming_;
};

} // namespace

int CompiledGraph::find_node(std::string_view id) const noexcept {
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

std::string CompileResult::error_text() const {
    std::string text;
    for (const Diagnostic& d : diagnostics) {
        text += d.severity == Diagnostic::Severity::Error ? "error: " : "warning: ";
        if (!d.node.empty()) text += d.node + ": ";
        text += d.message + "\n";
    }
    return text;
}

CompileResult compile(const GraphDoc& doc, const NodeRegistry& registry) {
    return Builder(doc, registry).run();
}

} // namespace ztrip::graph
