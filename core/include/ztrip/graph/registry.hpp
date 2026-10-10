#pragma once

#include "ztrip/graph/node.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace ztrip::graph {

// Node types by name. Spec pointers stay valid for the registry's lifetime.
class NodeRegistry {
public:
    // Adds a node type. Appends a Number param for every Continuous input that
    // doesn't declare one, defaulting to the port's default value. Throws
    // std::invalid_argument on a duplicate or malformed spec.
    void add(NodeSpec spec);

    const NodeSpec* find(std::string_view type) const noexcept;
    std::vector<const NodeSpec*> specs() const;

    // The v1 catalog (see docs/NODES.md).
    static const NodeRegistry& builtin();

private:
    std::vector<std::unique_ptr<NodeSpec>> specs_;
};

void register_builtin_nodes(NodeRegistry& registry);

} // namespace ztrip::graph
