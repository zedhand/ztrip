// ZtripGraphTools: the node catalog and .mgraph.json checks for editors.
#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace godot {

class ZtripGraphTools : public Object {
    GDCLASS(ZtripGraphTools, Object)

public:
    // One Dictionary per node type: type, category, summary,
    // inputs/outputs [{name, type}], params [{name, kind, default, choices}].
    // Port types are "Continuous", "Trigger" or "Clock"; param kinds "Number",
    // "Bool", "String" or "List".
    static Array get_node_types();

    // Parses and compiles a graph: {ok, errors, warnings, targets, macros}.
    static Dictionary check_graph(const String& json);

    // Re-writes a graph in the canonical format (stable key order):
    // {ok, text, error}.
    static Dictionary normalize_graph(const String& json);

    static String get_version();

protected:
    static void _bind_methods();
};

} // namespace godot
