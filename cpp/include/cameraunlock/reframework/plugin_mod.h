#pragma once

#include <cameraunlock/input/deferred_actions.h>
#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/reframework/plugin_config.h>
#include <cameraunlock/time/frame_clock.h>
#include <cameraunlock/tracking/head_tracking_session.h>

#include <atomic>
#include <string>

namespace cameraunlock::reframework {

// Per-game identity handed to PluginMod::Initialize.
struct PluginModDescriptor {
    // Log banner name, e.g. "RE9 Head Tracking".
    const char* displayName = "Head Tracking";
    const char* version = "0.0.0";
    // File name of the INI, resolved beside the plugin DLL.
    const char* configFileName = "HeadTracking.ini";
    PluginConfigSchema config;
};

// The tracking pipeline every RE Engine head-tracking plugin owns: config load,
// UDP receiver, HeadTrackingSession, the enable/mode/yaw hotkey actions, and
// the once-per-render-frame tick.
//
// One instance per process - a REFramework plugin DLL hosts exactly one mod.
class PluginMod {
public:
    static PluginMod& Instance();

    // Void, not bool. Nothing in here is fatal: a missing config file writes
    // defaults and carries on, and a busy UDP port is retried in the background
    // by the receiver's own supervisor thread. Returning a status the caller was
    // expected to act on gave the bootstrap an error branch that could not run.
    void Initialize(const PluginModDescriptor& descriptor);
    void Shutdown();

    bool IsEnabled() const { return m_enabled.load(); }
    void SetEnabled(bool enabled);
    void Toggle();

    void CycleTrackingMode();
    void ToggleYawMode();

    // Hotkey callbacks fire on the HotkeyPoller's background thread, but
    // CycleTrackingMode mutates the session's non-atomic
    // processor/interpolator smoothing state owned by the render thread. The
    // hotkey thread only requests the action; ProcessDeferredActions() runs it
    // on the render thread at the start of each frame.
    void RequestCycleTrackingMode() { m_cycleModeRequested.Request(); }
    void ProcessDeferredActions();

    PluginConfig& GetConfig() { return m_config; }
    const PluginConfig& GetConfig() const { return m_config; }

    // Advance interpolation + smoothing pipelines once per render frame.
    // Caches the smoothed rotation and position so every in-frame consumer
    // (camera matrix, crosshair projection, GUI marker compensation) reads
    // an identical value. Without this, per-element GUI calls would each
    // re-tick the pipeline with a fragmented dt, leaving the rendered
    // camera advancing on a partial-frame dt while position smoothing sees
    // an even smaller one.
    void TickFrame();

    // Latches the first tracker packet. Called from an ungated point in the
    // render callback: the answer to "did the tracker ever send anything"
    // must not depend on tracking being enabled or the camera hook engaging.
    void LogFirstTrackerPose();

    bool GetProcessedRotation(float& yaw, float& pitch, float& roll);
    bool GetPositionOffset(float& x, float& y, float& z);
    bool IsPositionEnabled() const { return m_session.IsPositionActive(); }
    bool IsRotationEnabled() const { return m_session.IsRotationActive(); }
    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw.load(std::memory_order_relaxed); }
    float GetLastDeltaTime() const { return m_lastDeltaTime; }

    PluginMod(const PluginMod&) = delete;
    PluginMod& operator=(const PluginMod&) = delete;

private:
    PluginMod() = default;
    ~PluginMod() = default;

    bool LoadConfig();

    PluginModDescriptor m_descriptor;

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_initialized{false};

    PluginConfig m_config;
    cameraunlock::UdpReceiver m_udpReceiver;
    cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver> m_session{m_udpReceiver};

    // Read on the render thread, toggled on the hotkey thread.
    std::atomic<bool> m_worldSpaceYaw{false};

    cameraunlock::input::DeferredAction m_cycleModeRequested;

    bool m_loggedFirstPose = false;

    cameraunlock::time::FrameClock m_frameClock;
    float m_lastDeltaTime = 0.016f;

    std::string m_configPath;
};

} // namespace cameraunlock::reframework
