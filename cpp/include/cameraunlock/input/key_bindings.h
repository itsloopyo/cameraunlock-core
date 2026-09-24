#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace cameraunlock::input {

/// The modifier keys a binding can name. The numbers match
/// CameraUnlock.Core.Input.KeyModifiers.
enum class KeyModifiers : unsigned {
    kNone = 0,
    kCtrl = 1,
    kShift = 2,
    kAlt = 4,
};

constexpr KeyModifiers operator|(KeyModifiers a, KeyModifiers b) {
    return static_cast<KeyModifiers>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

constexpr KeyModifiers operator&(KeyModifiers a, KeyModifiers b) {
    return static_cast<KeyModifiers>(static_cast<unsigned>(a) & static_cast<unsigned>(b));
}

/// True when `set` holds every modifier in `wanted`.
constexpr bool HasModifiers(KeyModifiers set, KeyModifiers wanted) {
    return (set & wanted) == wanted;
}

/// One binding of a hotkey list: a Windows virtual-key code, 0x01 to 0xFE, and the
/// modifiers held with it.
struct KeyBinding {
    KeyModifiers modifiers = KeyModifiers::kNone;
    int vk = 0;

    friend bool operator==(const KeyBinding& a, const KeyBinding& b) {
        return a.modifiers == b.modifiers && a.vk == b.vk;
    }
    friend bool operator!=(const KeyBinding& a, const KeyBinding& b) { return !(a == b); }
};

/// What ParseKeyBindings read. On failure `error` says what was expected and `bindings`
/// is empty.
struct KeyBindingsParseResult {
    std::vector<KeyBinding> bindings;
    std::string error;

    bool ok() const { return error.empty(); }
};

/// Reads a hotkey value as native mods write it: `End`, `End, Ctrl+Shift+Y`, or empty for
/// unbound. Pure, and never throws for any input.
///
/// A value that is empty after trimming spaces and tabs is an empty list. Otherwise it is
/// split at ',' into items, each trimmed and non-empty. An item is split at '+' into
/// trimmed tokens: any of Ctrl, Shift and Alt, each at most once and in any order, then
/// exactly one key. A key is a name from data/keys.json that has a virtual-key code, or
/// `0x` / `0X` and one or two hex digits from 0x01 to 0xFE, so every code a legacy file
/// can hold is expressible. Names and modifiers read ASCII case-insensitively. A list
/// naming the same binding twice is invalid.
KeyBindingsParseResult ParseKeyBindings(std::string_view text);

/// The canonical text of a hotkey list, as ParseKeyBindings reads it back: modifiers as
/// `Ctrl+Shift+Alt+` in that order, the key's name from data/keys.json or, for a code
/// with no name, `0x` and upper-case hex without padding, items joined by ", ". An empty
/// list is "".
///
/// Throws std::invalid_argument for a code outside 0x01-0xFE, a modifier value outside
/// KeyModifiers, or a binding listed twice, none of which reads back.
std::string FormatKeyBindings(const std::vector<KeyBinding>& bindings);

/// One key as FormatKeyBindings writes it: `End`, `F9`, `0xBA`. Throws
/// std::invalid_argument for a code outside 0x01-0xFE.
std::string FormatVirtualKey(int vk);

}  // namespace cameraunlock::input
