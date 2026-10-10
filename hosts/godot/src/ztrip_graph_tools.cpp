#include "ztrip_graph_tools.h"

#include "convert.h"

#include "ztrip/graph/compiler.hpp"
#include "ztrip/graph/registry.hpp"
#include "ztrip/serialize/mgraph.hpp"
#include "ztrip/version.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace godot {

using ztrip::godot_host::to_godot;
using ztrip::godot_host::to_std;
using ztrip::godot_host::to_variant;

namespace {

const char* kind_name(ztrip::graph::ParamKind kind) {
    switch (kind) {
    case ztrip::graph::ParamKind::Number: return "Number";
    case ztrip::graph::ParamKind::Bool: return "Bool";
    case ztrip::graph::ParamKind::String: return "String";
    case ztrip::graph::ParamKind::List: return "List";
    }
    return "Number";
}

Array ports(const std::vector<ztrip::graph::PortSpec>& specs) {
    Array out;
    for (const auto& p : specs) {
        Dictionary d;
        d["name"] = to_godot(p.name);
        d["type"] = to_godot(std::string(ztrip::graph::to_string(p.type)));
        d["default"] = p.default_value;
        out.push_back(d);
    }
    return out;
}

} // namespace

void ZtripGraphTools::_bind_methods() {
    ClassDB::bind_static_method("ZtripGraphTools", D_METHOD("get_node_types"), &ZtripGraphTools::get_node_types);
    ClassDB::bind_static_method("ZtripGraphTools", D_METHOD("check_graph", "json"), &ZtripGraphTools::check_graph);
    ClassDB::bind_static_method("ZtripGraphTools", D_METHOD("normalize_graph", "json"),
                                &ZtripGraphTools::normalize_graph);
    ClassDB::bind_static_method("ZtripGraphTools", D_METHOD("get_version"), &ZtripGraphTools::get_version);
}

Array ZtripGraphTools::get_node_types() {
    Array out;
    for (const auto* spec : ztrip::graph::NodeRegistry::builtin().specs()) {
        Dictionary d;
        d["type"] = to_godot(spec->type);
        d["category"] = to_godot(spec->category);
        d["summary"] = to_godot(spec->summary);
        d["is_target"] = spec->is_target;
        d["inputs"] = ports(spec->inputs);
        d["outputs"] = ports(spec->outputs);
        Array params;
        for (const auto& p : spec->params) {
            Dictionary pd;
            pd["name"] = to_godot(p.name);
            pd["kind"] = kind_name(p.kind);
            pd["default"] = to_variant(p.default_value);
            PackedStringArray choices;
            for (const auto& c : p.choices) choices.push_back(to_godot(c));
            pd["choices"] = choices;
            params.push_back(pd);
        }
        d["params"] = params;
        out.push_back(d);
    }
    return out;
}

Dictionary ZtripGraphTools::check_graph(const String& json) {
    Dictionary out;
    PackedStringArray errors, warnings, targets, macros;
    bool ok = false;
    try {
        const auto doc = ztrip::serialize::parse_mgraph(to_std(json));
        const auto compiled = ztrip::graph::compile(doc);
        for (const auto& d : compiled.diagnostics) {
            String line = to_godot(d.node.empty() ? d.message : d.node + ": " + d.message);
            (d.severity == ztrip::graph::Diagnostic::Severity::Error ? errors : warnings).push_back(line);
        }
        if (compiled.ok()) {
            ok = true;
            for (const auto& n : compiled.graph->nodes)
                if (n.spec->is_target) targets.push_back(to_godot(n.target));
            for (const auto& m : compiled.graph->macros) macros.push_back(to_godot(m.name));
        }
    } catch (const std::exception& e) {
        errors.push_back(to_godot(e.what()));
    }
    out["ok"] = ok;
    out["errors"] = errors;
    out["warnings"] = warnings;
    out["targets"] = targets;
    out["macros"] = macros;
    return out;
}

Dictionary ZtripGraphTools::normalize_graph(const String& json) {
    Dictionary out;
    try {
        out["text"] = to_godot(ztrip::serialize::write_mgraph(ztrip::serialize::parse_mgraph(to_std(json))));
        out["ok"] = true;
        out["error"] = String();
    } catch (const std::exception& e) {
        out["text"] = String();
        out["ok"] = false;
        out["error"] = to_godot(e.what());
    }
    return out;
}

String ZtripGraphTools::get_version() { return to_godot(std::string(ztrip::version())); }

} // namespace godot
