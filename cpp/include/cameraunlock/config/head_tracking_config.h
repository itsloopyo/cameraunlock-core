#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "cameraunlock/config/config_key_schema.g.h"
#include "cameraunlock/data/position_settings.h"

namespace cameraunlock {

/// The C++ twin of CameraUnlock.Core.Config.HeadTrackingConfigData.
///
/// Both halves resolve keys through the table generated from data/config-schema.json, so a
/// spelling accepted by one is accepted by the other. Matching is SECTION-LESS and
/// case-insensitive - a key is lowercased and stripped of '_' and '-' before lookup - which
/// is why ApplyValues takes plain key/value pairs and ParseIniConfig throws section headers
/// away. The canonical sections ([Network] [General] [Sensitivity] [Inversion] [Smoothing]
/// [Position] [Hotkeys] [Reticle]) decide file layout and documentation, not parsing.
///
/// This is deliberately NOT built on IniReader. IniReader sits on GetPrivateProfileStringA,
/// which can only answer "what is the value of key K in section S" - it cannot enumerate,
/// so it cannot implement section-less matching, and its lookup would miss any spelling that
/// differs from the probed one by a '_' or a '-'. ParseIniConfig reads the text directly and
/// mirrors ConfigParsingUtils.ParseIniFile line for line. IniReader stays for the mods that
/// already read their own game-specific keys through it.
struct HeadTrackingConfig {
    using LogFn = std::function<void(const std::string&)>;

    int udp_port = 4242;
    bool enable_on_startup = true;

    float yaw_sensitivity = 1.0f;
    float pitch_sensitivity = 1.0f;
    float roll_sensitivity = 1.0f;
    bool invert_yaw = false;
    bool invert_pitch = false;
    bool invert_roll = false;

    float local_smoothing = static_cast<float>(math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(math::kDefaultRemoteSmoothing);

    bool world_space_yaw = true;
    bool aim_decoupling_enabled = true;
    bool show_decoupled_reticle = true;
    float reticle_color_rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    bool position_enabled = true;
    PositionSettings position = PositionSettings::Default();
    float tracker_pivot_forward = 0.0f;
    float tracker_pivot_up = 0.0f;

    std::string recenter_key_name = "Home";
    std::string toggle_key_name = "End";
    std::string yaw_mode_key_name = "PageDown";
    std::string position_toggle_key_name = "PageUp";
    std::string reticle_toggle_key_name = "Insert";
    std::string cycle_tracking_mode_key_name;

    /// Applies parsed key/value pairs. Keys that resolve to no concept are ignored, so a
    /// mod's own game-specific keys can share the file.
    void ApplyValues(const std::vector<std::pair<std::string, std::string>>& values,
                     const LogFn& log = nullptr);

    /// Reads an INI file and applies it. A missing file leaves every default in place.
    static HeadTrackingConfig LoadFromFile(const std::string& path, const LogFn& log = nullptr);
};

/// Parses an INI-style file into key/value pairs, discarding section headers. Mirrors
/// ConfigParsingUtils.ParseIniFile: '#' and ';' start a comment, an inline comment is
/// stripped only outside a quoted value, and a wholly quoted value has its quotes removed.
std::vector<std::pair<std::string, std::string>> ParseIniConfig(const std::string& path);

/// Value parsers shared with the C# side. Each rejects input it cannot use rather than
/// substituting a value, so a caller can tell "absent" from "unusable".
bool TryParseConfigFloat(const std::string& value, float& out);
bool TryParseConfigInt(const std::string& value, int& out);
bool TryParseConfigBool(const std::string& value, bool& out);
bool TryParseConfigColor(const std::string& value, float out_rgba[4]);

}  // namespace cameraunlock
