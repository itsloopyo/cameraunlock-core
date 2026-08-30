#include <cameraunlock/reframework/plugin_config.h>

#include <cameraunlock/config/ini_reader.h>
#include <cameraunlock/math/finite_utils.h>
#include <cameraunlock/protocol/port_utils.h>
#include <cameraunlock/reframework/log_callback.h>

#include <fstream>
#include <iomanip>

namespace cameraunlock::reframework {

void PluginConfig::SetDefaults(const PluginConfigSchema& schema) {
    *this = PluginConfig{};
    positionSensitivityX = schema.positionSensitivity;
    positionSensitivityY = schema.positionSensitivity;
    positionSensitivityZ = schema.positionSensitivity;
}

void PluginConfig::Validate(const PluginConfigSchema& schema) {
    using cameraunlock::math::SanitizeFinite;
    PluginConfig defaults;
    defaults.SetDefaults(schema);

    yawMultiplier = SanitizeFinite(yawMultiplier, defaults.yawMultiplier, 0.1f, 5.0f);
    pitchMultiplier = SanitizeFinite(pitchMultiplier, defaults.pitchMultiplier, 0.1f, 5.0f);
    rollMultiplier = SanitizeFinite(rollMultiplier, defaults.rollMultiplier, 0.0f, 2.0f);

    // Validation only: NaN/Inf falls back to the default, finite values clamp to
    // [0,1]. There is no minimum floor - 0.0 means the user asked for zero
    // smoothing and gets it.
    localSmoothing = SanitizeFinite(localSmoothing, defaults.localSmoothing, 0.0f, 1.0f);
    remoteSmoothing = SanitizeFinite(remoteSmoothing, defaults.remoteSmoothing, 0.0f, 1.0f);

    positionSensitivityX = SanitizeFinite(positionSensitivityX, defaults.positionSensitivityX, 0.1f, 10.0f);
    positionSensitivityY = SanitizeFinite(positionSensitivityY, defaults.positionSensitivityY, 0.1f, 10.0f);
    positionSensitivityZ = SanitizeFinite(positionSensitivityZ, defaults.positionSensitivityZ, 0.1f, 10.0f);

    positionLimitX = SanitizeFinite(positionLimitX, defaults.positionLimitX, 0.01f, 2.0f);
    positionLimitY = SanitizeFinite(positionLimitY, defaults.positionLimitY, 0.01f, 2.0f);
    positionLimitZ = SanitizeFinite(positionLimitZ, defaults.positionLimitZ, 0.01f, 2.0f);
    positionLimitZBack = SanitizeFinite(positionLimitZBack, defaults.positionLimitZBack, 0.01f, 2.0f);

    if (schema.flashlight) {
        flashlightMultiplier = SanitizeFinite(flashlightMultiplier, defaults.flashlightMultiplier, 0.0f, 5.0f);
    }
}

// Warned once per process rather than once per load: config is reloadable, and
// repeating this on every reload buries it.
//
// The old value is deliberately NOT migrated into the new keys. The single
// Smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
static void WarnRetiredSmoothingKey(const cameraunlock::IniReader& reader,
                                    const char* section, const char* key) {
    static bool warned = false;
    if (warned) return;
    if (reader.ReadString(section, key, "").empty()) return;
    warned = true;
    LogWarning(
        "Config key [%s] %s has been retired and is IGNORED. Smoothing is now two "
        "keys: LocalSmoothing (default 0, applies to a tracker on this machine) and "
        "RemoteSmoothing (default 0.15, applies to a tracker on the network). The "
        "old value is not migrated because the semantics changed - it carried a "
        "hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

bool PluginConfig::Load(const char* path, const PluginConfigSchema& schema) {
    SetDefaults(schema);

    cameraunlock::IniReader reader;
    if (!reader.Open(path)) {
        LogWarning("Could not load config from %s, using defaults", path);
        return false;
    }

    int rawPort = reader.ReadInt("Network", "UDPPort", udpPort);
    bool portValid = false;
    udpPort = cameraunlock::NormalizeUdpPort(rawPort, kDefaultUdpPort, portValid);
    if (!portValid) {
        LogWarning("UDP port %d is out of range (1024-65535), using default %d",
                   rawPort, kDefaultUdpPort);
    }

    yawMultiplier = reader.ReadFloat("Sensitivity", "YawMultiplier", yawMultiplier);
    pitchMultiplier = reader.ReadFloat("Sensitivity", "PitchMultiplier", pitchMultiplier);
    rollMultiplier = reader.ReadFloat("Sensitivity", "RollMultiplier", rollMultiplier);

    localSmoothing = reader.ReadFloat("Smoothing", "LocalSmoothing", localSmoothing);
    remoteSmoothing = reader.ReadFloat("Smoothing", "RemoteSmoothing", remoteSmoothing);

    WarnRetiredSmoothingKey(reader, "Position", "Smoothing");

    toggleKey = reader.ReadHex("Hotkeys", "ToggleKey", toggleKey);
    positionToggleKey = reader.ReadHex("Hotkeys", "PositionToggleKey", positionToggleKey);
    yawModeKey = reader.ReadHex("Hotkeys", "YawModeKey", yawModeKey);
    if (schema.diagnosticMarkerKey) {
        diagnosticMarkerKey = reader.ReadHex("Hotkeys", "DiagnosticMarkerKey", diagnosticMarkerKey);
    }

    positionSensitivityX = reader.ReadFloat("Position", "SensitivityX", positionSensitivityX);
    positionSensitivityY = reader.ReadFloat("Position", "SensitivityY", positionSensitivityY);
    positionSensitivityZ = reader.ReadFloat("Position", "SensitivityZ", positionSensitivityZ);
    positionLimitX = reader.ReadFloat("Position", "LimitX", positionLimitX);
    positionLimitY = reader.ReadFloat("Position", "LimitY", positionLimitY);
    positionLimitZ = reader.ReadFloat("Position", "LimitZ", positionLimitZ);
    positionLimitZBack = reader.ReadFloat("Position", "LimitZBack", positionLimitZBack);
    if (schema.positionInvertKeys) {
        positionInvertX = reader.ReadBool("Position", "InvertX", positionInvertX);
        positionInvertY = reader.ReadBool("Position", "InvertY", positionInvertY);
        positionInvertZ = reader.ReadBool("Position", "InvertZ", positionInvertZ);
    }
    positionEnabled = reader.ReadBool("Position", "Enabled", positionEnabled);

    if (schema.flashlight) {
        flashlightTracking = reader.ReadBool("Flashlight", "Enabled", flashlightTracking);
        flashlightMultiplier = reader.ReadFloat("Flashlight", "Multiplier", flashlightMultiplier);
    }

    autoEnable = reader.ReadBool("General", "AutoEnable", autoEnable);
    worldSpaceYaw = reader.ReadBool("General", "WorldSpaceYaw", worldSpaceYaw);

    Validate(schema);
    LogInfo("Config loaded from %s", path);
    return true;
}

bool PluginConfig::Save(const char* path, const PluginConfigSchema& schema) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        LogError("Failed to save config to %s", path);
        return false;
    }

    file << "; " << schema.title << " Configuration\n";
    file << "; Delete this file to reset to defaults\n\n";

    file << "[Network]\n";
    file << "; UDP port for OpenTrack data (default: 4242)\n";
    file << "UDPPort=" << udpPort << "\n\n";

    file << "[Sensitivity]\n";
    file << "; Rotation sensitivity multipliers (1.0 = 1:1)\n";
    file << "YawMultiplier=" << yawMultiplier << "\n";
    file << "PitchMultiplier=" << pitchMultiplier << "\n";
    file << "RollMultiplier=" << rollMultiplier << "\n\n";

    file << "[Smoothing]\n";
    file << "; Smoothing applied when the tracker runs on this machine (loopback).\n";
    file << "; 0 = no smoothing, 1 = heavy. Covers rotation and position.\n";
    file << "LocalSmoothing=" << localSmoothing << "\n";
    file << "; Smoothing applied when the tracker is a remote device on the network.\n";
    file << "; 0 = no smoothing, 1 = heavy. Covers rotation and position.\n";
    file << "RemoteSmoothing=" << remoteSmoothing << "\n\n";

    file << "[Position]\n";
    file << "; Position tracking sensitivity (0.1-10.0, higher = more movement)\n";
    file << "SensitivityX=" << positionSensitivityX << "\n";
    file << "SensitivityY=" << positionSensitivityY << "\n";
    file << "SensitivityZ=" << positionSensitivityZ << "\n";
    file << "; Position limits in meters\n";
    file << "LimitX=" << positionLimitX << "\n";
    file << "LimitY=" << positionLimitY << "\n";
    file << "LimitZ=" << positionLimitZ << "\n";
    file << "LimitZBack=" << positionLimitZBack << "\n";
    if (schema.positionInvertKeys) {
        file << "InvertX=" << (positionInvertX ? "true" : "false") << "\n";
        file << "InvertY=" << (positionInvertY ? "true" : "false") << "\n";
        file << "InvertZ=" << (positionInvertZ ? "true" : "false") << "\n";
    }
    file << "Enabled=" << (positionEnabled ? "true" : "false") << "\n\n";

    if (schema.flashlight) {
        file << "[Flashlight]\n";
        file << "; Head tracking moves the flashlight beam as well as the view.\n";
        file << "Enabled=" << (flashlightTracking ? "true" : "false") << "\n";
        file << "; How far the beam leads the view (1.0 = matches the head, 1.5 = default)\n";
        file << "Multiplier=" << flashlightMultiplier << "\n\n";
    }

    file << "[Hotkeys]\n";
    file << "; Virtual key codes (hex)\n";
    file << "ToggleKey=0x" << std::hex << toggleKey << "    ; End\n";
    file << "PositionToggleKey=0x" << positionToggleKey << " ; Page Up\n";
    file << "YawModeKey=0x" << yawModeKey << "      ; Page Down - toggle world/local yaw\n";
    if (schema.diagnosticMarkerKey) {
        file << "DiagnosticMarkerKey=0x" << diagnosticMarkerKey << " ; F9 - hide world-anchored markers\n";
    }
    file << std::dec << "\n";

    file << "[General]\n";
    file << "AutoEnable=" << (autoEnable ? "true" : "false") << "\n";
    file << "; Yaw mode: false = camera-local, true = horizon-locked (default)\n";
    file << "WorldSpaceYaw=" << (worldSpaceYaw ? "true" : "false") << "\n";

    file.close();
    LogInfo("Config saved to %s", path);
    return true;
}

} // namespace cameraunlock::reframework
