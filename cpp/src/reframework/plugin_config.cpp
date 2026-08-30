#include <cameraunlock/reframework/plugin_config.h>

#include <cameraunlock/config/ini_reader.h>
#include <cameraunlock/config/value_guards.h>
#include <cameraunlock/math/finite_utils.h>
#include <cameraunlock/protocol/port_utils.h>
#include <cameraunlock/reframework/log_callback.h>

#include <cctype>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <string>
#include <vector>

namespace cameraunlock::reframework {
namespace {

std::string TrimAscii(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
    size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) --end;
    return text.substr(begin, end - begin);
}

bool EqualsIgnoreCase(const std::string& value, const char* other) {
    size_t i = 0;
    for (; i < value.size(); ++i) {
        if (other[i] == '\0') return false;
        if (std::tolower(static_cast<unsigned char>(value[i])) !=
            std::tolower(static_cast<unsigned char>(other[i]))) {
            return false;
        }
    }
    return other[i] == '\0';
}

bool SectionOf(const std::string& line, std::string& out) {
    const std::string trimmed = TrimAscii(line);
    if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']') return false;
    out = TrimAscii(trimmed.substr(1, trimmed.size() - 2));
    return true;
}

bool KeyOf(const std::string& line, std::string& out) {
    const std::string trimmed = TrimAscii(line);
    if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') return false;
    const size_t equals = trimmed.find('=');
    if (equals == std::string::npos) return false;
    out = TrimAscii(trimmed.substr(0, equals));
    return !out.empty();
}

// A config split so it can be put back together byte for byte. Every line keeps
// its own terminator, so a file that is LF stays LF and a final line with no
// newline stays that way.
struct IniLines {
    std::vector<std::string> text;
    std::vector<std::string> ends;
    std::string eol;
};

IniLines SplitLines(const std::string& content) {
    IniLines file;
    file.eol = content.find("\r\n") == std::string::npos ? "\n" : "\r\n";
    size_t begin = 0;
    while (begin < content.size()) {
        const size_t newline = content.find('\n', begin);
        if (newline == std::string::npos) {
            file.text.push_back(content.substr(begin));
            file.ends.push_back("");
            break;
        }
        size_t end = newline;
        std::string terminator = "\n";
        if (end > begin && content[end - 1] == '\r') {
            --end;
            terminator = "\r\n";
        }
        file.text.push_back(content.substr(begin, end - begin));
        file.ends.push_back(terminator);
        begin = newline + 1;
    }
    return file;
}

struct IniEdit {
    const char* section;
    const char* key;
    std::string value;
};

// Sets one key and touches nothing else. Rewriting the whole file through
// Save() would keep the values but lose the comments, the ordering and any key
// this build does not know about, which is most of what a user has actually
// edited.
void ApplyEdit(IniLines& file, const IniEdit& edit) {
    bool inSection = false;
    bool sectionSeen = false;
    size_t insertAt = 0;

    for (size_t i = 0; i < file.text.size(); ++i) {
        std::string section;
        if (SectionOf(file.text[i], section)) {
            inSection = EqualsIgnoreCase(section, edit.section);
            if (inSection) {
                sectionSeen = true;
                insertAt = i + 1;
            }
            continue;
        }
        if (!inSection) continue;
        if (!TrimAscii(file.text[i]).empty()) insertAt = i + 1;
        std::string key;
        if (KeyOf(file.text[i], key) && EqualsIgnoreCase(key, edit.key)) {
            file.text[i] = std::string(edit.key) + "=" + edit.value;
            if (file.ends[i].empty()) file.ends[i] = file.eol;
            return;
        }
    }

    const std::string line = std::string(edit.key) + "=" + edit.value;
    if (sectionSeen) {
        file.text.insert(file.text.begin() + static_cast<std::ptrdiff_t>(insertAt), line);
        file.ends.insert(file.ends.begin() + static_cast<std::ptrdiff_t>(insertAt), file.eol);
        return;
    }

    if (!file.text.empty()) {
        if (file.ends.back().empty()) file.ends.back() = file.eol;
        if (!TrimAscii(file.text.back()).empty()) {
            file.text.push_back("");
            file.ends.push_back(file.eol);
        }
    }
    file.text.push_back(std::string("[") + edit.section + "]");
    file.ends.push_back(file.eol);
    file.text.push_back(line);
    file.ends.push_back(file.eol);
}

bool ApplyIniEdits(const char* path, const std::vector<IniEdit>& edits, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        error = "it could not be opened for reading";
        return false;
    }
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    in.close();

    IniLines file = SplitLines(content);
    for (const IniEdit& edit : edits) {
        ApplyEdit(file, edit);
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        error = "it could not be opened for writing";
        return false;
    }
    for (size_t i = 0; i < file.text.size(); ++i) {
        out << file.text[i] << file.ends[i];
    }
    out.flush();
    if (!out) {
        error = "the write did not complete";
        return false;
    }
    return true;
}

// A shipped default that changed belongs to one game, not to the RE Engine, and
// PluginConfigSchema carries no mod identifier, so the title each mod builds its
// schema with is what names the mod here.
constexpr const char* kRe8Title = "RE8 Head Tracking";

void MigrateToCurrentVersion(const char* path, const PluginConfigSchema& schema,
                             PluginConfig& config) {
    if (config.configVersion >= kPluginConfigVersion) return;
    if (std::strcmp(schema.title, kRe8Title) != 0) return;

    std::vector<IniEdit> edits;
    edits.push_back({"General", "ConfigVersion", std::to_string(kPluginConfigVersion)});

    // RE8 shipped InvertX=true, which cancelled the negation the camera boundary
    // already applies and mirrored the lateral lean. The shipped value is false
    // now, but Load writes the file only when it could not read one, so an INI
    // from before that keeps the wrong value through every update.
    //
    // A config with no version stamp cannot say whether that true was chosen or
    // inherited, so it is corrected. The stamp written alongside it is what makes
    // the answer knowable from here on: a user who wants the mirrored lean sets it
    // back and keeps it, because a stamped config is never migrated again.
    if (schema.positionInvertKeys && config.positionInvertX) {
        config.positionInvertX = false;
        edits.push_back({"Position", "InvertX", "false"});
        LogWarning(
            "[Position] InvertX was true, which mirrors the lateral lean on this title. "
            "Setting it to false and stamping the config ConfigVersion=%d. If you set it "
            "deliberately, set it back - a stamped config is not migrated again.",
            kPluginConfigVersion);
    }

    std::string error;
    if (!ApplyIniEdits(path, edits, error)) {
        LogError("Could not migrate %s to ConfigVersion %d because %s. The corrected values "
                 "are in effect for this session only; the file is unchanged and will be "
                 "migrated again on the next launch.",
                 path, kPluginConfigVersion, error.c_str());
        return;
    }
    config.configVersion = kPluginConfigVersion;
    LogInfo("Config migrated to ConfigVersion %d", kPluginConfigVersion);
}

} // namespace

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

    // Floored at 0, not 0.1. The old floors let roll be zeroed and refused to let
    // yaw or pitch be, which is not a distinction anything downstream makes: a
    // user pinning one rotation axis is asking for exactly what a 0 multiplier
    // does. The ceilings are shared for the same reason.
    yawMultiplier = SanitizeFinite(yawMultiplier, defaults.yawMultiplier, kMinSensitivity, kMaxRotationSensitivity);
    pitchMultiplier = SanitizeFinite(pitchMultiplier, defaults.pitchMultiplier, kMinSensitivity, kMaxRotationSensitivity);
    rollMultiplier = SanitizeFinite(rollMultiplier, defaults.rollMultiplier, kMinSensitivity, kMaxRotationSensitivity);

    // Validation only: NaN/Inf falls back to the default, finite values clamp to
    // [0,1]. There is no minimum floor - 0.0 means the user asked for zero
    // smoothing and gets it.
    localSmoothing = SanitizeFinite(localSmoothing, defaults.localSmoothing, 0.0f, 1.0f);
    remoteSmoothing = SanitizeFinite(remoteSmoothing, defaults.remoteSmoothing, 0.0f, 1.0f);

    positionSensitivityX = SanitizeFinite(positionSensitivityX, defaults.positionSensitivityX, kMinSensitivity, kMaxPositionSensitivity);
    positionSensitivityY = SanitizeFinite(positionSensitivityY, defaults.positionSensitivityY, kMinSensitivity, kMaxPositionSensitivity);
    positionSensitivityZ = SanitizeFinite(positionSensitivityZ, defaults.positionSensitivityZ, kMinSensitivity, kMaxPositionSensitivity);

    positionLimitX = SanitizeFinite(positionLimitX, defaults.positionLimitX, kMinPositionLimit, kMaxPositionLimit);
    positionLimitY = SanitizeFinite(positionLimitY, defaults.positionLimitY, kMinPositionLimit, kMaxPositionLimit);
    positionLimitZ = SanitizeFinite(positionLimitZ, defaults.positionLimitZ, kMinPositionLimit, kMaxPositionLimit);
    positionLimitZBack = SanitizeFinite(positionLimitZBack, defaults.positionLimitZBack, kMinPositionLimit, kMaxPositionLimit);

    if (schema.flashlight) {
        flashlightMultiplier = SanitizeFinite(flashlightMultiplier, defaults.flashlightMultiplier, 0.0f, kMaxFlashlightMultiplier);
    }
}

// Every number in the file goes through config::ReadFloatChecked rather than
// IniReader::ReadFloat. ReadFloat is a strtod PREFIX parse, so "LocalSmoothing=0,15"
// - a European decimal comma, which is the expected user error - yielded 0.0,
// sat inside the valid range and passed every check with nothing in the log.
// ReadFloatChecked strips the inline comment, requires the whole token to parse,
// and reports what it had to correct.
//
// LogWarning has REFramework's own printf signature, which is what
// config::LogSink is, so the diagnostics come out with the plugin's log tag on
// them.
static float ReadFloat(const cameraunlock::IniReader& reader, const char* section,
                       const char* key, float fallback, float lo, float hi) {
    return cameraunlock::config::ReadFloatChecked(reader, section, key, fallback, lo, hi,
                                                  &LogWarning);
}

// A virtual key GetAsyncKeyState can never report is a hotkey that silently
// does nothing: ToggleKey=0x230 registered and was polled forever without ever
// firing, and the user has no way to tell that from a broken mod.
static int ReadHotkey(const cameraunlock::IniReader& reader, const char* key, int fallback) {
    const int vk = reader.ReadHex("Hotkeys", key, fallback);
    if (cameraunlock::config::IsBindableVirtualKey(vk)) return vk;
    LogWarning("Config key [Hotkeys] %s=0x%X is not a key that can be polled "
               "(GetAsyncKeyState defines 0x01-0xFE, and Ctrl/Shift/Alt are reserved "
               "for the chord bindings) - using the default 0x%X instead",
               key, vk, fallback);
    return fallback;
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

    yawMultiplier = ReadFloat(reader, "Sensitivity", "YawMultiplier", yawMultiplier,
                              kMinSensitivity, kMaxRotationSensitivity);
    pitchMultiplier = ReadFloat(reader, "Sensitivity", "PitchMultiplier", pitchMultiplier,
                                kMinSensitivity, kMaxRotationSensitivity);
    rollMultiplier = ReadFloat(reader, "Sensitivity", "RollMultiplier", rollMultiplier,
                               kMinSensitivity, kMaxRotationSensitivity);

    localSmoothing = ReadFloat(reader, "Smoothing", "LocalSmoothing", localSmoothing, 0.0f, 1.0f);
    remoteSmoothing = ReadFloat(reader, "Smoothing", "RemoteSmoothing", remoteSmoothing, 0.0f, 1.0f);

    cameraunlock::config::WarnRetiredSmoothingKey(reader, "Position", "Smoothing", &LogWarning);

    toggleKey = ReadHotkey(reader, "ToggleKey", toggleKey);
    positionToggleKey = ReadHotkey(reader, "PositionToggleKey", positionToggleKey);
    yawModeKey = ReadHotkey(reader, "YawModeKey", yawModeKey);
    if (schema.diagnosticMarkerKey) {
        diagnosticMarkerKey = ReadHotkey(reader, "DiagnosticMarkerKey", diagnosticMarkerKey);
    }

    positionSensitivityX = ReadFloat(reader, "Position", "SensitivityX", positionSensitivityX,
                                     kMinSensitivity, kMaxPositionSensitivity);
    positionSensitivityY = ReadFloat(reader, "Position", "SensitivityY", positionSensitivityY,
                                     kMinSensitivity, kMaxPositionSensitivity);
    positionSensitivityZ = ReadFloat(reader, "Position", "SensitivityZ", positionSensitivityZ,
                                     kMinSensitivity, kMaxPositionSensitivity);
    positionLimitX = ReadFloat(reader, "Position", "LimitX", positionLimitX,
                               kMinPositionLimit, kMaxPositionLimit);
    positionLimitY = ReadFloat(reader, "Position", "LimitY", positionLimitY,
                               kMinPositionLimit, kMaxPositionLimit);
    positionLimitZ = ReadFloat(reader, "Position", "LimitZ", positionLimitZ,
                               kMinPositionLimit, kMaxPositionLimit);
    positionLimitZBack = ReadFloat(reader, "Position", "LimitZBack", positionLimitZBack,
                                   kMinPositionLimit, kMaxPositionLimit);
    if (schema.positionInvertKeys) {
        positionInvertX = reader.ReadBool("Position", "InvertX", positionInvertX);
        positionInvertY = reader.ReadBool("Position", "InvertY", positionInvertY);
        positionInvertZ = reader.ReadBool("Position", "InvertZ", positionInvertZ);
    }
    positionEnabled = reader.ReadBool("Position", "Enabled", positionEnabled);

    if (schema.flashlight) {
        flashlightTracking = reader.ReadBool("Flashlight", "Enabled", flashlightTracking);
        flashlightMultiplier = ReadFloat(reader, "Flashlight", "Multiplier", flashlightMultiplier,
                                         0.0f, kMaxFlashlightMultiplier);
    }

    autoEnable = reader.ReadBool("General", "AutoEnable", autoEnable);
    worldSpaceYaw = reader.ReadBool("General", "WorldSpaceYaw", worldSpaceYaw);
    configVersion = reader.ReadInt("General", "ConfigVersion", 0);

    Validate(schema);
    LogInfo("Config loaded from %s", path);
    MigrateToCurrentVersion(path, schema, *this);
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
    file << "; Format version of this file. The mod stamps it; lowering it re-applies\n";
    file << "; the corrections the mod makes to shipped values that changed.\n";
    file << "ConfigVersion=" << kPluginConfigVersion << "\n";

    file.close();
    LogInfo("Config saved to %s", path);
    return true;
}

} // namespace cameraunlock::reframework
