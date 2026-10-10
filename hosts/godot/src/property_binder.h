// Generic property binder: writes graph outputs to scene properties.
#pragma once

#include "ztrip/graph/evaluator.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace ztrip::godot_host {

// Resolves graph target paths relative to a root node and writes values to
// them. A target is "<node path>:<property path>", e.g.
//   Lights/Key:light_energy
//   Lights/Key:light_color:h                         (one component)
//   Orb:material_override:shader_parameter/glow      (a ShaderMaterial uniform)
//   Camera:position:x
//   @globals:pulse                                   (a global shader parameter)
// The property path is anything Object.set_indexed accepts. Float, int and
// bool properties are supported; colors and vectors need a component.
//
// The value a property had before the binder first wrote to it is kept, and
// restore() writes it back, so turning the graph off leaves the scene as it was.
class PropertyBinder {
public:
    void set_root(godot::Node* root);

    // Writes each output, resolving targets the first time they are seen.
    void apply(const std::vector<graph::TargetValue>& outputs);

    // Writes the original values back and forgets every binding, so the next
    // apply() re-resolves (and re-reads the base values).
    void restore();

    int bound_count() const;
    // One line per target that could not be bound.
    godot::PackedStringArray problems() const;

private:
    struct Binding {
        bool ok = false;
        bool global = false;
        godot::ObjectID object;
        godot::NodePath property;
        godot::StringName global_name;
        godot::Variant::Type type = godot::Variant::NIL;
        godot::Variant base;
        godot::String problem;
    };

    Binding resolve(const std::string& target) const;
    static void write(Binding& b, double value);

    godot::ObjectID root_;
    std::unordered_map<std::string, Binding> bindings_;
    std::vector<std::string> order_; // targets in first-seen order, for restore
};

} // namespace ztrip::godot_host
