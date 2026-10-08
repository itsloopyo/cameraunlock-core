#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cameraunlock/input/hotkey_poller.h>
#include <cameraunlock/input/key_bindings.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cameraunlock::input {

namespace detail {

// The modifiers held now, either side, read the way IsChordHeld reads them.
inline KeyModifiers HeldModifiers() {
    KeyModifiers held = KeyModifiers::kNone;
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) held = held | KeyModifiers::kCtrl;
    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) held = held | KeyModifiers::kShift;
    if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0) held = held | KeyModifiers::kAlt;
    return held;
}

// Whether a binding whose key just went down fires. One with modifiers fires while every
// modifier it names is held. One without follows NavGuarded: it does not fire while Ctrl
// and Shift are both held, so Ctrl+Shift+<key> reaches only a binding that names the chord.
inline bool BindingFires(KeyModifiers binding, KeyModifiers held) {
    if (binding == KeyModifiers::kNone) return !HasModifiers(held, KeyModifiers::kCtrl | KeyModifiers::kShift);
    return HasModifiers(held, binding);
}

// One callback for every binding on one key: it runs `action` once when any of them fires,
// so a list whose items share a key (End, Ctrl+End) counts one press once.
inline std::function<void()> GuardKey(std::vector<KeyModifiers> bindings, std::function<void()> action,
                                      KeyModifiers (*held)()) {
    return [bindings = std::move(bindings), action = std::move(action), held]() {
        const KeyModifiers now = held();
        for (const KeyModifiers binding : bindings) {
            if (BindingFires(binding, now)) {
                action();
                return;
            }
        }
    };
}

// A list's bindings by key: each distinct key once, in the order it first appears, with the
// modifiers of every binding on it. Throws std::invalid_argument for a code outside 0x01-0xFE
// or a modifier value outside KeyModifiers.
struct KeyGroup {
    int vk;
    std::vector<KeyModifiers> modifiers;
};

inline std::vector<KeyGroup> GroupByKey(const std::vector<KeyBinding>& bindings) {
    for (const KeyBinding& binding : bindings) {
        if (binding.vk < 0x01 || binding.vk > 0xFE) {
            throw std::invalid_argument("virtual-key code " + std::to_string(binding.vk) + " is outside 0x01-0xFE");
        }
        const auto modifiers = static_cast<unsigned>(binding.modifiers);
        if ((modifiers & ~static_cast<unsigned>(KeyModifiers::kCtrl | KeyModifiers::kShift | KeyModifiers::kAlt)) != 0) {
            throw std::invalid_argument("modifier value " + std::to_string(modifiers) + " is not a set of KeyModifiers");
        }
    }

    std::vector<KeyGroup> groups;
    for (const KeyBinding& binding : bindings) {
        std::size_t i = 0;
        while (i < groups.size() && groups[i].vk != binding.vk) ++i;
        if (i == groups.size()) groups.push_back({binding.vk, {}});
        groups[i].modifiers.push_back(binding.modifiers);
    }
    return groups;
}

// RegisterHoldKeyBindings with the modifier read handed in, for a test.
inline std::vector<int> RegisterHoldKeyBindings(HotkeyPoller& poller, const std::vector<KeyBinding>& bindings,
                                                int holdMs, std::function<void()> onTap, std::function<void()> onHold,
                                                KeyModifiers (*held)()) {
    if (!onTap || !onHold) throw std::invalid_argument("RegisterHoldKeyBindings needs an action for a tap and one for a hold");
    if (holdMs < 0) throw std::invalid_argument("hold time " + std::to_string(holdMs) + " ms is negative");

    std::vector<int> ids;
    for (KeyGroup& group : GroupByKey(bindings)) {
        // Whether the press now down is one a binding on this key fires for. All three callbacks
        // run on the poller's thread, the first before either of the others.
        const auto fires = std::make_shared<bool>(false);
        ids.push_back(poller.AddHoldHotkey(
            group.vk, holdMs,
            [fires, onTap] {
                if (*fires) onTap();
            },
            [fires, onHold] {
                if (*fires) onHold();
            },
            [fires, modifiers = std::move(group.modifiers), held] {
                const KeyModifiers now = held();
                *fires = false;
                for (const KeyModifiers binding : modifiers) {
                    if (BindingFires(binding, now)) *fires = true;
                }
            }));
    }
    return ids;
}

}  // namespace detail

/// Puts a hotkey list on the poller: one AddHotkey per distinct key, running `action` once
/// when that key goes down and detail::BindingFires allows any binding on it. Returns one id
/// per distinct key, in the order each key first appears, for RemoveHotkey. An empty list
/// registers nothing.
///
/// Throws std::invalid_argument for an empty action, a code outside 0x01-0xFE or a
/// modifier value outside KeyModifiers, before registering anything.
inline std::vector<int> RegisterKeyBindings(HotkeyPoller& poller, const std::vector<KeyBinding>& bindings,
                                            std::function<void()> action) {
    if (!action) throw std::invalid_argument("RegisterKeyBindings needs an action");

    std::vector<int> ids;
    for (detail::KeyGroup& group : detail::GroupByKey(bindings)) {
        ids.push_back(poller.AddHotkey(group.vk, detail::GuardKey(std::move(group.modifiers), action,
                                                                  &detail::HeldModifiers)));
    }
    return ids;
}

/// Puts a hotkey list on the poller whose keys do one thing tapped and another held: one
/// AddHoldHotkey per distinct key. `onTap` runs once when the key is let go less than `holdMs`
/// after it went down, and `onHold` once when it has been down that long. detail::BindingFires is
/// asked as the key goes down and its answer holds for that press, so letting go of Ctrl before
/// the key does not turn a chord's tap into nothing, or into a bare key's. Returns one id per
/// distinct key, as RegisterKeyBindings does.
///
/// Throws std::invalid_argument for an empty action, a negative hold time, a code outside
/// 0x01-0xFE or a modifier value outside KeyModifiers, before registering anything.
inline std::vector<int> RegisterHoldKeyBindings(HotkeyPoller& poller, const std::vector<KeyBinding>& bindings,
                                                int holdMs, std::function<void()> onTap, std::function<void()> onHold) {
    return detail::RegisterHoldKeyBindings(poller, bindings, holdMs, std::move(onTap), std::move(onHold),
                                           &detail::HeldModifiers);
}

}  // namespace cameraunlock::input
