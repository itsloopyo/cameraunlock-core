#pragma once

#include <cameraunlock/reframework/camera_pipeline.h>
#include <cameraunlock/reframework/plugin_mod.h>

#include <reframework/API.hpp>

namespace cameraunlock::input {
class HotkeyPoller;
}

namespace cameraunlock::reframework {

struct PluginBootstrapDescriptor {
    // Prefix on every log line, e.g. "RE9HT".
    const char* logTag = "HT";

    PluginModDescriptor mod;
    CameraPipelineDescriptor camera;

    // on_pre_gui_draw_element callback. Return false to skip drawing the
    // element. Null leaves the callback unregistered.
    bool (*preGuiDrawElement)(void* element, void* context) = nullptr;

    // Move the game's window to the centre of its monitor's work area once.
    bool centerGameWindow = false;

    // Registered after the shared nav-cluster and Ctrl+Shift bindings, for
    // game-specific keys. Optional.
    void (*registerExtraHotkeys)(cameraunlock::input::HotkeyPoller& poller,
                                 const PluginConfig& config) = nullptr;
};

// Everything a REFramework head-tracking plugin does in
// reframework_plugin_initialize: bring up the SDK wrapper, route logging,
// initialize the tracking pipeline, install the camera pipeline, register the
// BeginRendering and GUI callbacks, and bind the hotkeys.
//
// The descriptor must outlive the process (its function pointers and strings
// are retained). Returns false when mod initialization fails, which is what
// the export should return.
bool InitializePlugin(const REFrameworkPluginInitializeParam* param,
                      const PluginBootstrapDescriptor& descriptor);

} // namespace cameraunlock::reframework
