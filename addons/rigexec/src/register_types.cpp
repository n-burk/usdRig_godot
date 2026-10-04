#include "rigexec_character.h"
#include "rigexec_player.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

namespace rigexec {

void initialize_rigexec_module(godot::ModuleInitializationLevel level) {
    if (level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    godot::ClassDB::register_class<RigExecCharacter>();
    godot::ClassDB::register_class<RigExecPlayer>();
}

void uninitialize_rigexec_module(godot::ModuleInitializationLevel level) {
    (void)level;
}

} // namespace rigexec

extern "C" {
GDExtensionBool GDE_EXPORT
rigexec_library_init(GDExtensionInterfaceGetProcAddress get_proc_address,
                     GDExtensionClassLibraryPtr library,
                     GDExtensionInitialization *initialization) {
    const godot::GDExtensionBinding::InitObject init_object(
        get_proc_address, library, initialization);
    init_object.register_initializer(
        rigexec::initialize_rigexec_module);
    init_object.register_terminator(
        rigexec::uninitialize_rigexec_module);
    init_object.set_minimum_library_initialization_level(
        godot::MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_object.init();
}
}
