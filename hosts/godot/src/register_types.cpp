// GDExtension entry point: registers the ztrip classes with Godot.
#include "ztrip_analysis.h"
#include "ztrip_graph_tools.h"
#include "ztrip_player.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

namespace {

void initialize_ztrip(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    GDREGISTER_CLASS(ZtripAnalysis);
    GDREGISTER_CLASS(ZtripGraphTools);
    GDREGISTER_CLASS(ZtripPlayer);
}

void uninitialize_ztrip(ModuleInitializationLevel) {}

} // namespace

extern "C" {

GDExtensionBool GDE_EXPORT ztrip_library_init(GDExtensionInterfaceGetProcAddress get_proc_address,
                                              GDExtensionClassLibraryPtr library,
                                              GDExtensionInitialization* initialization) {
    GDExtensionBinding::InitObject init(get_proc_address, library, initialization);
    init.register_initializer(initialize_ztrip);
    init.register_terminator(uninitialize_ztrip);
    init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init.init();
}

}
