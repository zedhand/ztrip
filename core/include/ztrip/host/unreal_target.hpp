#pragma once
// Target paths of the Unreal Engine host, parsed without the engine so the
// syntax is tested with the rest of the core. A PropertyOut target is
//
//   <actor>[/<component>]:<property>
//   @mpc[/<collection>]:<parameter>
//
// <actor> is an actor's label (as in the Outliner), name or tag. With no
// <component>, a plain property is looked up on the actor, then on its
// components in order; <component> is a component's name or class name
// (PointLightComponent). <property> is one of:
//
//   Intensity, LightColor.R            a property, then struct fields
//   location.x, rotation.yaw, scale.z  the transform (relative to the parent;
//                                      rotation takes roll/pitch/yaw or x/y/z)
//   material.Glow, material[1].Glow    a scalar parameter on dynamic material
//                                      instances of every slot, or of one
//   niagara.Rate                       a Niagara user parameter (User.Rate)
//   @burst                             restarts a Niagara or Cascade system
//                                      each time the value rises through 0.5
//   @call.OnBeat                       calls a function (Blueprint or C++)
//                                      taking one number when the value changes
//
// and @mpc sets a scalar parameter of the player's Material Parameter
// Collection (or of the named one, when it has several).

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ztrip::host {

struct UnrealTarget {
    enum class Kind { Property, Location, Rotation, Scale, Material, Niagara, Burst, Call, Collection };

    Kind kind = Kind::Property;
    std::string actor;             // empty for Collection
    std::string component;         // empty: the actor, then its components
    std::vector<std::string> path; // Property: property, then struct fields
    int axis = 0;                  // Location, Scale: x y z; Rotation: roll pitch yaw (0..2)
    int slot = -1;                 // Material: element index, -1 for every slot
    std::string parameter;         // Material, Niagara, Collection parameter; Call function name
    std::string collection;        // Collection: which of the player's collections, empty for the first
};

// Parses a target. Returns nullopt with `error` set when it is malformed.
std::optional<UnrealTarget> parse_unreal_target(std::string_view text, std::string& error);

std::string to_string(const UnrealTarget& target);

} // namespace ztrip::host
