#pragma once

#include <cameraunlock/config/defaults_file.h>
#include <cameraunlock/input/deferred_actions.h>
#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/reframework/plugin_config.h>
#include <cameraunlock/time/frame_clock.h>
#include <cameraunlock/tracking/head_tracking_session.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace cameraunlock::config {
template <class Config>
class ConfigOwner;
}

namespace cameraunlock::reframework {

// Per-game identity handed to PluginMod::Initialize.
struct PluginModDescriptor {
    // Log banner name, e.g. "RE9 Head Tracking".
    const char* displayName = "Head Tracking";
    const char* version = "0.0.0";
    // File name of the INI, resolved beside the plugin DLL. With config.canonicalConfig it
    // names the legacy file instead: settings live in CameraUnlock.ini beside the plugin DLL,
    // and this file is imported into it once, while CameraUnlock.ini is absent. It is never
    // written.
    const char* configFileName = "HeadTracking.ini";
    PluginConfigSchema config;
    // The game's name as data/games.json spells it, in printable ASCII, written at the
    // top of a canonical config. Required with config.canonicalConfig.
    const char* gameName = nullptr;
    // Where Defaults.ini is, which the canonical config's concept rows take their defaults from:
    // config::DefaultsFile::PerUser() in a mod, config::DefaultsFile::At with a scratch path in a
    // test. Required with config.canonicalConfig. Members are only ever appended, and this one
    // is last.
    config::DefaultsFile defaults;
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
    //
    // With canonicalConfig the request also decides the mode: the one after the
    // mode the render thread last applied, so two presses before a frame still
    // move one step. It saves [Position] PositionEnabled for that mode, on the
    // calling thread, and the frame applies it.
    void RequestCycleTrackingMode();
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
    PluginMod();
    ~PluginMod();

    bool LoadConfig();
    bool LoadCanonicalConfig();
    void ApplyTrackingMode(cameraunlock::TrackingMode mode);
    void SaveConfig(const char* row, const std::function<void(PluginConfig&)>& change);

    PluginModDescriptor m_descriptor;

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_initialized{false};

    PluginConfig m_config;
    cameraunlock::UdpReceiver m_udpReceiver;
    cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver> m_session{m_udpReceiver};

    // Read on the render thread, toggled on the hotkey thread.
    std::atomic<bool> m_worldSpaceYaw{false};

    cameraunlock::input::DeferredAction m_cycleModeRequested;
    // Written on the render thread by ApplyTrackingMode. In canonical mode the
    // hotkey thread reads the applied mode to compute the desired one.
    std::atomic<cameraunlock::TrackingMode> m_appliedMode{cameraunlock::TrackingMode::RotationAndPosition};
    std::atomic<cameraunlock::TrackingMode> m_desiredMode{cameraunlock::TrackingMode::RotationAndPosition};

    // Null unless canonicalConfig, or when the config path could not be resolved.
    std::unique_ptr<cameraunlock::config::ConfigOwner<PluginConfig>> m_configOwner;

    bool m_loggedFirstPose = false;

    cameraunlock::time::FrameClock m_frameClock;
    float m_lastDeltaTime = 0.016f;

    std::string m_configPath;
};

} // namespace cameraunlock::reframework
