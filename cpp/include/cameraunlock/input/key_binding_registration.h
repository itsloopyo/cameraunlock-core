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
    for (const KeyBinding& binding : bindings) {
        if (binding.vk < 0x01 || binding.vk > 0xFE) {
            throw std::invalid_argument("virtual-key code " + std::to_string(binding.vk) + " is outside 0x01-0xFE");
        }
        const auto modifiers = static_cast<unsigned>(binding.modifiers);
        if ((modifiers & ~static_cast<unsigned>(KeyModifiers::kCtrl | KeyModifiers::kShift | KeyModifiers::kAlt)) != 0) {
            throw std::invalid_argument("modifier value " + std::to_string(modifiers) + " is not a set of KeyModifiers");
        }
    }

    std::vector<int> keys;
    std::vector<std::vector<KeyModifiers>> modifiers;
    for (const KeyBinding& binding : bindings) {
        std::size_t i = 0;
        while (i < keys.size() && keys[i] != binding.vk) ++i;
        if (i == keys.size()) {
            keys.push_back(binding.vk);
            modifiers.emplace_back();
        }
        modifiers[i].push_back(binding.modifiers);
    }

    std::vector<int> ids;
    ids.reserve(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i) {
        ids.push_back(poller.AddHotkey(keys[i], detail::GuardKey(std::move(modifiers[i]), action,
                                                                 &detail::HeldModifiers)));
    }
    return ids;
}

}  // namespace cameraunlock::input
