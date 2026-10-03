#pragma once

// The command language of the isolated-input test driver (isolated_input.h).
//
// A lab build of a mod can feed its game keyboard and mouse input from inside
// the process, so a test session needs neither the foreground nor the real
// devices. The harness writes commands to a file and the mod plays them. This
// header is the pure half: one line of text in, the steps it stands for out.
//
//   down <key>            press and hold: a key name, or Ctrl, Shift or Alt
//   up <key>              release it
//   tap <binding> [ms]    press and release, held ms (default 60). A binding
//                         may be a chord, as in a hotkey list: Ctrl+Shift+U
//   mouse <left|right|middle> <down|up|click>
//   move <dx> <dy>        relative mouse movement, in counts
//   cursor <x> <y>        put the mouse cursor at a point of the game window's
//                         client area, in pixels, for a menu that reads where
//                         the cursor is and not how the mouse moved
//   text <characters>     typed one character at a time, to the end of the line
//   wait <ms>
//
// Blank lines and lines starting with # are no steps. Key names are the ones
// hotkey lists use (data/keys.json).

#include "cameraunlock/input/key_bindings.h"

#include <charconv>
#include <string>
#include <string_view>
#include <vector>

namespace cameraunlock::dev {

enum class InputAction { kKeyDown, kKeyUp, kMouseDown, kMouseUp, kMouseMove, kCursor, kText, kWait };
enum class MouseButton { kLeft, kRight, kMiddle };

struct InputStep {
    InputAction action = InputAction::kWait;
    int vk = 0;
    MouseButton button = MouseButton::kLeft;
    // The movement of kMouseMove, or the client-area point of kCursor.
    int dx = 0;
    int dy = 0;
    unsigned waitMs = 0;
    std::string text;
};

inline constexpr unsigned kDefaultTapHoldMs = 60;
// The virtual-key codes of the three modifiers, either side.
inline constexpr int kVkShift = 0x10;
inline constexpr int kVkControl = 0x11;
inline constexpr int kVkAlt = 0x12;

namespace detail {

inline std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

// The next space-separated word of `rest`, which is advanced past it.
inline std::string_view NextWord(std::string_view& rest) {
    rest = Trim(rest);
    const size_t end = rest.find_first_of(" \t");
    const std::string_view word = rest.substr(0, end);
    rest = end == std::string_view::npos ? std::string_view() : rest.substr(end);
    return word;
}

inline bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const char x = a[i] >= 'A' && a[i] <= 'Z' ? static_cast<char>(a[i] + 32) : a[i];
        const char y = b[i] >= 'A' && b[i] <= 'Z' ? static_cast<char>(b[i] + 32) : b[i];
        if (x != y) return false;
    }
    return true;
}

template <typename T>
inline bool ParseNumber(std::string_view word, T& out) {
    if (word.empty()) return false;
    const auto result = std::from_chars(word.data(), word.data() + word.size(), out);
    return result.ec == std::errc() && result.ptr == word.data() + word.size();
}

// A key for `down` and `up`: a modifier by name, or one key with no modifiers.
inline bool ParseHeldKey(std::string_view word, int& vk, std::string& error) {
    if (EqualsIgnoreCase(word, "ctrl")) { vk = kVkControl; return true; }
    if (EqualsIgnoreCase(word, "shift")) { vk = kVkShift; return true; }
    if (EqualsIgnoreCase(word, "alt")) { vk = kVkAlt; return true; }
    const input::KeyBindingsParseResult parsed = input::ParseKeyBindings(word);
    if (!parsed.ok()) { error = parsed.error; return false; }
    if (parsed.bindings.size() != 1 || parsed.bindings[0].modifiers != input::KeyModifiers::kNone) {
        error = "expected one key with no modifiers";
        return false;
    }
    vk = parsed.bindings[0].vk;
    return true;
}

inline InputStep KeyStep(InputAction action, int vk) {
    InputStep step;
    step.action = action;
    step.vk = vk;
    return step;
}

inline InputStep WaitStep(unsigned ms) {
    InputStep step;
    step.action = InputAction::kWait;
    step.waitMs = ms;
    return step;
}

}  // namespace detail

// Appends the steps one line stands for. False, with `error` saying what was
// expected, for a line that is not a command; `out` is then left as it was.
inline bool ParseInputLine(std::string_view line, std::vector<InputStep>& out, std::string& error) {
    using namespace detail;
    std::string_view rest = Trim(line);
    if (rest.empty() || rest.front() == '#') return true;
    const std::string_view command = NextWord(rest);

    if (EqualsIgnoreCase(command, "down") || EqualsIgnoreCase(command, "up")) {
        int vk = 0;
        const std::string_view key = NextWord(rest);
        if (!Trim(rest).empty()) { error = "expected one key"; return false; }
        if (!ParseHeldKey(key, vk, error)) return false;
        out.push_back(KeyStep(EqualsIgnoreCase(command, "down") ? InputAction::kKeyDown : InputAction::kKeyUp, vk));
        return true;
    }
    if (EqualsIgnoreCase(command, "tap")) {
        const std::string_view binding = NextWord(rest);
        unsigned holdMs = kDefaultTapHoldMs;
        const std::string_view hold = NextWord(rest);
        if (!hold.empty() && !ParseNumber(hold, holdMs)) { error = "expected a hold time in milliseconds"; return false; }
        if (!Trim(rest).empty()) { error = "expected a binding and an optional hold time"; return false; }
        const input::KeyBindingsParseResult parsed = input::ParseKeyBindings(binding);
        if (!parsed.ok()) { error = parsed.error; return false; }
        if (parsed.bindings.size() != 1) { error = "expected one binding"; return false; }
        const input::KeyBinding& b = parsed.bindings[0];
        std::vector<int> modifiers;
        if (input::HasModifiers(b.modifiers, input::KeyModifiers::kCtrl)) modifiers.push_back(kVkControl);
        if (input::HasModifiers(b.modifiers, input::KeyModifiers::kShift)) modifiers.push_back(kVkShift);
        if (input::HasModifiers(b.modifiers, input::KeyModifiers::kAlt)) modifiers.push_back(kVkAlt);
        // A modifier goes down before the key it is held with and comes up after
        // it, each on a frame of its own, as a hand does it.
        for (const int vk : modifiers) {
            out.push_back(KeyStep(InputAction::kKeyDown, vk));
            out.push_back(WaitStep(kDefaultTapHoldMs));
        }
        out.push_back(KeyStep(InputAction::kKeyDown, b.vk));
        out.push_back(WaitStep(holdMs));
        out.push_back(KeyStep(InputAction::kKeyUp, b.vk));
        for (auto it = modifiers.rbegin(); it != modifiers.rend(); ++it) {
            out.push_back(WaitStep(kDefaultTapHoldMs));
            out.push_back(KeyStep(InputAction::kKeyUp, *it));
        }
        return true;
    }
    if (EqualsIgnoreCase(command, "mouse")) {
        const std::string_view which = NextWord(rest);
        const std::string_view what = NextWord(rest);
        InputStep step;
        if (EqualsIgnoreCase(which, "left")) step.button = MouseButton::kLeft;
        else if (EqualsIgnoreCase(which, "right")) step.button = MouseButton::kRight;
        else if (EqualsIgnoreCase(which, "middle")) step.button = MouseButton::kMiddle;
        else { error = "expected left, right or middle"; return false; }
        if (!Trim(rest).empty()) { error = "expected a button and down, up or click"; return false; }
        if (!EqualsIgnoreCase(what, "down") && !EqualsIgnoreCase(what, "up") && !EqualsIgnoreCase(what, "click")) {
            error = "expected down, up or click";
            return false;
        }
        if (EqualsIgnoreCase(what, "down") || EqualsIgnoreCase(what, "click")) {
            step.action = InputAction::kMouseDown;
            out.push_back(step);
        }
        if (EqualsIgnoreCase(what, "click")) out.push_back(WaitStep(kDefaultTapHoldMs));
        if (EqualsIgnoreCase(what, "up") || EqualsIgnoreCase(what, "click")) {
            step.action = InputAction::kMouseUp;
            out.push_back(step);
        }
        return true;
    }
    if (EqualsIgnoreCase(command, "move")) {
        InputStep step;
        step.action = InputAction::kMouseMove;
        const std::string_view dx = NextWord(rest);
        const std::string_view dy = NextWord(rest);
        if (!ParseNumber(dx, step.dx) || !ParseNumber(dy, step.dy) || !Trim(rest).empty()) {
            error = "expected two whole numbers";
            return false;
        }
        out.push_back(step);
        return true;
    }
    if (EqualsIgnoreCase(command, "cursor")) {
        InputStep step;
        step.action = InputAction::kCursor;
        const std::string_view x = NextWord(rest);
        const std::string_view y = NextWord(rest);
        if (!ParseNumber(x, step.dx) || !ParseNumber(y, step.dy) || step.dx < 0 || step.dy < 0 || !Trim(rest).empty()) {
            error = "expected two whole numbers, neither below zero";
            return false;
        }
        out.push_back(step);
        return true;
    }
    if (EqualsIgnoreCase(command, "text")) {
        InputStep step;
        step.action = InputAction::kText;
        step.text = std::string(Trim(rest));
        if (step.text.empty()) { error = "expected the characters to type"; return false; }
        out.push_back(step);
        return true;
    }
    if (EqualsIgnoreCase(command, "wait")) {
        unsigned ms = 0;
        const std::string_view word = NextWord(rest);
        if (!ParseNumber(word, ms) || !Trim(rest).empty()) { error = "expected a time in milliseconds"; return false; }
        out.push_back(WaitStep(ms));
        return true;
    }
    error = "unknown command '" + std::string(command) + "'";
    return false;
}

}  // namespace cameraunlock::dev
