#include <cameraunlock/reframework/plugin_bootstrap.h>

#include <cameraunlock/input/chord_hotkeys.h>
#include <cameraunlock/input/hotkey_poller.h>
#include <cameraunlock/input/key_binding_registration.h>
#include <cameraunlock/input/key_bindings.h>
#include <cameraunlock/reframework/game_window.h>
#include <cameraunlock/reframework/log_callback.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace cameraunlock::reframework {

static const PluginBootstrapDescriptor* g_descriptor = nullptr;

// Constructed on first use from inside InitializePlugin, AFTER
// PluginMod::Instance() has been built, so it is destroyed BEFORE the mod its
// callbacks reach into. At namespace scope it was constructed first and torn
// down last, which left the polling thread alive and calling
// PluginMod::Instance() on an object whose destructor had already run.
static cameraunlock::input::HotkeyPoller& HotkeyPoller() {
    static cameraunlock::input::HotkeyPoller poller;
    return poller;
}

// The config owner read these through the table's hotkey codec, so every one parses.
static std::vector<cameraunlock::input::KeyBinding> CanonicalBindings(const std::string& text) {
    cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(text);
    if (!parsed.ok()) throw std::logic_error("hotkey list '" + text + "' does not parse: " + parsed.error);
    return parsed.bindings;
}

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

    PluginMod::Instance().Initialize(descriptor.mod);

    InitCameraPipeline(descriptor.camera);

    param->functions->on_pre_application_entry("BeginRendering", OnPreBeginRendering);
    param->functions->on_post_application_entry("BeginRendering", OnPostBeginRendering);
    if (descriptor.preGuiDrawElement) {
        param->functions->on_pre_gui_draw_element(OnPreGuiDrawElement);
    }

    const PluginConfig& config = PluginMod::Instance().GetConfig();
    auto& g_hotkeyPoller = HotkeyPoller();
    using cameraunlock::input::ChordGuarded;
    using cameraunlock::input::NavGuarded;

    if (descriptor.mod.config.canonicalConfig) {
        // The chords are items of these lists, so there is no second binding path.
        using cameraunlock::input::RegisterKeyBindings;
        RegisterKeyBindings(g_hotkeyPoller, CanonicalBindings(config.toggleKeyBindings), []() {
            PluginMod::Instance().Toggle();
        });
        RegisterKeyBindings(g_hotkeyPoller, CanonicalBindings(config.cycleTrackingModeKeyBindings), []() {
            PluginMod::Instance().RequestCycleTrackingMode();
        });
        RegisterKeyBindings(g_hotkeyPoller, CanonicalBindings(config.yawModeKeyBindings), []() {
            PluginMod::Instance().ToggleYawMode();
        });
    } else {
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
    }

    if (descriptor.registerExtraHotkeys) {
        descriptor.registerExtraHotkeys(g_hotkeyPoller, config);
    }

    g_hotkeyPoller.Start();

    LogInfo("Plugin initialization complete");
    return true;
}

} // namespace cameraunlock::reframework
