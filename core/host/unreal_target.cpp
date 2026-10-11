#include "ztrip/host/unreal_target.hpp"

#include <array>
#include <charconv>
#include <system_error>

namespace ztrip::host {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

bool split_dots(std::string_view s, std::vector<std::string>& out) {
    out.clear();
    while (true) {
        const auto dot = s.find('.');
        const std::string_view part = s.substr(0, dot);
        if (part.empty()) return false;
        out.emplace_back(part);
        if (dot == std::string_view::npos) return true;
        s.remove_prefix(dot + 1);
    }
}

int axis_index(std::string_view kind, std::string_view axis) {
    static constexpr std::array<std::string_view, 3> xyz{"x", "y", "z"};
    static constexpr std::array<std::string_view, 3> rpy{"roll", "pitch", "yaw"};
    for (int i = 0; i < 3; ++i) {
        if (axis == xyz[static_cast<std::size_t>(i)]) return i;
        if (kind == "rotation" && axis == rpy[static_cast<std::size_t>(i)]) return i;
    }
    return -1;
}

bool is_identifier(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

constexpr std::array<std::string_view, 3> kAxisNames{"x", "y", "z"};
constexpr std::array<std::string_view, 3> kRotationNames{"roll", "pitch", "yaw"};

} // namespace

std::optional<UnrealTarget> parse_unreal_target(std::string_view text, std::string& error) {
    const std::string_view full = trim(text);
    const auto fail = [&](std::string why) -> std::optional<UnrealTarget> {
        error = "\"" + std::string(full) + "\": " + std::move(why);
        return std::nullopt;
    };
    const auto colon = full.find(':');
    if (colon == std::string_view::npos) return fail("expected <actor>:<property> or @mpc:<parameter>");
    const std::string_view object = trim(full.substr(0, colon));
    const std::string_view property = trim(full.substr(colon + 1));
    if (property.empty()) return fail("nothing after ':'");
    if (property.find_first_of(":/") != std::string_view::npos)
        return fail("a property path has no ':' or '/' (a target from another host? map it in the player's Target Map)");

    UnrealTarget t;
    const auto slash = object.find('/');
    const std::string_view first = trim(object.substr(0, slash));
    const std::string_view second = slash == std::string_view::npos ? std::string_view{} : trim(object.substr(slash + 1));
    if (slash != std::string_view::npos && (second.empty() || second.find('/') != std::string_view::npos))
        return fail("expected <actor>/<component> before ':'");

    if (first == "@mpc") {
        t.kind = UnrealTarget::Kind::Collection;
        t.collection = std::string(second);
        if (property.find('.') != std::string_view::npos) return fail("a collection parameter has no fields");
        t.parameter = std::string(property);
        return t;
    }
    if (first.empty()) return fail("no actor before ':'");
    if (first.front() == '@') return fail("unknown target kind " + std::string(first) + " (did you mean @mpc?)");
    t.actor = std::string(first);
    t.component = std::string(second);

    if (property.front() == '@') {
        if (property == "@burst") {
            t.kind = UnrealTarget::Kind::Burst;
            return t;
        }
        if (property.starts_with("@call.") && property.size() > 6) {
            t.kind = UnrealTarget::Kind::Call;
            t.parameter = std::string(property.substr(6));
            if (t.parameter.find('.') != std::string::npos) return fail("a function name has no '.'");
            return t;
        }
        return fail("unknown action " + std::string(property) + " (expected @burst or @call.<function>)");
    }

    std::vector<std::string> parts;
    if (!split_dots(property, parts)) return fail("empty name in the property path");
    const std::string& head = parts.front();

    if (head == "location" || head == "rotation" || head == "scale") {
        if (parts.size() != 2) return fail(head + " takes one axis, e.g. " + head + ".x");
        t.kind = head == "location" ? UnrealTarget::Kind::Location
               : head == "rotation" ? UnrealTarget::Kind::Rotation
                                    : UnrealTarget::Kind::Scale;
        t.axis = axis_index(head, parts[1]);
        if (t.axis < 0)
            return fail("unknown axis " + parts[1] + (head == "rotation" ? " (roll, pitch, yaw or x, y, z)" : " (x, y or z)"));
        return t;
    }
    if (head == "niagara") {
        if (parts.size() < 2) return fail("niagara takes a parameter, e.g. niagara.Rate");
        t.kind = UnrealTarget::Kind::Niagara;
        // "niagara.User.Rate" and "niagara.Rate" both name User.Rate.
        std::string name;
        for (std::size_t i = 1; i < parts.size(); ++i) name += (i > 1 ? "." : "") + parts[i];
        t.parameter = name.starts_with("User.") ? name : "User." + name;
        return t;
    }
    if (head.starts_with("material") && (head.size() == 8 || head[8] == '[')) {
        t.kind = UnrealTarget::Kind::Material;
        if (head.size() > 8) {
            if (head.back() != ']') return fail("expected material[<slot>]");
            const std::string_view digits = std::string_view(head).substr(9, head.size() - 10);
            int slot = -1;
            const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), slot);
            if (digits.empty() || ec != std::errc{} || end != digits.data() + digits.size() || slot < 0)
                return fail("material slot must be a number 0 or more");
            t.slot = slot;
        }
        if (parts.size() != 2) return fail("material takes one parameter, e.g. material.Glow");
        t.parameter = parts[1];
        return t;
    }
    for (const auto& name : parts)
        if (!is_identifier(name)) return fail("\"" + name + "\" is not a property name");
    t.kind = UnrealTarget::Kind::Property;
    t.path = std::move(parts);
    return t;
}

std::string to_string(const UnrealTarget& t) {
    if (t.kind == UnrealTarget::Kind::Collection)
        return "@mpc" + (t.collection.empty() ? std::string() : "/" + t.collection) + ":" + t.parameter;
    std::string out = t.actor + (t.component.empty() ? std::string() : "/" + t.component) + ":";
    const auto axis = static_cast<std::size_t>(t.axis);
    switch (t.kind) {
    case UnrealTarget::Kind::Property:
        for (std::size_t i = 0; i < t.path.size(); ++i) out += (i ? "." : "") + t.path[i];
        break;
    case UnrealTarget::Kind::Location: out += "location." + std::string(kAxisNames[axis]); break;
    case UnrealTarget::Kind::Rotation: out += "rotation." + std::string(kRotationNames[axis]); break;
    case UnrealTarget::Kind::Scale: out += "scale." + std::string(kAxisNames[axis]); break;
    case UnrealTarget::Kind::Material:
        out += "material" + (t.slot >= 0 ? "[" + std::to_string(t.slot) + "]" : std::string()) + "." + t.parameter;
        break;
    case UnrealTarget::Kind::Niagara: out += "niagara." + t.parameter.substr(5); break;
    case UnrealTarget::Kind::Burst: out += "@burst"; break;
    case UnrealTarget::Kind::Call: out += "@call." + t.parameter; break;
    case UnrealTarget::Kind::Collection: break;
    }
    return out;
}

} // namespace ztrip::host
