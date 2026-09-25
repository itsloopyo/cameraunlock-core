#include <cameraunlock/reframework/plugin_config_table.h>

#include <cameraunlock/config/hotkey_codec.h>
#include <cameraunlock/input/key_bindings.h>

#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace cameraunlock::reframework {
namespace {

using config::DroppedValue;
using config::ImportResult;
using config::LegacyInput;
using config::LegacyKey;
using config::LegacyPoseShaping;
using config::PoseShapingValue;
using Concept = config::schema::Concept;

// The Ctrl+Shift letter plugin_bootstrap.cpp registers beside each legacy hotkey, or 0.
std::string LegacyBindings(int vk, int chordLetter) {
    std::vector<input::KeyBinding> bindings{{input::KeyModifiers::kNone, vk}};
    if (chordLetter != 0) bindings.push_back({input::KeyModifiers::kCtrl | input::KeyModifiers::kShift, chordLetter});
    return input::FormatKeyBindings(bindings);
}

std::vector<LegacyKey> ReadKeys(const PluginConfigSchema& schema) {
    std::vector<LegacyKey> keys = {
        {"Network", "UDPPort"},
        {"Sensitivity", "YawMultiplier"},
        {"Sensitivity", "PitchMultiplier"},
        {"Sensitivity", "RollMultiplier"},
        {"Smoothing", "LocalSmoothing"},
        {"Smoothing", "RemoteSmoothing"},
        {"Position", "Smoothing"},
        {"Hotkeys", "ToggleKey"},
        {"Hotkeys", "PositionToggleKey"},
        {"Hotkeys", "YawModeKey"},
    };
    if (schema.diagnosticMarkerKey) keys.push_back({"Hotkeys", "DiagnosticMarkerKey"});
    for (const char* key : {"SensitivityX", "SensitivityY", "SensitivityZ", "LimitX", "LimitY", "LimitZ", "LimitZBack"}) {
        keys.push_back({"Position", key});
    }
    if (schema.positionInvertKeys) {
        for (const char* key : {"InvertX", "InvertY", "InvertZ"}) keys.push_back({"Position", key});
    }
    keys.push_back({"Position", "Enabled"});
    if (schema.flashlight) {
        keys.push_back({"Flashlight", "Enabled"});
        keys.push_back({"Flashlight", "Multiplier"});
    }
    keys.push_back({"General", "AutoEnable"});
    keys.push_back({"General", "WorldSpaceYaw"});
    keys.push_back({"General", "ConfigVersion"});
    return keys;
}

}  // namespace

config::ConfigTable<PluginConfig> PluginConfigTable(const PluginConfigSchema& schema) {
    PluginConfig defaults;
    defaults.SetDefaults(schema);
    config::ConfigTable<PluginConfig> table(std::move(defaults));

    table.Concept<Concept::UdpPort>(&PluginConfig::udpPort);
    table.Concept<Concept::EnableOnStartup>(&PluginConfig::autoEnable);
    table.Concept<Concept::WorldSpaceYaw>(&PluginConfig::worldSpaceYaw).Writable();
    table.Concept<Concept::LocalSmoothing>(&PluginConfig::localSmoothing);
    table.Concept<Concept::RemoteSmoothing>(&PluginConfig::remoteSmoothing);
    table.Concept<Concept::PositionEnabled>(&PluginConfig::positionEnabled)
        .Writable()
        .Comment("true: moving your head moves the view.\n"
                 "Tracking mode at startup. The mode hotkey turns it on and off and saves it here.");
    table.Concept<Concept::PositionLimitX>(&PluginConfig::positionLimitX);
    table.Concept<Concept::PositionLimitY>(&PluginConfig::positionLimitY)
        .Comment("How far, in metres, raising or lowering your head can move the view.");
    table.Concept<Concept::PositionLimitZ>(&PluginConfig::positionLimitZ);
    table.Concept<Concept::PositionLimitZBack>(&PluginConfig::positionLimitZBack);
    table.Concept<Concept::ToggleKey>(&PluginConfig::toggleKeyBindings);
    table.Concept<Concept::CycleTrackingModeKey>(&PluginConfig::cycleTrackingModeKeyBindings)
        .Comment("Changes the tracking mode: rotation and position, or rotation only.");
    table.Concept<Concept::YawModeKey>(&PluginConfig::yawModeKeyBindings);
    if (schema.diagnosticMarkerKey) {
        table.Local("Hotkeys", "DiagnosticMarkerKey", &PluginConfig::diagnosticMarkerKeyBindings, config::HotkeyCodec{},
                    "Hides and shows the game's world-anchored markers.");
    }
    if (schema.flashlight) {
        table.Concept<Concept::LightFollowsHead>(&PluginConfig::flashlightTracking);
        table.Concept<Concept::LightMultiplier>(&PluginConfig::flashlightMultiplier);
    }
    return table;
}

config::LegacyImport<PluginConfig> PluginConfigLegacyImport(const PluginConfigSchema& schema) {
    config::LegacyImport<PluginConfig> import;
    import.keys = ReadKeys(schema);
    import.run = [schema](const LegacyInput& input, PluginConfig& out) {
        PluginConfig legacy;
        const bool found = legacy.Read(input.ansi_path.c_str(), schema);
        // The in-memory half of Load's RE8 migration, which Load applies even when it cannot
        // write the stamp.
        if (std::strcmp(schema.modId, "re8") == 0 && legacy.configVersion < kPluginConfigVersion &&
            schema.positionInvertKeys && legacy.positionInvertX) {
            legacy.positionInvertX = false;
        }

        out.udpPort = legacy.udpPort;
        out.autoEnable = legacy.autoEnable;
        out.worldSpaceYaw = legacy.worldSpaceYaw;
        out.localSmoothing = legacy.localSmoothing;
        out.remoteSmoothing = legacy.remoteSmoothing;
        out.positionEnabled = legacy.positionEnabled;
        out.positionLimitX = legacy.positionLimitX;
        out.positionLimitY = legacy.positionLimitY;
        out.positionLimitZ = legacy.positionLimitZ;
        out.positionLimitZBack = legacy.positionLimitZBack;
        out.flashlightTracking = legacy.flashlightTracking;
        out.flashlightMultiplier = legacy.flashlightMultiplier;
        out.toggleKeyBindings = LegacyBindings(legacy.toggleKey, 'Y');
        out.cycleTrackingModeKeyBindings = LegacyBindings(legacy.positionToggleKey, 'G');
        out.yawModeKeyBindings = LegacyBindings(legacy.yawModeKey, 'H');
        out.diagnosticMarkerKeyBindings = LegacyBindings(legacy.diagnosticMarkerKey, 0);

        PluginConfig shipped;
        shipped.SetDefaults(schema);
        std::vector<DroppedValue> dropped;
        std::vector<PoseShapingValue> pose;
        LegacyPoseShaping(legacy.yawMultiplier, shipped.yawMultiplier, "Sensitivity", "YawMultiplier", pose, dropped);
        LegacyPoseShaping(legacy.pitchMultiplier, shipped.pitchMultiplier, "Sensitivity", "PitchMultiplier", pose, dropped);
        LegacyPoseShaping(legacy.rollMultiplier, shipped.rollMultiplier, "Sensitivity", "RollMultiplier", pose, dropped);
        LegacyPoseShaping(legacy.positionSensitivityX, shipped.positionSensitivityX, "Position", "SensitivityX", pose,
                          dropped);
        LegacyPoseShaping(legacy.positionSensitivityY, shipped.positionSensitivityY, "Position", "SensitivityY", pose,
                          dropped);
        LegacyPoseShaping(legacy.positionSensitivityZ, shipped.positionSensitivityZ, "Position", "SensitivityZ", pose,
                          dropped);
        if (schema.positionInvertKeys) {
            LegacyPoseShaping(legacy.positionInvertX, shipped.positionInvertX, "Position", "InvertX", pose, dropped);
            LegacyPoseShaping(legacy.positionInvertY, shipped.positionInvertY, "Position", "InvertY", pose, dropped);
            LegacyPoseShaping(legacy.positionInvertZ, shipped.positionInvertZ, "Position", "InvertZ", pose, dropped);
        }

        return found ? ImportResult::Imported(std::move(dropped), std::move(pose))
                     : ImportResult::Absent(std::move(dropped), std::move(pose));
    };
    return import;
}

}  // namespace cameraunlock::reframework
