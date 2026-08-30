#include <cameraunlock/config/value_guards.h>

#include <cameraunlock/math/finite_utils.h>

#include <cmath>
#include <cstddef>
#include <cstdlib>

namespace cameraunlock::config {

namespace {

float Guard(const char* key, float value, float fallback, float lo, float hi, LogSink log) {
    const float sanitized = math::SanitizeFinite(value, fallback, lo, hi);
    if (log == nullptr) return sanitized;

    if (!std::isfinite(value)) {
        log("config: %s is not a finite number; using %g", key,
            static_cast<double>(sanitized));
    } else if (sanitized != value) {
        log("config: %s=%g is outside [%g, %g]; clamped to %g", key,
            static_cast<double>(value), static_cast<double>(lo), static_cast<double>(hi),
            static_cast<double>(sanitized));
    }
    return sanitized;
}

}  // namespace

float SanitizeSmoothing(const char* key, float value, float fallback, LogSink log) {
    return Guard(key, value, fallback, 0.0f, 1.0f, log);
}

float SanitizeSensitivity(const char* key, float value, float fallback, LogSink log) {
    return Guard(key, value, fallback, -kMaxSensitivity, kMaxSensitivity, log);
}

float SanitizePositionLimit(const char* key, float value, float fallback, LogSink log) {
    return Guard(key, value, fallback, 0.0f, kMaxPositionLimit, log);
}

bool IsBindableVirtualKey(int vk) {
    if (vk < 0x01 || vk > 0xFE) return false;
    if (vk >= 0x10 && vk <= 0x12) return false;  // Shift, Control, Alt
    if (vk >= 0xA0 && vk <= 0xA5) return false;  // and their left/right halves
    return true;
}

bool ParseFloatStrict(const std::string& text, float& out) {
    if (text.empty()) return false;

    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0') return false;

    out = static_cast<float>(parsed);
    return true;
}

std::string ReadRawValue(const IniReader& ini, const char* section, const char* key) {
    std::string text = ini.ReadString(section, key, "");
    const std::size_t comment = text.find_first_of(";#");
    if (comment != std::string::npos) text.erase(comment);
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

float ReadFloatChecked(const IniReader& ini, const char* section, const char* key,
                       float fallback, float lo, float hi, LogSink log) {
    const std::string raw = ReadRawValue(ini, section, key);
    if (raw.empty()) return fallback;

    float parsed = 0.0f;
    if (!ParseFloatStrict(raw, parsed)) {
        if (log != nullptr) {
            log("config: [%s] %s=%s is not a number, so the default %g is used instead. "
                "Use a dot for the decimal point.",
                section, key, raw.c_str(), static_cast<double>(fallback));
        }
        return fallback;
    }

    // The key name reaches Guard's diagnostic alone, so the section is prefixed
    // here: two sections can carry the same key name and "Sensitivity clamped"
    // does not say which one the user has to go and edit.
    const std::string qualified = std::string("[") + section + "] " + key;
    return Guard(qualified.c_str(), parsed, fallback, lo, hi, log);
}

void WarnRetiredSmoothingKey(const IniReader& reader, const char* section,
                             const char* key, LogSink log) {
    static bool warned = false;
    if (warned) return;
    // Latched only once the warning is actually emitted. Latching first would
    // let a caller with no sink burn the one chance any later caller had of
    // seeing it.
    if (log == nullptr) return;
    if (ReadRawValue(reader, section, key).empty()) return;
    warned = true;
    log("Config key [%s] %s has been retired and is IGNORED. Smoothing is now two "
        "keys: LocalSmoothing (default 0, applies to a tracker on this machine) and "
        "RemoteSmoothing (default 0.15, applies to a tracker on the network). The "
        "old value is not migrated because the semantics changed - it carried a "
        "hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

}  // namespace cameraunlock::config
