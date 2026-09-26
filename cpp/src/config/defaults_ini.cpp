#include "cameraunlock/config/defaults_ini.h"

#include "cameraunlock/config/head_tracking_config_table.h"
#include "cameraunlock/config/hotkey_codec.h"
#include "cameraunlock/config/value_codecs.h"
#include "cameraunlock/input/key_names.g.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <array>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cameraunlock::config::detail {

namespace {

constexpr const char* kHeader[] = {
    "; CameraUnlock head tracking defaults, read by every head tracking mod that keeps its",
    "; settings in CameraUnlock.ini. A game uses the value here for each setting its",
    "; CameraUnlock.ini sets to default. A value in a game's CameraUnlock.ini changes that game",
    "; only. The mods never change this file.",
    "; Comments start with ; and go on their own line. Text after a value is part of the value.",
    "; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.",
    "; Only these key names are read here: A to Z, Alpha0 to Alpha9, F1 to F24, Keypad0 to Keypad9,",
    "; KeypadPeriod, KeypadDivide, KeypadMultiply, KeypadMinus, KeypadPlus, UpArrow, DownArrow,",
    "; LeftArrow, RightArrow, Insert, Delete, Home, End, PageUp, PageDown, Backspace, Tab, Return,",
    "; Space, Escape, Pause, Print, Menu, Numlock, CapsLock, ScrollLock, LeftShift, RightShift,",
    "; LeftControl, RightControl, LeftAlt, RightAlt, LeftWindows, RightWindows. A value holding any",
    "; other key makes every game use its built-in keys for that action.",
};

std::string_view Trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
    return text.substr(begin, end - begin);
}

// The bytes as UTF-8 for a log line: well-formed sequences kept, each maximal subpart of an
// ill-formed one written as U+FFFD, which is how C# core's CodecText.Utf8Text decodes them.
std::string Utf8Text(std::string_view bytes) {
    std::string text;
    std::size_t i = 0;
    while (i < bytes.size()) {
        const auto lead = static_cast<unsigned char>(bytes[i]);
        std::size_t length = 0;
        unsigned char low = 0x80;
        unsigned char high = 0xBF;
        if (lead < 0x80) {
            length = 1;
        } else if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            length = 3;
            if (lead == 0xE0) low = 0xA0;
            else if (lead == 0xED) high = 0x9F;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            length = 4;
            if (lead == 0xF0) low = 0x90;
            else if (lead == 0xF4) high = 0x8F;
        }

        std::size_t next = i + 1;
        while (next < i + length && next < bytes.size()) {
            const auto b = static_cast<unsigned char>(bytes[next]);
            if (b < low || b > high) break;
            ++next;
            low = 0x80;
            high = 0xBF;
        }
        if (length != 0 && next == i + length) {
            text.append(bytes.substr(i, length));
        } else {
            text += "\xEF\xBF\xBD";
        }
        i = next;
    }
    return text;
}

bool IsModifier(std::string_view token) {
    for (const input::KeyModifierEntry& modifier : input::kKeyModifiers) {
        if (EqualsAsciiIgnoreCase(token, modifier.name)) return true;
    }
    return false;
}

bool IsVirtualKeyName(std::string_view token) {
    for (const input::KeyNameEntry& key : input::kKeyNames) {
        if (key.vk != 0 && EqualsAsciiIgnoreCase(token, key.name)) return true;
    }
    return false;
}

// The first item whose key is not a name with a Windows virtual-key code, or empty. An item this
// cannot split into modifiers and a key is left to the codec, which names what it expected.
std::string KeyNameError(std::string_view text) {
    std::size_t start = 0;
    for (;;) {
        const std::size_t comma = text.find(',', start);
        const std::string_view item =
            text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        const std::size_t plus = item.rfind('+');
        const std::string_view key = Trim(plus == std::string_view::npos ? item : item.substr(plus + 1));
        if (!key.empty() && !IsModifier(key) && !IsVirtualKeyName(key)) {
            return std::string(key) + " is not one of the key names this file takes";
        }
        if (comma == std::string_view::npos) return {};
        start = comma + 1;
    }
}

template <schema::Concept Id>
std::string CodecError(std::string_view text) {
    using Traits = schema::ConceptTraits<Id>;
    if constexpr (Traits::kFamily == schema::ValueFamily::kBool) {
        return BoolCodec().Parse(text).error;
    } else if constexpr (Traits::kFamily == schema::ValueFamily::kInteger) {
        return IntCodec<int>(static_cast<int>(Traits::kMin), static_cast<int>(Traits::kMax)).Parse(text).error;
    } else if constexpr (Traits::kFamily == schema::ValueFamily::kFloating) {
        return FloatCodec(Traits::kMin, Traits::kMax).Parse(text).error;
    } else {
        std::string error = KeyNameError(text);
        return error.empty() ? HotkeyCodec().Parse(text).error : error;
    }
}

template <std::size_t... I>
std::string ConceptCodecError(schema::Concept id, std::string_view text, std::index_sequence<I...>) {
    std::string error;
    ((static_cast<std::size_t>(id) == I ? (error = CodecError<static_cast<schema::Concept>(I)>(text), true) : false) ||
     ...);
    return error;
}

DefaultsIniValue ReadValue(const CanonicalIni& doc, const schema::ConceptInfo& info) {
    DefaultsIniValue read;
    const CanonicalSection* section = doc.FindSection(info.section);
    const CanonicalValue* found = section == nullptr ? nullptr : section->Find(info.key);
    if (found == nullptr) return read;
    read.line = found->line;
    read.section = section->name;
    read.key = found->key;
    read.value = found->value;
    read.reason = Utf8Text(ConceptCodecError(info.id, found->value, std::make_index_sequence<schema::kConceptCount>{}));
    read.state = read.reason.empty() ? DefaultsIniValueState::kAccepted : DefaultsIniValueState::kRefused;
    return read;
}

// An accepted value as the codec reads it, or the built-in for an absent one.
bool Flag(schema::Concept id, const DefaultsIniValue& value) {
    const std::string_view text = value.state == DefaultsIniValueState::kAccepted
                                      ? std::string_view(value.value)
                                      : std::string_view(schema::kConcepts[static_cast<std::size_t>(id)].default_text);
    const CodecParseResult<bool> read = BoolCodec().Parse(text);
    if (!read.ok()) {
        throw std::logic_error(std::string(schema::kConcepts[static_cast<std::size_t>(id)].name) + ": '" +
                               std::string(text) + "' does not read: " + read.error);
    }
    return read.value;
}

// Why the pair is refused, or empty when it names a tracking mode.
std::string PairReason(const DefaultsIniValue& rotation, const DefaultsIniValue& position) {
    std::string refused;
    for (const DefaultsIniValue* value : {&rotation, &position}) {
        if (value->state != DefaultsIniValueState::kRefused) continue;
        if (!refused.empty()) refused += "; ";
        refused += value->key + ": " + value->reason;
    }
    if (!refused.empty()) return refused + ", and the two are read together as the tracking mode";
    const bool rotation_enabled = Flag(schema::Concept::RotationEnabled, rotation);
    const bool position_enabled = Flag(schema::Concept::PositionEnabled, position);
    return DecodeTrackingMode(rotation_enabled, position_enabled) ? std::string() : "both false is not a tracking mode";
}

constexpr std::size_t kGlobalConceptCount = [] {
    std::size_t count = 0;
    for (const schema::ConceptInfo& info : schema::kConcepts) count += info.global ? 1 : 0;
    return count;
}();

constexpr std::array<schema::Concept, kGlobalConceptCount> kGlobalConcepts = [] {
    std::array<schema::Concept, kGlobalConceptCount> ids{};
    std::size_t next = 0;
    for (const schema::ConceptInfo& info : schema::kConcepts) {
        if (info.global) ids[next++] = info.id;
    }
    return ids;
}();

template <std::size_t... I>
ConfigTable<HeadTrackingConfig> GlobalConceptTable(std::index_sequence<I...>) {
    return HeadTrackingConfigTable({kGlobalConcepts[I]...});
}

std::string Setting(const DefaultsIniValue& value) {
    return "[" + value.section + "] " + value.key + "=" + Utf8Text(value.value);
}

}  // namespace

ConfigTable<HeadTrackingConfig> DefaultsIniTable() {
    return GlobalConceptTable(std::make_index_sequence<kGlobalConceptCount>{});
}

std::string RenderDefaultsIni() {
    const ConfigTable<HeadTrackingConfig> table = DefaultsIniTable();
    return RenderCanonicalValues(table, table.defaults(), std::vector<std::string>(std::begin(kHeader), std::end(kHeader)));
}

DefaultsIniSnapshot ReadDefaultsIni(std::string_view bytes) {
    DefaultsIniSnapshot snapshot;
    const CanonicalIni doc = ParseCanonicalIni(bytes);
    if (doc.status == CanonicalReadStatus::Utf16) {
        snapshot.unreadable = "it is saved as UTF-16; save it as ANSI or UTF-8";
        return snapshot;
    }
    if (doc.status == CanonicalReadStatus::NulByte) {
        snapshot.unreadable = "line " + std::to_string(doc.unreadable_line) + " holds a NUL byte";
        return snapshot;
    }

    for (const CanonicalDiagnostic& diagnostic : doc.diagnostics) {
        if (diagnostic.kind != CanonicalDiagnosticKind::ConfigFormatNewer) continue;
        snapshot.format_line = "Defaults.ini: line " + std::to_string(diagnostic.lines.front()) + ": " + diagnostic.key +
                               "=" + diagnostic.value + " was written by a newer version of the mod. This version reads " +
                               "format " + std::to_string(kConfigFormat) + ".";
    }

    for (const schema::ConceptInfo& info : schema::kConcepts) {
        if (info.global) snapshot.values[static_cast<std::size_t>(info.id)] = ReadValue(doc, info);
    }

    DefaultsIniValue& rotation = snapshot.values[static_cast<std::size_t>(schema::Concept::RotationEnabled)];
    DefaultsIniValue& position = snapshot.values[static_cast<std::size_t>(schema::Concept::PositionEnabled)];
    const std::string pair_reason = PairReason(rotation, position);
    if (!pair_reason.empty()) {
        for (DefaultsIniValue* value : {&rotation, &position}) {
            if (value->state == DefaultsIniValueState::kAbsent) continue;
            value->state = DefaultsIniValueState::kRefused;
            value->reason = pair_reason;
        }
        snapshot.pair_refused = true;
    }
    return snapshot;
}

std::string DefaultsIniRefusedLine(const DefaultsIniValue& value, std::string_view built_in) {
    if (value.state != DefaultsIniValueState::kRefused) {
        throw std::invalid_argument("only a refused value has a line, and this one is not refused");
    }
    return "Defaults.ini: line " + std::to_string(value.line) + ": " + Setting(value) + " is not read (" + value.reason +
           "), so the built-in " + std::string(built_in) + " is used.";
}

std::string DefaultsIniText(std::string_view bytes) { return Utf8Text(bytes); }

std::string DefaultsIniPairLine(const DefaultsIniSnapshot& snapshot, std::string_view rotation_built_in,
                                std::string_view position_built_in) {
    if (!snapshot.pair_refused) throw std::invalid_argument("the snapshot's tracking-mode pair is not refused");

    std::vector<const DefaultsIniValue*> held;
    for (const schema::Concept id : {schema::Concept::RotationEnabled, schema::Concept::PositionEnabled}) {
        const DefaultsIniValue& value = snapshot.Value(id);
        if (value.state != DefaultsIniValueState::kAbsent) held.push_back(&value);
    }
    if (held.size() == 2 && held[1]->line < held[0]->line) std::swap(held[0], held[1]);

    const std::string text = held.size() == 1
                                 ? "line " + std::to_string(held[0]->line) + ": " + Setting(*held[0]) + " is not read"
                                 : "lines " + std::to_string(held[0]->line) + " and " + std::to_string(held[1]->line) +
                                       ": " + Setting(*held[0]) + " and " + Setting(*held[1]) + " are not read";
    return "Defaults.ini: " + text + " (" + held[0]->reason + "), so the built-in RotationEnabled=" +
           std::string(rotation_built_in) + " and PositionEnabled=" + std::string(position_built_in) + " are used.";
}

}  // namespace cameraunlock::config::detail
