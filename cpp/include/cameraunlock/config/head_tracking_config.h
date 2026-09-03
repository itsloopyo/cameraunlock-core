#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "cameraunlock/ads/ads_mode.h"
#include "cameraunlock/config/config_key_schema.g.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/effects/head_follow_light.h"

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

    /// What head tracking does while the sights are up. Parsed with `marker`
    /// ALLOWED, because this struct cannot know whether the mod reading it ships
    /// two slots or three.
    ///
    /// A two-slot mod therefore cannot take this field as it stands: ApplyValues
    /// consumes the raw string and does not keep it, so there is nothing left to
    /// re-parse. Such a mod either reads the key itself with
    /// ads::ParseAdsMode(raw, false) before handing the pairs over, or maps
    /// AdsMode::Marker onto ads::kDefaultAdsMode after loading. Left alone it
    /// would hold a mode it does not implement, and AdsSuspendsTracking(Marker)
    /// is false, so tracking would stay live through ADS with a marker the mod
    /// never draws.
    ads::AdsMode ads_mode = ads::kDefaultAdsMode;

    /// A carried light that follows the head rather than the aim. Inert in a mod
    /// for a game with no carried light; see effects/head_follow_light.h.
    effects::HeadFollowLightSettings light;

    std::string recenter_key_name = "Home";
    std::string toggle_key_name = "End";
    std::string yaw_mode_key_name = "PageDown";
    std::string position_toggle_key_name = "PageUp";
    std::string reticle_toggle_key_name = "Insert";
    std::string cycle_tracking_mode_key_name;

    /// Applies parsed key/value pairs. Keys that resolve to no concept are ignored, so a
    /// mod's own game-specific keys can share the file.
    ///
    /// Every numeric key is range-checked here, and a value outside its range is REFUSED -
    /// reported through @p log, with the field left holding what it had. A mod does not
    /// need its own guard pass over the result, and the two that matter are not survivable
    /// downstream: a negative position limit inverts the bounds of
    /// math::Clamp(v, -limit, limit) and pins the camera at a fixed offset instead of
    /// freeing it, and a sensitivity past config::kMaxSensitivity multiplies the
    /// processor's decomposition out to an infinity that reaches the camera.
    ///
    /// This does NOT cover a mod that builds PositionSettings in code. The guards in
    /// config/value_guards.h are the same bounds for that path.
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
