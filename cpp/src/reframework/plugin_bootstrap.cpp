#include <cameraunlock/reframework/plugin_bootstrap.h>

#include <cameraunlock/input/chord_hotkeys.h>
#include <cameraunlock/input/hotkey_poller.h>
#include <cameraunlock/reframework/game_window.h>
#include <cameraunlock/reframework/log_callback.h>

namespace cameraunlock::reframework {

static cameraunlock::input::HotkeyPoller g_hotkeyPoller;
static const PluginBootstrapDescriptor* g_descriptor = nullptr;

static void OnPreBeginRendering() {
    if (g_descriptor->centerGameWindow) CenterGameWindowOnce();
    CameraPipelinePreRender();
}

static void OnPostBeginRendering() {
    CameraPipelinePostRender();
}

static bool OnPreGuiDrawElement(void* element, void* context) {
    return g_descriptor->preGuiDrawElement(element, context);
}

bool InitializePlugin(const REFrameworkPluginInitializeParam* param,
                      const PluginBootstrapDescriptor& descriptor) {
    g_descriptor = &descriptor;

    ::reframework::API::initialize(param);

    InstallReframeworkLogSink(param->functions->log_info,
                              param->functions->log_warn,
                              param->functions->log_error,
                              descriptor.logTag);

    LogInfo("%s v%s - Plugin loaded", descriptor.mod.displayName, descriptor.mod.version);

    if (!PluginMod::Instance().Initialize(descriptor.mod)) {
        LogError("Mod initialization failed");
        return false;
    }

    InitCameraPipeline(descriptor.camera);

    param->functions->on_pre_application_entry("BeginRendering", OnPreBeginRendering);
    param->functions->on_post_application_entry("BeginRendering", OnPostBeginRendering);
    if (descriptor.preGuiDrawElement) {
        param->functions->on_pre_gui_draw_element(OnPreGuiDrawElement);
    }

    const PluginConfig& config = PluginMod::Instance().GetConfig();
    using cameraunlock::input::ChordGuarded;
    using cameraunlock::input::NavGuarded;

    // Nav-cluster bindings. Suppressed while Ctrl+Shift is held so the chord
    // path below is the sole trigger for Ctrl+Shift+<nav> combos.
    g_hotkeyPoller.SetToggleKey(config.toggleKey, NavGuarded([]() {
        PluginMod::Instance().Toggle();
    }));
    g_hotkeyPoller.AddHotkey(config.positionToggleKey, NavGuarded([]() {
        PluginMod::Instance().RequestCycleTrackingMode();
    }));
    g_hotkeyPoller.AddHotkey(config.yawModeKey, NavGuarded([]() {
        PluginMod::Instance().ToggleYawMode();
    }));

    // Ctrl+Shift+<letter> chord bindings (the shared T/Y/U/G/H/J cluster).
    g_hotkeyPoller.AddHotkey('Y', ChordGuarded([]() {
        PluginMod::Instance().Toggle();
    }));
    g_hotkeyPoller.AddHotkey('G', ChordGuarded([]() {
        PluginMod::Instance().RequestCycleTrackingMode();
    }));
    g_hotkeyPoller.AddHotkey('H', ChordGuarded([]() {
        PluginMod::Instance().ToggleYawMode();
    }));

    if (descriptor.registerExtraHotkeys) {
        descriptor.registerExtraHotkeys(g_hotkeyPoller, config);
    }

    g_hotkeyPoller.Start();

    LogInfo("Plugin initialization complete");
    return true;
}

} // namespace cameraunlock::reframework
