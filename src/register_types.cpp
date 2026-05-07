#include "register_types.hpp"

#include "OpenH264Loader.hpp"
#include "VideoStreamOpenH264.hpp"
#include "VideoStreamPlaybackOpenH264.hpp"

#include <gdextension_interface.h>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

static OpenH264Loader *_openh264_loader = nullptr;

void initialize_openh264_module(ModuleInitializationLevel p_level) {

    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    ClassDB::register_class<OpenH264Loader>();
    ClassDB::register_class<VideoStreamOpenH264>();
    ClassDB::register_class<VideoStreamPlaybackOpenH264>();

    _openh264_loader = memnew(OpenH264Loader);
    Engine::get_singleton()->register_singleton("OpenH264Loader", _openh264_loader);
}

void uninitialize_openh264_module(ModuleInitializationLevel p_level) {

    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    if (_openh264_loader) {
        Engine::get_singleton()->unregister_singleton("OpenH264Loader");
        memdelete(_openh264_loader);
        _openh264_loader = nullptr;
    }
}

extern "C" {

GDExtensionBool GDE_EXPORT godot_openh264_init(
        GDExtensionInterfaceGetProcAddress p_get_proc_address,
        GDExtensionClassLibraryPtr p_library,
        GDExtensionInitialization *r_initialization) {
    godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
    init_obj.register_initializer(initialize_openh264_module);
    init_obj.register_terminator(uninitialize_openh264_module);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}

} // extern "C"
