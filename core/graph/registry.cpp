#include "ztrip/graph/registry.hpp"

#include <set>
#include <stdexcept>

namespace ztrip::graph {

void NodeRegistry::add(NodeSpec spec) {
    if (spec.type.empty() || !spec.factory) throw std::invalid_argument("node spec needs a type and a factory");
    if (find(spec.type)) throw std::invalid_argument("node type '" + spec.type + "' is already registered");
    if (spec.is_target && (spec.inputs.empty() || spec.param_index("target") < 0)) {
        throw std::invalid_argument("target node '" + spec.type + "' needs an input and a 'target' param");
    }
    for (const PortSpec& in : spec.inputs) {
        if (in.type == PortType::Continuous && spec.param_index(in.name) < 0) {
            spec.params.push_back({in.name, ParamKind::Number, in.default_value, {}});
        }
    }
    std::set<std::string> names;
    for (const auto& p : spec.params) {
        if (!names.insert(p.name).second) {
            throw std::invalid_argument("node type '" + spec.type + "' repeats param '" + p.name + "'");
        }
    }
    specs_.push_back(std::make_unique<NodeSpec>(std::move(spec)));
}

const NodeSpec* NodeRegistry::find(std::string_view type) const noexcept {
    for (const auto& s : specs_) {
        if (s->type == type) return s.get();
    }
    return nullptr;
}

std::vector<const NodeSpec*> NodeRegistry::specs() const {
    std::vector<const NodeSpec*> out;
    out.reserve(specs_.size());
    for (const auto& s : specs_) out.push_back(s.get());
    return out;
}

const NodeRegistry& NodeRegistry::builtin() {
    static const NodeRegistry registry = [] {
        NodeRegistry r;
        register_builtin_nodes(r);
        return r;
    }();
    return registry;
}

} // namespace ztrip::graph
