#pragma once

#include <cameraunlock/config/config_concepts.g.h>
#include <cameraunlock/config/config_table.h>
#include <cameraunlock/config/head_tracking_config.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

// Defaults.ini, the file a global concept row that is not PerGame takes its default from: core's
// table of every global concept, the bytes a new file holds, and the reader. Internal to core, for the
// config owner. CameraUnlock.Core.Config.DefaultsIni is the C# twin, and
// data/fixtures/canonical-ini/global holds both to the same bytes and lines.

namespace cameraunlock::config::detail {

// What Defaults.ini holds for one canonical concept: no line for its section and key, a value that
// passed every check, or one that failed a check, so every game uses its built-in value instead.
enum class DefaultsIniValueState { kAbsent, kAccepted, kRefused };

// For a line that was read, the section, key and value are the file's own bytes and the line is
// 1-based; for an absent concept they are empty and the line is 0. `reason` is empty unless the
// value is refused. The reason and the lines are UTF-8 whatever the file holds: each maximal
// subpart of an ill-formed sequence in the value is written as U+FFFD, as C# decodes it.
struct DefaultsIniValue {
    DefaultsIniValueState state = DefaultsIniValueState::kAbsent;
    int line = 0;
    std::string section;
    std::string key;
    std::string value;
    std::string reason;
};

struct DefaultsIniSnapshot {
    // Why nothing was read, in the owner's words for an unreadable file (`it is saved as UTF-16;
    // save it as ANSI or UTF-8`, `line 3 holds a NUL byte`), or nullopt when the file was read. An
    // unreadable file holds every concept absent.
    std::optional<std::string> unreadable;
    // The line naming a ConfigFormat newer than this build's, or nullopt.
    std::optional<std::string> format_line;
    // Indexed by schema::Concept.
    std::array<DefaultsIniValue, schema::kConceptCount> values;
    // True when RotationEnabled and PositionEnabled were refused together, so both are refused, or
    // absent, and DefaultsIniPairLine gives their one line.
    bool pair_refused = false;

    const DefaultsIniValue& Value(schema::Concept id) const { return values[static_cast<std::size_t>(id)]; }
};

// Core's global table: HeadTrackingConfigTable naming every global concept, whose defaults are the
// built-in values a new Defaults.ini holds. CollisionMargin and CollisionChannel are not global, so
// it has no row for them.
ConfigTable<HeadTrackingConfig> DefaultsIniTable();

// The bytes a new Defaults.ini holds: the global table's defaults, the four hotkey lists and
// CollisionEnabled at their canonical_default, every row written as its value, under Defaults.ini's
// own header.
std::string RenderDefaultsIni();

// Reads Defaults.ini's bytes. Pure, and never throws for any input.
//
// A file saved as UTF-16 or holding a NUL is unreadable. Any other file is read by the canonical
// reader, whatever its stamp: no [CameraUnlock], or a ConfigFormat missing, zero or not a number,
// draws nothing, and a newer ConfigFormat draws `format_line`. Each global concept is found by its
// section and key, ASCII case-insensitively, the last occurrence winning; an alias, a key in another
// section, a key no concept has and the key of a concept that is not global are not read and draw
// nothing, and such a concept is absent. A value is refused when
// the concept's codec with the schema's range does not read it, `default` included, and a hotkey
// list also when an item's key is not one of the names with a Windows virtual-key code, which every
// mod reads. Then the tracking-mode pair: each of RotationEnabled and PositionEnabled is its
// accepted value, or the built-in when absent; when either is refused, or the two name no tracking
// mode, both are refused together.
DefaultsIniSnapshot ReadDefaultsIni(std::string_view bytes);

// The log line for a refused value that a game would take from Defaults.ini, e.g.
// `Defaults.ini: line 12: [Hotkeys] ToggleKey=Mouse4 is not read (Mouse4 is not one of the key
// names this file takes), so the built-in End, Ctrl+Shift+Y is used.` `built_in` is the game's
// built-in value for the row, as its codec writes it. Throws std::invalid_argument for a value that
// is not refused.
std::string DefaultsIniRefusedLine(const DefaultsIniValue& value, std::string_view built_in);

// The one log line for a refused tracking-mode pair, naming each of the two lines the file holds,
// e.g. `Defaults.ini: lines 2 and 4: [General] RotationEnabled=false and [Position]
// PositionEnabled=false are not read (both false is not a tracking mode), so the built-in
// RotationEnabled=true and PositionEnabled=true are used.` Throws std::invalid_argument when the
// snapshot's pair is not refused.
std::string DefaultsIniPairLine(const DefaultsIniSnapshot& snapshot, std::string_view rotation_built_in,
                                std::string_view position_built_in);

// A value's bytes as the lines write them: well-formed UTF-8 kept, each maximal subpart of an
// ill-formed sequence written as U+FFFD, as C# core's CodecText.Utf8Text decodes them.
std::string DefaultsIniText(std::string_view bytes);

}  // namespace cameraunlock::config::detail
