#pragma once

#include <cameraunlock/config/config_table.h>
#include <cameraunlock/config/legacy_import.h>
#include <cameraunlock/reframework/plugin_config.h>

namespace cameraunlock::reframework {

/// The canonical config file of a REFramework mod, over PluginConfig. Every row's default is
/// PluginConfig after SetDefaults(schema). Those are also what the frozen PluginConfig::Read gives
/// a key an old file lacks, so a default that moves for new files is set over them in this
/// function, never in SetDefaults or PluginConfig's initialisers. The rows:
///
/// - [Network] UdpPort; [General] EnableOnStartup and WorldSpaceYaw (Writable);
/// - [Smoothing] LocalSmoothing and RemoteSmoothing;
/// - [Position] PositionEnabled (Writable), PositionLimitX, PositionLimitY, PositionLimitZ and
///   PositionLimitZBack;
/// - [Hotkeys] ToggleKey, CycleTrackingModeKey and YawModeKey on the three *KeyBindings
///   fields, and with schema.diagnosticMarkerKey the local row DiagnosticMarkerKey;
/// - with schema.flashlight, [Light] LightFollowsHead and LightMultiplier.
///
/// The mode cycle is two-state, so there is no RotationEnabled, and PluginMod sets the
/// downward limit from PositionLimitY, so there is no PositionLimitYDown. Sensitivities and
/// inversions have no row: the tracker shapes the pose, and those fields keep their SetDefaults
/// values, schema.positionSensitivity included. There is no ConfigVersion: the canonical file's
/// format is [CameraUnlock] ConfigFormat.
config::ConfigTable<PluginConfig> PluginConfigTable(const PluginConfigSchema& schema);

/// The legacy import of a REFramework mod, for PluginConfigTable's owner. It runs
/// PluginConfig::Read on the ANSI path, so a path the ANSI code page cannot hold finds no file,
/// as the legacy build found none there. Then it applies the in-memory half of Load's RE8
/// migration (modId "re8", ConfigVersion below 1 and schema.positionInvertKeys: InvertX becomes
/// false), and maps into the table's fields: each hotkey code becomes a list holding that key and
/// the Ctrl+Shift chord the legacy bootstrap registers beside it (Y for ToggleKey, G for
/// PositionToggleKey, H for YawModeKey; DiagnosticMarkerKey has none). The three multipliers, the
/// three position sensitivities and, where schema.positionInvertKeys lets Read read them, the three
/// position inversions go through config::LegacyPoseShaping against their SetDefaults values, so
/// the result lists those nine, or six, in pose_shaping, and one that differs from SetDefaults is
/// also dropped (PoseShaping). SetDefaults
/// is where an RE mod keeps its shipped shaping, schema.positionSensitivity included, so a folded
/// value is one PluginMod still applies. No other field of `out` is set. Imported, or Absent when Read finds no file; it never refuses,
/// and writes nothing. `keys` lists every key Read reads for this schema, [Position] Smoothing
/// (read only to warn that it is retired) and [General] ConfigVersion included.
config::LegacyImport<PluginConfig> PluginConfigLegacyImport(const PluginConfigSchema& schema);

}  // namespace cameraunlock::reframework
