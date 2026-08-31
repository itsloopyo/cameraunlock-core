#pragma once

#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/smoothing_utils.h"

#include <cstdint>

namespace cameraunlock::reframework {

inline constexpr uint16_t kDefaultUdpPort = 4242;

inline constexpr int kDefaultToggleKey = 0x23;            // VK_END
inline constexpr int kDefaultPositionToggleKey = 0x21;    // VK_PRIOR (Page Up)
inline constexpr int kDefaultYawModeKey = 0x22;           // VK_NEXT (Page Down)
inline constexpr int kDefaultDiagnosticMarkerKey = 0x78;  // VK_F9

// Accepted ranges for the tunable numbers, applied identically whether a value
// arrives from the INI or is set in code. Tighter than the library-wide guards
// in config/value_guards.h on purpose: those bound what cannot reach the camera
// matrix as garbage (a sensitivity of 100, a travel limit of 10 metres), these
// bound what is usable in an RE Engine title. The floor is 0 on every axis - a
// user pinning one axis is asking for exactly what a 0 multiplier does.
inline constexpr float kMinSensitivity = 0.0f;
inline constexpr float kMaxRotationSensitivity = 5.0f;
inline constexpr float kMaxPositionSensitivity = 10.0f;
inline constexpr float kMinPositionLimit = 0.0f;
inline constexpr float kMaxPositionLimit = 2.0f;
inline constexpr float kMaxFlashlightMultiplier = 5.0f;

// Format version of a written config. Bumped when a shipped default changes to
// a value an INI already on disk has to adopt, because Load only writes the
// file when it could not read one - without a version there is nothing to tell
// a stale shipped value apart from a value the user chose.
//
// Load migrates a config stamped below this and stamps it; a config already at
// this version is never rewritten, which is what makes an edit the user makes
// after the migration theirs to keep.
//
// 1: RE8 [Position] InvertX, which shipped true and mirrored the lateral lean.
inline constexpr int kPluginConfigVersion = 1;

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

    // Stable identity for the config migrations in Load(), separate from
    // `title` because that is display text and gets reworded. A migration that
    // has to correct one game's shipped value keys on this; an empty id matches
    // no migration. Keep it last - every mod's schema is initialised
    // positionally, so a field inserted above silently rebinds the rest.
    const char* modId = "";
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
    float localSmoothing = static_cast<float>(math::kDefaultLocalSmoothing);
    float remoteSmoothing = static_cast<float>(math::kDefaultRemoteSmoothing);

    // Hotkeys (Virtual Key codes)
    int toggleKey = kDefaultToggleKey;
    int positionToggleKey = kDefaultPositionToggleKey;
    int yawModeKey = kDefaultYawModeKey;
    int diagnosticMarkerKey = kDefaultDiagnosticMarkerKey;

    // Position (6DOF)
    float positionSensitivityX = 1.0f;
    float positionSensitivityY = 1.0f;
    float positionSensitivityZ = 1.0f;
    float positionLimitX = PositionSettings{}.limit_x;
    float positionLimitY = PositionSettings{}.limit_y;
    float positionLimitZ = PositionSettings{}.limit_z;
    float positionLimitZBack = PositionSettings{}.limit_z_back;
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

    // [General] ConfigVersion as it stood on disk. 0 is a file written before
    // the stamp existed, so nothing in it can be told apart from a shipped
    // default. Load raises it to kPluginConfigVersion once it has migrated the
    // file, and leaves it alone when the rewrite failed.
    int configVersion = 0;

    bool Load(const char* path, const PluginConfigSchema& schema);
    bool Save(const char* path, const PluginConfigSchema& schema) const;
    void SetDefaults(const PluginConfigSchema& schema);
    void Validate(const PluginConfigSchema& schema);
};

} // namespace cameraunlock::reframework
