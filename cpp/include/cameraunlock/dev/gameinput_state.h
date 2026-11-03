#pragma once

// What an isolated GameInput keyboard, mouse and gamepad report for the input a
// script plays (isolated_input.h). Pure: the script's state in, the structures
// of GameInput API version 2 (the Microsoft.GameInput NuGet package's
// v2/GameInput.h) out, held here as plain integers so the tests need no GameInput
// header.

#include "cameraunlock/dev/gamepad_state.h"
#include "cameraunlock/dev/input_script.h"

#include <cstddef>
#include <cstdint>

namespace cameraunlock::dev {

// GameInputMouseButtons and GameInputMousePositions.
inline constexpr uint32_t kGameInputMouseLeft = 0x1;
inline constexpr uint32_t kGameInputMouseRight = 0x2;
inline constexpr uint32_t kGameInputMouseMiddle = 0x4;
inline constexpr uint32_t kGameInputMouseRelativePosition = 0x2;

// GameInputMouseState.
struct GameInputMouseState {
    uint32_t buttons;
    uint32_t positions;
    int64_t positionX;
    int64_t positionY;
    int64_t absolutePositionX;
    int64_t absolutePositionY;
    int64_t wheelX;
    int64_t wheelY;
};
static_assert(sizeof(GameInputMouseState) == 56, "GameInputMouseState is two 32-bit fields and six 64-bit ones");

// GameInputKeyState.
struct GameInputKeyState {
    uint32_t scanCode;
    uint32_t codePoint;
    uint8_t virtualKey;
    bool isDeadKey;
};
static_assert(sizeof(GameInputKeyState) == 12, "GameInputKeyState is two 32-bit fields, two bytes and padding");

// GameInputGamepadState.
struct GameInputGamepadState {
    uint32_t buttons;
    float leftTrigger;
    float rightTrigger;
    float leftThumbstickX;
    float leftThumbstickY;
    float rightThumbstickX;
    float rightThumbstickY;
};
static_assert(sizeof(GameInputGamepadState) == 28, "GameInputGamepadState is a 32-bit field and six floats");

// The script's mouse as GameInput reports one: the buttons held, and where the
// movement played so far has taken it. A game takes its mouse movement from the
// change in positionX and positionY between two readings.
struct GameInputMouse {
    uint32_t buttons = 0;
    int64_t x = 0;
    int64_t y = 0;
    int64_t wheel = 0;

    void Move(int dx, int dy) {
        x += dx;
        y += dy;
    }

    void Wheel(int dz) { wheel += dz; }

    void Button(MouseButton button, bool down) {
        const uint32_t bit = button == MouseButton::kLeft    ? kGameInputMouseLeft
                             : button == MouseButton::kRight ? kGameInputMouseRight
                                                             : kGameInputMouseMiddle;
        buttons = down ? (buttons | bit) : (buttons & ~bit);
    }

    GameInputMouseState State() const {
        return {buttons, kGameInputMouseRelativePosition, x, y, 0, 0, 0, wheel};
    }
};

// The keys GameInput reports held: every virtual key the script holds, apart
// from the mouse buttons (VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1 and
// VK_XBUTTON2) and the side-less Shift, Ctrl and Alt, which a script holds
// together with the left-hand key. GameInput reads keys as the physical keys.
// Writes at most `capacity` of them, lowest code first, and returns how many are
// held.
inline size_t GameInputHeldKeys(const bool (&down)[256], uint8_t* out, size_t capacity) {
    size_t count = 0;
    for (int vk = 1; vk < 256; ++vk) {
        if (!down[vk] || vk == 0x01 || vk == 0x02 || vk == 0x04 || vk == 0x05 || vk == 0x06 || vk == 0x10
            || vk == 0x11 || vk == 0x12)
            continue;
        if (count < capacity) out[count] = static_cast<uint8_t>(vk);
        ++count;
    }
    return count;
}

// A script pad (XINPUT_GAMEPAD's layout) as a GameInput gamepad.
inline GameInputGamepadState GameInputPad(const PadState& pad) {
    struct Bit { uint16_t xinput; uint32_t gameInput; };
    static constexpr Bit kButtons[] = {
        {0x0010, 0x0001}, {0x0020, 0x0002}, {0x1000, 0x0004}, {0x2000, 0x0008}, {0x4000, 0x0010},
        {0x8000, 0x0020}, {0x0001, 0x0040}, {0x0002, 0x0080}, {0x0004, 0x0100}, {0x0008, 0x0200},
        {0x0100, 0x0400}, {0x0200, 0x0800}, {0x0040, 0x1000}, {0x0080, 0x2000},
    };
    GameInputGamepadState state = {};
    for (const Bit& bit : kButtons) {
        if (pad.buttons & bit.xinput) state.buttons |= bit.gameInput;
    }
    const auto stick = [](int16_t value) {
        const float scaled = static_cast<float>(value) / 32767.0f;
        return scaled < -1.0f ? -1.0f : scaled;
    };
    state.leftTrigger = static_cast<float>(pad.leftTrigger) / 255.0f;
    state.rightTrigger = static_cast<float>(pad.rightTrigger) / 255.0f;
    state.leftThumbstickX = stick(pad.leftX);
    state.leftThumbstickY = stick(pad.leftY);
    state.rightThumbstickX = stick(pad.rightX);
    state.rightThumbstickY = stick(pad.rightY);
    return state;
}

}  // namespace cameraunlock::dev
