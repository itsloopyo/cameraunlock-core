#include <cameraunlock/reframework/plugin_mod.h>

#include <cameraunlock/os/module_paths.h>
#include <cameraunlock/reframework/log_callback.h>

namespace cameraunlock::reframework {

using cameraunlock::TrackingMode;

// The session re-reads the receiver's connection locality every Update() and
// selects LocalSmoothing or RemoteSmoothing from it, but that wiring is
// SFINAE-gated on the receiver exposing IsRemoteConnection(). A receiver
// adapter that failed to forward the method would still compile and would
// silently pin every connection to LocalSmoothing forever.
static_assert(cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver>::kHasRemoteConnection,
              "receiver must expose IsRemoteConnection() or remote smoothing never applies");

PluginMod& PluginMod::Instance() {
    static PluginMod instance;
    return instance;
}

bool PluginMod::Initialize(const PluginModDescriptor& descriptor) {
    if (m_initialized.load()) {
        LogWarning("Mod already initialized");
        return true;
    }

    m_descriptor = descriptor;

    LogInfo("%s v%s initializing...", m_descriptor.displayName, m_descriptor.version);

    if (!LoadConfig()) {
        LogWarning("Using default configuration");
    }

    cameraunlock::SensitivitySettings sensitivity;
    sensitivity.yaw = m_config.yawMultiplier;
    sensitivity.pitch = m_config.pitchMultiplier;
    sensitivity.roll = m_config.rollMultiplier;
    m_session.GetProcessor().SetSensitivity(sensitivity);

    LogInfo("Sensitivity: yaw=%.2f pitch=%.2f roll=%.2f",
            sensitivity.yaw, sensitivity.pitch, sensitivity.roll);

    LogInfo("Smoothing: local=%.2f remote=%.2f",
            m_config.localSmoothing, m_config.remoteSmoothing);

    m_session.SetMode(m_config.positionEnabled ? TrackingMode::RotationAndPosition
                                               : TrackingMode::RotationOnly);
    m_worldSpaceYaw.store(m_config.worldSpaceYaw, std::memory_order_relaxed);

    // Assigned by name rather than through the positional constructor.
    // PositionSettings takes nine floats before its three inversion bools, so a
    // positional call that gains or loses one argument silently rebinds a bool
    // to a float parameter - an invert flag would land in a smoothing slot - and
    // still compiles clean. Naming every field removes that failure mode.
    cameraunlock::PositionSettings posSettings;
    posSettings.sensitivity_x = m_config.positionSensitivityX;
    posSettings.sensitivity_y = m_config.positionSensitivityY;
    posSettings.sensitivity_z = m_config.positionSensitivityZ;
    posSettings.limit_x = m_config.positionLimitX;
    // The clamp is [-limit_y_down, +limit_y] and limit_y_down carries its own
    // default, so mirror the one configured vertical limit the way
    // PositionSettings::Symmetric does. Left unset, raising LimitY widened the
    // upward budget only and downward travel stayed pinned at 0.20m.
    posSettings.limit_y = m_config.positionLimitY;
    posSettings.limit_y_down = m_config.positionLimitY;
    // Asymmetric Z: negative z is the forward lean, so the generous limit_z is
    // the forward range and limit_z_back restricts leaning back into the player.
    posSettings.limit_z = m_config.positionLimitZ;
    posSettings.limit_z_back = m_config.positionLimitZBack;
    // Protocol-to-engine axis conversion. ApplyViewSpacePositionOffset takes
    // offsetX and applies -offsetX, so leaving these false makes that single
    // negation the whole conversion. Setting invert_x true cancels it and
    // mirrors the lateral lean, which reads as working until something anchored
    // in the world - a reticle, a marker - has to agree with it. Requiem's
    // schema drops the INI keys entirely and so always lands here with false.
    posSettings.invert_x = m_config.positionInvertX;
    posSettings.invert_y = m_config.positionInvertY;
    posSettings.invert_z = m_config.positionInvertZ;

    // Smoothing first, then the settings. The session owns the smoothing pair
    // for both rotation and position, and SetPositionSettings stamps the owned
    // pair over whatever the struct carries - so the struct deliberately leaves
    // local_smoothing / remote_smoothing at their defaults and the two can
    // never drift apart.
    m_session.SetLocalSmoothing(m_config.localSmoothing);
    m_session.SetRemoteSmoothing(m_config.remoteSmoothing);
    m_session.SetPositionSettings(posSettings);

    // The previous per-mod pipeline never engaged tracker pivot compensation
    // (it passed radians to a degrees API, zeroing the artifact). Keep that
    // tuning until pivot compensation is verified in game.
    m_session.GetPositionProcessor().SetTrackerPivotForward(0.0f);

    LogInfo("Position: %s, sens=%.1f/%.1f/%.1f",
            IsPositionEnabled() ? "6DOF" : "3DOF",
            posSettings.sensitivity_x, posSettings.sensitivity_y, posSettings.sensitivity_z);

    // Forwarded so the core receiver's bind result, retry progress and latched
    // first-packet line reach the log. Set before Start so the bind is captured.
    m_udpReceiver.SetLog([](const std::string& msg) {
        LogInfo("UDP: %s", msg.c_str());
    });

    // A busy port is not fatal: the core receiver's supervisor thread keeps
    // retrying, so tracking comes up on its own once the port frees. Returning
    // false here aborted plugin init before the render callback was registered,
    // so the camera never ran and nothing could report the recovery.
    if (m_udpReceiver.Start(m_config.udpPort)) {
        LogInfo("UDP receiver started on port %d", m_config.udpPort);
    } else {
        LogWarning("UDP port %d busy - retrying in background", m_config.udpPort);
    }

    if (m_config.autoEnable) {
        m_enabled.store(true);
        LogInfo("Head tracking auto-enabled");
    }

    m_initialized.store(true);
    LogInfo("Initialization complete");
    return true;
}

void PluginMod::Shutdown() {
    if (!m_initialized.load()) return;

    LogInfo("Shutting down...");
    m_udpReceiver.Stop();
    m_initialized.store(false);
    LogInfo("Shutdown complete");
}

bool PluginMod::LoadConfig() {
    // Beside the plugin DLL. SelfModuleDirectory resolves the module from an
    // address inside this static library, which links into the plugin, so this
    // is the plugin's own directory rather than the game EXE's.
    std::string directory = cameraunlock::os::SelfModuleDirectoryNarrow();
    if (directory.empty()) {
        LogError("Could not resolve the plugin directory - config not loaded or written");
        return false;
    }
    m_configPath = directory + "\\" + m_descriptor.configFileName;

    if (!m_config.Load(m_configPath.c_str(), m_descriptor.config)) {
        m_config.SetDefaults(m_descriptor.config);
        m_config.Save(m_configPath.c_str(), m_descriptor.config);
        LogWarning("Config not found at %s - defaults written there", m_configPath.c_str());
        return false;
    }
    return true;
}

void PluginMod::SetEnabled(bool enabled) {
    bool wasEnabled = m_enabled.exchange(enabled);
    if (wasEnabled != enabled) {
        LogInfo("Head tracking %s", enabled ? "enabled" : "disabled");
    }
}

void PluginMod::Toggle() {
    SetEnabled(!m_enabled.load());
}

void PluginMod::CycleTrackingMode() {
    switch (m_session.CycleMode()) {
        case TrackingMode::RotationAndPosition:
            LogInfo("Tracking mode: full (rotation + position)");
            break;
        case TrackingMode::RotationOnly:
            LogInfo("Tracking mode: rotation only (position disabled)");
            break;
        case TrackingMode::PositionOnly:
            LogInfo("Tracking mode: position only (rotation disabled)");
            break;
    }
}

void PluginMod::ProcessDeferredActions() {
    if (!m_initialized.load()) return;
    if (m_cycleModeRequested.Consume()) CycleTrackingMode();
}

void PluginMod::TickFrame() {
    if (!m_initialized.load()) return;

    m_lastDeltaTime = m_frameClock.Tick();
    m_session.Update(m_lastDeltaTime);
}

void PluginMod::LogFirstTrackerPose() {
    if (!m_initialized.load()) return;
    if (m_loggedFirstPose) return;

    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    if (!m_udpReceiver.GetRotation(yaw, pitch, roll)) return;

    m_loggedFirstPose = true;
    LogInfo("First tracker pose received: yaw=%.2f pitch=%.2f roll=%.2f (%s connection)",
            yaw, pitch, roll,
            m_udpReceiver.IsRemoteConnection() ? "remote" : "local");
}

bool PluginMod::GetProcessedRotation(float& yaw, float& pitch, float& roll) {
    return m_session.GetRotation(yaw, pitch, roll);
}

bool PluginMod::GetPositionOffset(float& x, float& y, float& z) {
    return m_session.GetPositionOffset(x, y, z);
}

void PluginMod::ToggleYawMode() {
    bool now = !m_worldSpaceYaw.load(std::memory_order_relaxed);
    m_worldSpaceYaw.store(now, std::memory_order_relaxed);
    LogInfo("Yaw mode: %s", now ? "world-space (horizon-locked)" : "camera-local");
}

} // namespace cameraunlock::reframework
