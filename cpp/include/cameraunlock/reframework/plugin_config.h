#pragma once

#include <cstdint>

namespace cameraunlock::reframework {

inline constexpr uint16_t kDefaultUdpPort = 4242;

inline constexpr int kDefaultToggleKey = 0x23;            // VK_END
inline constexpr int kDefaultPositionToggleKey = 0x21;    // VK_PRIOR (Page Up)
inline constexpr int kDefaultYawModeKey = 0x22;           // VK_NEXT (Page Down)
inline constexpr int kDefaultDiagnosticMarkerKey = 0x78;  // VK_F9

// Which optional INI keys a given game's config carries.
//
// PluginConfig declares every field for every game; the schema decides which
// ones Load() reads and Save() writes. That keeps each game's INI schema
// exactly what it was - a game that never had InvertX does not silently gain
// it, and Requiem, which deliberately fixed the axis conversion at the camera
// boundary, does not get the knob back.
struct PluginConfigSchema {
    // Written into the banner comment of a generated INI.
    const char* title = "Head Tracking";

    // [Position] InvertX / InvertY / InvertZ.
    bool positionInvertKeys = false;

    // [Flashlight] Enabled / Multiplier.
    bool flashlight = false;

    // [Hotkeys] DiagnosticMarkerKey.
    bool diagnosticMarkerKey = false;

    // Default for [Position] SensitivityX/Y/Z.
    float positionSensitivity = 1.0f;
};

struct PluginConfig {
    // Network
    uint16_t udpPort = kDefaultUdpPort;

    // Sensitivity
    float yawMultiplier = 1.0f;
    float pitchMultiplier = 1.0f;
    float rollMultiplier = 1.0f;

    // Smoothing. Selected per connection from the packet source address: a
    // tracker on this machine (loopback) uses localSmoothing, a remote network
    // device uses remoteSmoothing. Both cover rotation and position.
    float localSmoothing = 0.0f;
    float remoteSmoothing = 0.15f;

    // Hotkeys (Virtual Key codes)
    int toggleKey = kDefaultToggleKey;
    int positionToggleKey = kDefaultPositionToggleKey;
    int yawModeKey = kDefaultYawModeKey;
    int diagnosticMarkerKey = kDefaultDiagnosticMarkerKey;

    // Position (6DOF)
    float positionSensitivityX = 1.0f;
    float positionSensitivityY = 1.0f;
    float positionSensitivityZ = 1.0f;
    float positionLimitX = 0.30f;
    float positionLimitY = 0.20f;
    float positionLimitZ = 0.40f;
    float positionLimitZBack = 0.10f;
    bool positionInvertX = false;
    bool positionInvertY = false;
    bool positionInvertZ = false;
    bool positionEnabled = true;

    // Flashlight. The beam is rotated by the head pose scaled by
    // flashlightMultiplier, so it leads the view instead of matching it. This
    // is a game-specific light-to-view relationship, not tracker pose shaping.
    bool flashlightTracking = true;
    float flashlightMultiplier = 1.5f;

    // General
    bool autoEnable = true;
    bool worldSpaceYaw = true;

    bool Load(const char* path, const PluginConfigSchema& schema);
    bool Save(const char* path, const PluginConfigSchema& schema) const;
    void SetDefaults(const PluginConfigSchema& schema);
    void Validate(const PluginConfigSchema& schema);
};

} // namespace cameraunlock::reframework
