#include <cameraunlock/reframework/plugin_config.h>

#include <cameraunlock/config/checked_file_writer.h>
#include <cameraunlock/config/ini_editor.h>
#include <cameraunlock/config/ini_reader.h>
#include <cameraunlock/config/value_guards.h>
#include <cameraunlock/math/finite_utils.h>
#include <cameraunlock/protocol/port_utils.h>
#include <cameraunlock/reframework/log_callback.h>

#include <windows.h>

#include <cstring>
#include <fstream>
#include <iomanip>
#include <string>
#include <string_view>
#include <vector>

namespace cameraunlock::reframework {
namespace {

struct IniEdit {
    const char* section;
    const char* key;
    std::string value;
};

// ANSI, like every other path in this file: IniReader opens it with
// GetPrivateProfileStringA, and PluginMod narrows it with os::NarrowToAnsi.
bool WidenAnsiPath(const char* path, std::wstring& wide, std::string& error) {
    const int length = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
    if (length > 0) {
        wide.assign(static_cast<size_t>(length), L'\0');
        if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, path, -1, &wide[0], length) == length) {
            wide.resize(static_cast<size_t>(length) - 1);
            return true;
        }
    }
    error = "its path is not valid in the ANSI code page (Windows error " +
            std::to_string(GetLastError()) + "); the file is unchanged";
    return false;
}

// 0 with the file's bytes in `bytes`, or the Win32 error that stopped the read.
DWORD ReadWholeFile(const std::wstring& path, std::string& bytes) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    DWORD error = 0;
    char buffer[4096];
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(file, buffer, sizeof(buffer), &read, nullptr)) {
            error = GetLastError();
            break;
        }
        if (read == 0) break;
        bytes.append(buffer, read);
    }
    if (!CloseHandle(file) && error == 0) error = GetLastError();
    return error;
}

std::string DescribeRefusal(const cameraunlock::IniEditResult& result) {
    std::string text = std::string("it cannot be edited safely (") +
                       cameraunlock::IniEditRefusalName(result.refusal);
    if (!result.key.empty()) text += " for [" + result.section + "] " + result.key;
    for (size_t i = 0; i < result.lines.size(); ++i) {
        text += (i == 0 ? (result.lines.size() == 1 ? " on line " : " on lines ") : ", ");
        text += std::to_string(result.lines[i]);
    }
    return text + "); the file is unchanged";
}

// WriteFileChecked names the temporary "<target>.<32 hex digits>.tmp", so all of it
// after the target is ASCII.
std::string DescribeTemporary(const char* path, const std::wstring& temporary) {
    constexpr size_t kSuffixLength = 1 + 32 + 4;
    std::string text = path;
    for (size_t i = temporary.size() - kSuffixLength; i < temporary.size(); ++i) {
        text += static_cast<char>(temporary[i]);
    }
    return text;
}

std::string DescribeWriteFailure(const char* path, const cameraunlock::CheckedWriteResult& result) {
    std::string text;
    if (result.status != cameraunlock::CheckedWriteStatus::Failed) {
        text = std::string("it changed on disk while it was being edited (") +
               cameraunlock::CheckedWriteStatusName(result.status) +
               "), so the edit was dropped and the file left as it now is";
    } else {
        text = std::string("the ") + cameraunlock::CheckedWriteStepName(result.failed_step) +
               " step failed with Windows error " + std::to_string(result.error);
        if (result.outcome_uncertain) {
            return text + ". Windows could not finish replacing the file, so it may be missing or "
                          "renamed; the edited contents are in " +
                   DescribeTemporary(path, result.temporary_path);
        }
        text += "; the file is unchanged";
    }
    if (!result.temporary_path.empty() && !result.temporary_removed) {
        text += ", and " + DescribeTemporary(path, result.temporary_path) +
                " could not be removed (Windows error " + std::to_string(result.cleanup_error) + ")";
    }
    return text;
}

// GetPrivateProfileStringA reads a UTF-8 byte order mark as part of the first line, so a
// header right behind the mark is not a header to it, while EditIni skips a mark at offset
// 0. Each byte from 0x80 up reaches EditIni as a two-byte letter from U+0180 to U+01FF, so
// it never sees a mark, and FromEditorView maps each letter back to its byte. EditIni copies
// the letters through like any other byte it does not edit.
std::string ToEditorView(const std::string& bytes) {
    std::string view;
    view.reserve(bytes.size() * 2);
    for (const char c : bytes) {
        const unsigned byte = static_cast<unsigned char>(c);
        if (byte < 0x80) {
            view += c;
            continue;
        }
        const unsigned letter = 0x100 + byte;
        view += static_cast<char>(0xC0 | (letter >> 6));
        view += static_cast<char>(0x80 | (letter & 0x3F));
    }
    return view;
}

// The edits are ASCII, so everything else in EditIni's output came from ToEditorView.
std::string FromEditorView(const std::string& view) {
    std::string bytes;
    bytes.reserve(view.size());
    for (size_t i = 0; i < view.size(); ++i) {
        const unsigned lead = static_cast<unsigned char>(view[i]);
        if (lead < 0x80) {
            bytes += view[i];
            continue;
        }
        const unsigned letter = ((lead & 0x1F) << 6) | (static_cast<unsigned char>(view[++i]) & 0x3F);
        bytes += static_cast<char>(letter - 0x100);
    }
    return bytes;
}

bool IsSpaceOrTab(char c) { return c == ' ' || c == '\t'; }

// GetPrivateProfileStringA skips these before or after a key, before a header, just inside
// its brackets and at the start of a value, while EditIni keeps them as part of the line.
// NUL is skipped too, and EditIni refuses it itself.
bool IsSkippedControlByte(char c) {
    const unsigned char b = static_cast<unsigned char>(c);
    return b >= 0x01 && b < 0x20 && c != '\t' && c != '\n' && c != '\r';
}

std::string HexByte(char c) {
    static const char kHex[] = "0123456789ABCDEF";
    const unsigned char b = static_cast<unsigned char>(c);
    return std::string("0x") + kHex[b >> 4] + kHex[b & 0xF];
}

std::string_view TrimSpaceOrTab(std::string_view text) {
    while (!text.empty() && IsSpaceOrTab(text.front())) text.remove_prefix(1);
    while (!text.empty() && IsSpaceOrTab(text.back())) text.remove_suffix(1);
    return text;
}

bool EqualsAsciiIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i];
        char y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

int LineOfOffset(const std::string& bytes, size_t offset) {
    int line = 1;
    for (size_t i = 0; i < offset; ++i) {
        if (bytes[i] == '\n') ++line;
    }
    return line;
}

std::string Refusal(const std::string& reason) {
    return "it cannot be edited safely: " + reason + "; the file is unchanged";
}

// What a replacement has to keep after the new value: from the first ';' or '#' outside
// quotes, with the white space in front of it, to the end of the line. Empty when the
// value has no such comment. GetPrivateProfileIntA reads "0 ; note" as 0, so a user's
// note on the stamp line stays where it was.
std::string_view InlineComment(std::string_view value) {
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
            size_t begin = i;
            while (begin > 0 && IsSpaceOrTab(value[begin - 1])) --begin;
            return value.substr(begin);
        }
    }
    return {};
}

// EditIni reads the canonical grammar, and GetPrivateProfileStringA reads this file.
// Turns the migration's edits into EditIni edits that change what GetPrivateProfileStringA
// reads, or refuses a file the two read differently.
bool PlanEdits(const std::string& bytes, const std::vector<IniEdit>& edits,
               std::vector<cameraunlock::IniEdit>& batch, std::string& error) {
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] == '\r' && (i + 1 == bytes.size() || bytes[i + 1] != '\n')) {
            error = Refusal("line " + std::to_string(LineOfOffset(bytes, i)) +
                            " holds a CR with no LF after it");
            return false;
        }
    }
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (IsSkippedControlByte(bytes[i])) {
            error = Refusal("line " + std::to_string(LineOfOffset(bytes, i)) +
                            " holds the control byte " + HexByte(bytes[i]));
            return false;
        }
    }

    struct KeyLine {
        size_t header;
        std::string_view key;
        std::string_view value;
        int number;
    };
    // With no lone CR, only LF ends a line. The byte order mark stays part of the first
    // line, as GetPrivateProfileStringA reads it.
    const std::string_view text(bytes);
    std::vector<std::string_view> headers;
    std::vector<KeyLine> keys;
    int number = 0;
    for (size_t begin = 0; begin < text.size();) {
        const size_t newline = text.find('\n', begin);
        size_t end = newline == std::string_view::npos ? text.size() : newline;
        if (end > begin && text[end - 1] == '\r') --end;
        std::string_view line = text.substr(begin, end - begin);
        begin = newline == std::string_view::npos ? text.size() : newline + 1;
        ++number;

        while (!line.empty() && IsSpaceOrTab(line.front())) line.remove_prefix(1);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[') {
            const size_t close = line.find(']', 1);
            if (close == std::string_view::npos) {
                error = Refusal("the section header on line " + std::to_string(number) +
                                " has no ']'");
                return false;
            }
            headers.push_back(TrimSpaceOrTab(line.substr(1, close - 1)));
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string_view key = TrimSpaceOrTab(line.substr(0, equals));
        if (key.empty()) continue;
        if (headers.empty()) continue;
        keys.push_back({headers.size() - 1, key, line.substr(equals + 1), number});
    }

    for (const IniEdit& edit : edits) {
        const std::string name = std::string("[") + edit.section + "] " + edit.key;
        size_t first_header = std::string::npos;
        for (size_t h = 0; h < headers.size() && first_header == std::string::npos; ++h) {
            if (EqualsAsciiIgnoreCase(headers[h], edit.section)) first_header = h;
        }
        const KeyLine* first = nullptr;
        for (const KeyLine& line : keys) {
            if (EqualsAsciiIgnoreCase(headers[line.header], edit.section) &&
                EqualsAsciiIgnoreCase(line.key, edit.key)) {
                first = &line;
                break;
            }
        }
        std::string value = edit.value;
        if (first != nullptr) {
            if (first->header != first_header) {
                error = Refusal(name + " is set only on line " + std::to_string(first->number) +
                                ", under a repeated [" + edit.section +
                                "] header that GetPrivateProfileStringA does not read");
                return false;
            }
            const std::string_view comment = InlineComment(first->value);
            for (const char c : comment) {
                const unsigned char b = static_cast<unsigned char>(c);
                if (b < 0x20 || b > 0x7E) {
                    error = Refusal("the comment after " + name + " on line " +
                                    std::to_string(first->number) + " holds the byte " +
                                    HexByte(c) + ", which the editor cannot write back");
                    return false;
                }
            }
            if (!comment.empty() && comment.back() == ' ') {
                error = Refusal("the comment after " + name + " on line " +
                                std::to_string(first->number) +
                                " ends in a space, which the editor cannot write back");
                return false;
            }
            value += comment;
        }
        batch.push_back({edit.section, edit.key, value, true, true});
    }
    return true;
}

// Sets the given keys and touches nothing else. Rewriting the whole file through
// Save() would keep the values but lose the comments, the ordering and any key
// this build does not know about, which is most of what a user has actually
// edited. A key that is absent is added, and so is its section.
//
// The file is replaced, never opened for writing: WriteFileChecked commits only
// if it still holds the bytes the edit was made from. A missing file is a
// failure, not something to create - Load's caller writes a whole default config
// in that case, and a file holding only these keys would stop it doing so.
bool ApplyIniEdits(const char* path, const std::vector<IniEdit>& edits, std::string& error) {
    std::wstring widePath;
    if (!WidenAnsiPath(path, widePath, error)) return false;

    std::string original;
    const DWORD readError = ReadWholeFile(widePath, original);
    if (readError == ERROR_FILE_NOT_FOUND || readError == ERROR_PATH_NOT_FOUND) {
        error = "it does not exist";
        return false;
    }
    if (readError != 0) {
        error = "it could not be read (Windows error " + std::to_string(readError) +
                "); the file is unchanged";
        return false;
    }

    // ToEditorView would hide a UTF-16 byte order mark from EditIni, which cannot edit
    // UTF-16.
    if (original.compare(0, 2, "\xFF\xFE") == 0 || original.compare(0, 2, "\xFE\xFF") == 0) {
        cameraunlock::IniEditResult utf16;
        utf16.refusal = cameraunlock::IniEditRefusal::Utf16;
        error = DescribeRefusal(utf16);
        return false;
    }

    std::vector<cameraunlock::IniEdit> batch;
    if (!PlanEdits(original, edits, batch, error)) return false;
    const cameraunlock::IniEditResult edited = cameraunlock::EditIni(ToEditorView(original), batch);
    if (!edited.Succeeded()) {
        error = DescribeRefusal(edited);
        return false;
    }

    const cameraunlock::CheckedWriteResult written =
        cameraunlock::WriteFileChecked(widePath, original, FromEditorView(edited.bytes));
    if (!written.Committed()) {
        error = DescribeWriteFailure(path, written);
        return false;
    }
    return true;
}

// A shipped default that changed belongs to one game, not to the RE Engine, so
// a migration is keyed on the schema's mod id.
constexpr const char* kRe8ModId = "re8";

void MigrateToCurrentVersion(const char* path, const PluginConfigSchema& schema,
                             PluginConfig& config) {
    if (config.configVersion >= kPluginConfigVersion) return;
    if (std::strcmp(schema.modId, kRe8ModId) != 0) return;

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
                 "are in effect for this session only, and a file still unstamped is "
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
