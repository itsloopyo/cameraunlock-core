#pragma once

// What an isolated XInput controller reports for the input a script plays
// (isolated_input.h). Pure: a pad command in, the state XInputGetState hands a
// game out. The layout is XINPUT_GAMEPAD's, held here as plain integers so the
// tests need no Windows header.

#include <cstdint>
#include <string_view>

namespace cameraunlock::dev {

inline constexpr int kPadCount = 4;

// What one pad command changes.
enum class PadControl { kButton, kLeftStick, kRightStick, kLeftTrigger, kRightTrigger };

// XINPUT_GAMEPAD's fields, and the packet number that goes up with every
// change, which is how a game knows to look.
struct PadState {
    bool connected = false;
    uint32_t packet = 0;
    uint16_t buttons = 0;
    uint8_t leftTrigger = 0;
    uint8_t rightTrigger = 0;
    int16_t leftX = 0;
    int16_t leftY = 0;
    int16_t rightX = 0;
    int16_t rightY = 0;
};

namespace detail {

inline bool PadNameIs(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const char x = a[i] >= 'A' && a[i] <= 'Z' ? static_cast<char>(a[i] + 32) : a[i];
        if (x != b[i]) return false;
    }
    return true;
}

}  // namespace detail

// The XINPUT_GAMEPAD_* bit of a button by its name, or 0 for a name that is not
// a button.
inline uint16_t PadButtonBit(std::string_view name) {
    struct Named { std::string_view name; uint16_t bit; };
    static constexpr Named kButtons[] = {
        {"up", 0x0001}, {"down", 0x0002}, {"left", 0x0004}, {"right", 0x0008},
        {"start", 0x0010}, {"back", 0x0020}, {"ls", 0x0040}, {"rs", 0x0080},
        {"lb", 0x0100}, {"rb", 0x0200}, {"a", 0x1000}, {"b", 0x2000}, {"x", 0x4000}, {"y", 0x8000},
    };
    for (const Named& button : kButtons) {
        if (detail::PadNameIs(name, button.name)) return button.bit;
    }
    return 0;
}

// A stick deflection of -1 to 1 as XInput's signed 16 bits. Out of range values
// are held at the ends.
inline int16_t PadAxis(float value) {
    if (!(value > -1.0f)) return -32768;
    if (!(value < 1.0f)) return 32767;
    return static_cast<int16_t>(value * 32767.0f);
}

// A trigger pull of 0 to 1 as XInput's byte.
inline uint8_t PadTrigger(float value) {
    if (!(value > 0.0f)) return 0;
    if (!(value < 1.0f)) return 255;
    return static_cast<uint8_t>(value * 255.0f);
}

// One pad command applied. The pad is connected from its first command on.
inline void ApplyPadStep(PadState& pad, PadControl control, uint16_t button, bool down, float x, float y) {
    pad.connected = true;
    ++pad.packet;
    switch (control) {
        case PadControl::kButton:
            pad.buttons = down ? static_cast<uint16_t>(pad.buttons | button)
                               : static_cast<uint16_t>(pad.buttons & ~button);
            break;
        case PadControl::kLeftStick:    pad.leftX = PadAxis(x); pad.leftY = PadAxis(y); break;
        case PadControl::kRightStick:   pad.rightX = PadAxis(x); pad.rightY = PadAxis(y); break;
        case PadControl::kLeftTrigger:  pad.leftTrigger = PadTrigger(x); break;
        case PadControl::kRightTrigger: pad.rightTrigger = PadTrigger(x); break;
    }
}

}  // namespace cameraunlock::dev
