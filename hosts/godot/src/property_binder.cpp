#include "property_binder.h"

#include "convert.h"

#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/object.hpp>

#include <cmath>

namespace ztrip::godot_host {

using namespace godot;

void PropertyBinder::set_root(Node* root) {
    const ObjectID id = root ? ObjectID(root->get_instance_id()) : ObjectID();
    if (id == root_) return;
    restore();
    root_ = id;
}

PropertyBinder::Binding PropertyBinder::resolve(const std::string& target) const {
    Binding b;
    const auto colon = target.find(':');
    if (target.empty() || colon == std::string::npos || colon + 1 >= target.size()) {
        b.problem = to_godot("\"" + target + "\": expected <node path>:<property>, e.g. Lights/Key:light_energy");
        return b;
    }
    const String node_path = to_godot(target.substr(0, colon));
    const String property = to_godot(target.substr(colon + 1));

    if (node_path == "@globals") {
        b.global = true;
        b.global_name = property;
        b.base = RenderingServer::get_singleton()->global_shader_parameter_get(b.global_name);
        b.type = b.base.get_type();
        if (b.type == Variant::NIL) {
            b.problem = "\"" + to_godot(target) + "\": no global shader parameter named " + property;
            return b;
        }
    } else {
        Node* root = Object::cast_to<Node>(ObjectDB::get_instance(static_cast<uint64_t>(root_)));
        if (!root) {
            b.problem = "\"" + to_godot(target) + "\": no root node";
            return b;
        }
        Node* node = root->get_node_or_null(NodePath(node_path));
        if (!node) {
            b.problem = "\"" + to_godot(target) + "\": no node at " + node_path;
            return b;
        }
        b.object = ObjectID(node->get_instance_id());
        b.property = NodePath(property);
        b.base = node->get_indexed(b.property);
        b.type = b.base.get_type();
        // A shader uniform left at its default reads as null when the renderer
        // can't report defaults (headless); restoring null puts the default back.
        if (b.type == Variant::NIL && property.get_slice(":", property.get_slice_count(":") - 1).begins_with("shader_parameter/"))
            b.type = Variant::FLOAT;
        if (b.type == Variant::NIL) {
            b.problem = "\"" + to_godot(target) + "\": " + node->get_class() + " has no property " + property;
            return b;
        }
    }
    if (b.type != Variant::FLOAT && b.type != Variant::INT && b.type != Variant::BOOL) {
        b.problem = "\"" + to_godot(target) + "\" is a " + Variant::get_type_name(b.type) +
                    "; target one component instead, e.g. :x or :r or :h";
        return b;
    }
    b.ok = true;
    return b;
}

void PropertyBinder::write(Binding& b, double value) {
    Variant v;
    switch (b.type) {
    case Variant::INT: v = static_cast<int64_t>(std::llround(value)); break;
    case Variant::BOOL: v = value >= 0.5; break;
    default: v = value; break;
    }
    if (b.global) {
        RenderingServer::get_singleton()->global_shader_parameter_set(b.global_name, v);
        return;
    }
    Object* obj = ObjectDB::get_instance(static_cast<uint64_t>(b.object));
    if (!obj) {
        b.ok = false;
        b.problem = "target node was freed";
        return;
    }
    obj->set_indexed(b.property, v);
}

void PropertyBinder::apply(const std::vector<graph::TargetValue>& outputs) {
    for (const auto& out : outputs) {
        auto it = bindings_.find(out.target);
        if (it == bindings_.end()) {
            it = bindings_.emplace(out.target, resolve(out.target)).first;
            order_.push_back(out.target);
        }
        if (it->second.ok && std::isfinite(out.value)) write(it->second, out.value);
    }
}

void PropertyBinder::restore() {
    // Newest first, so when two targets share a property the oldest base wins.
    for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
        Binding& b = bindings_[*it];
        if (!b.ok) continue;
        if (b.global) {
            RenderingServer::get_singleton()->global_shader_parameter_set(b.global_name, b.base);
        } else if (Object* obj = ObjectDB::get_instance(static_cast<uint64_t>(b.object))) {
            obj->set_indexed(b.property, b.base);
        }
    }
    bindings_.clear();
    order_.clear();
}

int PropertyBinder::bound_count() const {
    int n = 0;
    for (const auto& [target, b] : bindings_) n += b.ok ? 1 : 0;
    return n;
}

PackedStringArray PropertyBinder::problems() const {
    PackedStringArray out;
    for (const auto& target : order_) {
        const Binding& b = bindings_.at(target);
        if (!b.ok) out.push_back(b.problem);
    }
    return out;
}

} // namespace ztrip::godot_host
