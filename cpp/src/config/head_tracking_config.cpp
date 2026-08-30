#include "cameraunlock/config/head_tracking_config.h"

#include <cmath>
#include <fstream>
#include <locale>
#include <sstream>

namespace cameraunlock {

namespace {

std::string Trim(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string ToLower(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

// Truncates at the first ';' or '#' that is not inside a quoted section, mirroring
// ConfigParsingUtils.StripInlineComment.
std::string StripInlineComment(const std::string& value) {
    bool in_quotes = false;
    char quote = '\0';
    for (size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (in_quotes) {
            if (c == quote) in_quotes = false;
            continue;
        }
        if (c == '"' || c == '\'') {
            in_quotes = true;
            quote = c;
            continue;
        }
        if (c == ';' || c == '#') {
            const std::string head = value.substr(0, i);
            const size_t end = head.find_last_not_of(" \t");
            if (end == std::string::npos) return std::string();
            return head.substr(0, end + 1);
        }
    }
    return value;
}

float Clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

}  // namespace

bool TryParseConfigFloat(const std::string& value, float& out) {
    const std::string trimmed = Trim(value);
    if (trimmed.empty()) return false;

    std::istringstream stream(trimmed);
    stream.imbue(std::locale::classic());
    double parsed = 0.0;
    stream >> parsed;
    if (stream.fail()) return false;

    // Anything left over means the text was not wholly a number ("1.5m", "0,3"). C#'s
    // float.TryParse rejects those, and accepting a prefix here would let the two halves
    // disagree about the same config line.
    char leftover;
    if (stream >> leftover) return false;

    // NaN and the infinities parse as far as the stream is concerned. A config file is a
    // system boundary and a non-finite value is not recoverable downstream: every
    // comparison against NaN is false, so no clamp fires, and the smoothed pose is NaN
    // from that frame on, permanently.
    if (!std::isfinite(parsed)) return false;

    out = static_cast<float>(parsed);
    return std::isfinite(out);
}

bool TryParseConfigInt(const std::string& value, int& out) {
    const std::string trimmed = Trim(value);
    if (trimmed.empty()) return false;

    std::istringstream stream(trimmed);
    stream.imbue(std::locale::classic());
    long long parsed = 0;
    stream >> parsed;
    if (stream.fail()) return false;

    char leftover;
    if (stream >> leftover) return false;
    if (parsed < -2147483648LL || parsed > 2147483647LL) return false;

    out = static_cast<int>(parsed);
    return true;
}

bool TryParseConfigBool(const std::string& value, bool& out) {
    const std::string trimmed = ToLower(Trim(value));
    if (trimmed == "true" || trimmed == "yes" || trimmed == "1") {
        out = true;
        return true;
    }
    if (trimmed == "false" || trimmed == "no" || trimmed == "0") {
        out = false;
        return true;
    }
    return false;
}

bool TryParseConfigColor(const std::string& value, float out_rgba[4]) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : value) {
        if (c == ',') {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    if (parts.size() < 3) return false;

    float r, g, b;
    float a = 1.0f;
    if (!TryParseConfigFloat(parts[0], r)) return false;
    if (!TryParseConfigFloat(parts[1], g)) return false;
    if (!TryParseConfigFloat(parts[2], b)) return false;
    // A supplied alpha must parse. Falling back to 1.0 would hide a typo.
    if (parts.size() >= 4 && !TryParseConfigFloat(parts[3], a)) return false;

    // 0-255 detected off the max of RGB, not per channel, so a single out-of-range value is
    // still scaled with its peers. Alpha is detected independently because it routinely
    // arrives as 1.0 even in 0-255 input.
    float max_rgb = r;
    if (g > max_rgb) max_rgb = g;
    if (b > max_rgb) max_rgb = b;
    if (max_rgb > 1.0f) {
        r /= 255.0f;
        g /= 255.0f;
        b /= 255.0f;
    }
    if (a > 1.0f) a /= 255.0f;

    out_rgba[0] = Clamp01(r);
    out_rgba[1] = Clamp01(g);
    out_rgba[2] = Clamp01(b);
    out_rgba[3] = Clamp01(a);
    return true;
}

std::vector<std::pair<std::string, std::string>> ParseIniConfig(const std::string& path) {
    std::vector<std::pair<std::string, std::string>> result;

    std::ifstream file(path);
    if (!file.is_open()) return result;

    std::string line;
    while (std::getline(file, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';' || trimmed[0] == '[') {
            continue;
        }

        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos || eq == 0) continue;

        const std::string key = Trim(trimmed.substr(0, eq));
        std::string value = Trim(StripInlineComment(Trim(trimmed.substr(eq + 1))));

        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }

        result.emplace_back(key, value);
    }

    return result;
}

void HeadTrackingConfig::ApplyValues(
    const std::vector<std::pair<std::string, std::string>>& values, const LogFn& log) {
    // Warned once per process rather than once per load: mods reload config on a hotkey or
    // a file watcher, and repeating this every reload buries it.
    static bool warned_retired_smoothing = false;

    bool saw_limit_y = false;
    bool saw_limit_y_down = false;

    for (const auto& entry : values) {
        const char* key = ResolveConfigKey(entry.first);
        if (key == nullptr) continue;

        const std::string& value = entry.second;
        const std::string canonical(key);

        int int_val = 0;
        float float_val = 0.0f;
        bool bool_val = false;

        if (canonical == config_keys::kUdpPort) {
            // Range-checked here rather than left to the socket, and against the actual
            // protocol range: Windows imposes no privileged-port restriction on a UDP bind,
            // so a user already running below 1024 has a working configuration.
            if (TryParseConfigInt(value, int_val)) {
                if (int_val >= 1 && int_val <= 65535) {
                    udp_port = int_val;
                } else if (log) {
                    log("Config key '" + entry.first + "' has an out-of-range value '" + value +
                        "' (expected 1-65535) - using " + std::to_string(udp_port));
                }
            }
        } else if (canonical == config_keys::kEnableOnStartup) {
            if (TryParseConfigBool(value, bool_val)) enable_on_startup = bool_val;
        } else if (canonical == config_keys::kYawSensitivity) {
            if (TryParseConfigFloat(value, float_val)) yaw_sensitivity = float_val;
        } else if (canonical == config_keys::kPitchSensitivity) {
            if (TryParseConfigFloat(value, float_val)) pitch_sensitivity = float_val;
        } else if (canonical == config_keys::kRollSensitivity) {
            if (TryParseConfigFloat(value, float_val)) roll_sensitivity = float_val;
        } else if (canonical == config_keys::kInvertYaw) {
            if (TryParseConfigBool(value, bool_val)) invert_yaw = bool_val;
        } else if (canonical == config_keys::kInvertPitch) {
            if (TryParseConfigBool(value, bool_val)) invert_pitch = bool_val;
        } else if (canonical == config_keys::kInvertRoll) {
            if (TryParseConfigBool(value, bool_val)) invert_roll = bool_val;
        } else if (canonical == config_keys::kLocalSmoothing) {
            if (TryParseConfigFloat(value, float_val)) {
                local_smoothing = Clamp01(float_val);
            } else {
                local_smoothing = static_cast<float>(math::kDefaultLocalSmoothing);
                if (log) {
                    log("Config key 'LocalSmoothing' has an unusable value '" + value +
                        "' (not a finite number) - using " + std::to_string(local_smoothing));
                }
            }
        } else if (canonical == config_keys::kRemoteSmoothing) {
            if (TryParseConfigFloat(value, float_val)) {
                remote_smoothing = Clamp01(float_val);
            } else {
                remote_smoothing = static_cast<float>(math::kDefaultRemoteSmoothing);
                if (log) {
                    log("Config key 'RemoteSmoothing' has an unusable value '" + value +
                        "' (not a finite number) - using " + std::to_string(remote_smoothing));
                }
            }
        } else if (canonical == config_keys::kWorldSpaceYaw) {
            if (TryParseConfigBool(value, bool_val)) world_space_yaw = bool_val;
        } else if (canonical == config_keys::kAimDecoupling) {
            if (TryParseConfigBool(value, bool_val)) aim_decoupling_enabled = bool_val;
        } else if (canonical == config_keys::kShowReticle) {
            if (TryParseConfigBool(value, bool_val)) show_decoupled_reticle = bool_val;
        } else if (canonical == config_keys::kReticleColor) {
            float rgba[4];
            if (TryParseConfigColor(value, rgba)) {
                for (int i = 0; i < 4; ++i) reticle_color_rgba[i] = rgba[i];
            }
        } else if (canonical == config_keys::kPositionEnabled) {
            if (TryParseConfigBool(value, bool_val)) position_enabled = bool_val;
        } else if (canonical == config_keys::kPositionSensitivityX) {
            if (TryParseConfigFloat(value, float_val)) position.sensitivity_x = float_val;
        } else if (canonical == config_keys::kPositionSensitivityY) {
            if (TryParseConfigFloat(value, float_val)) position.sensitivity_y = float_val;
        } else if (canonical == config_keys::kPositionSensitivityZ) {
            if (TryParseConfigFloat(value, float_val)) position.sensitivity_z = float_val;
        } else if (canonical == config_keys::kPositionLimitX) {
            if (TryParseConfigFloat(value, float_val)) position.limit_x = float_val;
        } else if (canonical == config_keys::kPositionLimitY) {
            if (TryParseConfigFloat(value, float_val)) {
                position.limit_y = float_val;
                saw_limit_y = true;
            }
        } else if (canonical == config_keys::kPositionLimitYDown) {
            if (TryParseConfigFloat(value, float_val)) {
                position.limit_y_down = float_val;
                saw_limit_y_down = true;
            }
        } else if (canonical == config_keys::kPositionLimitZ) {
            if (TryParseConfigFloat(value, float_val)) position.limit_z = float_val;
        } else if (canonical == config_keys::kPositionLimitZBack) {
            if (TryParseConfigFloat(value, float_val)) position.limit_z_back = float_val;
        } else if (canonical == config_keys::kInvertPositionX) {
            if (TryParseConfigBool(value, bool_val)) position.invert_x = bool_val;
        } else if (canonical == config_keys::kInvertPositionY) {
            if (TryParseConfigBool(value, bool_val)) position.invert_y = bool_val;
        } else if (canonical == config_keys::kInvertPositionZ) {
            if (TryParseConfigBool(value, bool_val)) position.invert_z = bool_val;
        } else if (canonical == config_keys::kTrackerPivotForward) {
            if (TryParseConfigFloat(value, float_val)) tracker_pivot_forward = float_val;
        } else if (canonical == config_keys::kTrackerPivotUp) {
            if (TryParseConfigFloat(value, float_val)) tracker_pivot_up = float_val;
        } else if (canonical == config_keys::kRecenterKey) {
            recenter_key_name = value;
        } else if (canonical == config_keys::kToggleKey) {
            toggle_key_name = value;
        } else if (canonical == config_keys::kYawModeKey) {
            yaw_mode_key_name = value;
        } else if (canonical == config_keys::kPositionToggleKey) {
            position_toggle_key_name = value;
        } else if (canonical == config_keys::kReticleToggleKey) {
            reticle_toggle_key_name = value;
        } else if (canonical == config_keys::kCycleTrackingModeKey) {
            cycle_tracking_mode_key_name = value;
        } else if (canonical == config_keys::kSmoothing) {
            // Deliberately NOT migrated. The old single value carried a hidden 0.15 floor,
            // so the number in an existing config does not mean what it used to.
            if (!warned_retired_smoothing) {
                warned_retired_smoothing = true;
                if (log) {
                    log("Config key '" + entry.first +
                        "' has been retired and is IGNORED. Smoothing is now two keys: "
                        "LocalSmoothing (a tracker on this machine) and RemoteSmoothing (a "
                        "tracker on the network). The old value is not migrated because the "
                        "semantics changed - it carried a hidden floor that no longer exists. "
                        "Set the two new keys.");
                }
            }
        }
    }

    // A file that names one vertical limit means one vertical limit. The clamp is
    // [-limit_y_down, +limit_y], so leaving the down side at its struct default silently caps
    // a raised limit_y at 0.20m downward - the exact bug ~47 mod repos carry today, each of
    // which hand-mirrors the key or does not. Decided after the loop, so entry order cannot
    // change the outcome, and an explicit LimitYDown always wins.
    if (saw_limit_y && !saw_limit_y_down) {
        position.limit_y_down = position.limit_y;
    }

    position.local_smoothing = local_smoothing;
    position.remote_smoothing = remote_smoothing;
}

HeadTrackingConfig HeadTrackingConfig::LoadFromFile(const std::string& path, const LogFn& log) {
    HeadTrackingConfig config;
    const auto values = ParseIniConfig(path);
    if (values.empty()) {
        if (log) log("No config file found, using defaults");
        return config;
    }
    config.ApplyValues(values, log);
    if (log) log("Config loaded successfully");
    return config;
}

}  // namespace cameraunlock
