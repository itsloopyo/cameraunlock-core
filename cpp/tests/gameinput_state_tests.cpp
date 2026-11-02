// Tests for what an isolated GameInput keyboard, mouse and gamepad report
// (cameraunlock/dev/gameinput_state.h). A game takes its mouse movement from
// the change in position between two readings, so a move counted twice turns
// the view twice as far, and a key reported under its side-less code as well
// as its left-hand one is two keys.

#include <cameraunlock/dev/gameinput_state.h>

#include <iostream>

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << name << "\n";
    if (!cond) ++g_failures;
}

using cameraunlock::dev::GameInputGamepadState;
using cameraunlock::dev::GameInputHeldKeys;
using cameraunlock::dev::GameInputMouse;
using cameraunlock::dev::GameInputMouseState;
using cameraunlock::dev::GameInputPad;
using cameraunlock::dev::MouseButton;
using cameraunlock::dev::PadState;

}  // namespace

int RunGameInputStateTests() {
    std::cout << "\n=== GameInput state ===\n";

    {
        GameInputMouse mouse;
        const GameInputMouseState still = mouse.State();
        Check(still.buttons == 0 && still.positionX == 0 && still.positionY == 0,
              "a mouse the script has not touched holds no button and has not moved");
        Check(still.positions == cameraunlock::dev::kGameInputMouseRelativePosition && still.absolutePositionX == 0
                  && still.absolutePositionY == 0 && still.wheelX == 0 && still.wheelY == 0,
              "the mouse reports relative movement only, and no wheel");
        mouse.Move(300, -20);
        mouse.Move(-100, 5);
        const GameInputMouseState moved = mouse.State();
        Check(moved.positionX == 200 && moved.positionY == -15, "moves add up, so each is reported once");
        Check(mouse.State().positionX == 200, "reading the state again does not move the mouse");
    }

    {
        GameInputMouse mouse;
        mouse.Button(MouseButton::kRight, true);
        Check(mouse.State().buttons == cameraunlock::dev::kGameInputMouseRight, "the right button is held");
        mouse.Button(MouseButton::kLeft, true);
        mouse.Button(MouseButton::kMiddle, true);
        Check(mouse.State().buttons == 0x7, "the three buttons are held together");
        mouse.Button(MouseButton::kRight, false);
        Check(mouse.State().buttons == (cameraunlock::dev::kGameInputMouseLeft | cameraunlock::dev::kGameInputMouseMiddle),
              "letting one go keeps the others");
    }

    {
        bool down[256] = {};
        uint8_t keys[8] = {};
        Check(GameInputHeldKeys(down, keys, 8) == 0, "no key held reports none");

        down['W'] = true;
        down[0x10] = true;  // VK_SHIFT
        down[0xA0] = true;  // VK_LSHIFT
        down[0x02] = true;  // VK_RBUTTON
        const size_t count = GameInputHeldKeys(down, keys, 8);
        Check(count == 2 && keys[0] == 'W' && keys[1] == 0xA0,
              "a held Shift is its left-hand key, and a held mouse button is not a key");

        down[0x11] = true;
        down[0xA2] = true;
        down[0x12] = true;
        down[0xA4] = true;
        down[0x04] = true;
        down[0x05] = true;
        down[0x06] = true;
        down[0x01] = true;
        Check(GameInputHeldKeys(down, keys, 8) == 4, "Ctrl and Alt are their left-hand keys, and no mouse button counts");

        uint8_t one[1] = {};
        Check(GameInputHeldKeys(down, one, 1) == 4 && one[0] == 'W',
              "a short array gets the lowest codes and the count is still every key held");
        Check(GameInputHeldKeys(down, nullptr, 0) == 4, "the count alone needs no array");
    }

    {
        PadState pad;
        const GameInputGamepadState idle = GameInputPad(pad);
        Check(idle.buttons == 0 && idle.leftTrigger == 0.0f && idle.leftThumbstickX == 0.0f && idle.rightThumbstickY == 0.0f,
              "a pad at rest holds nothing");

        pad.buttons = 0x0010 | 0x0020 | 0x1000 | 0x2000 | 0x4000 | 0x8000;
        Check(GameInputPad(pad).buttons == (0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0010 | 0x0020),
              "start, back, a, b, x and y are menu, view, a, b, x and y");
        pad.buttons = 0x0001 | 0x0002 | 0x0004 | 0x0008;
        Check(GameInputPad(pad).buttons == (0x0040 | 0x0080 | 0x0100 | 0x0200), "the d-pad keeps its directions");
        pad.buttons = 0x0100 | 0x0200 | 0x0040 | 0x0080;
        Check(GameInputPad(pad).buttons == (0x0400 | 0x0800 | 0x1000 | 0x2000),
              "the shoulders and the stick presses keep their sides");

        pad.buttons = 0;
        pad.leftTrigger = 255;
        pad.rightTrigger = 0;
        pad.leftX = 32767;
        pad.leftY = -32768;
        pad.rightX = 0;
        pad.rightY = 16384;
        const GameInputGamepadState held = GameInputPad(pad);
        Check(held.leftTrigger == 1.0f && held.rightTrigger == 0.0f, "a trigger pulled all the way is 1");
        Check(held.leftThumbstickX == 1.0f && held.leftThumbstickY == -1.0f,
              "a stick at either end is 1 or -1, never past it");
        Check(held.rightThumbstickY > 0.49f && held.rightThumbstickY < 0.51f && held.rightThumbstickX == 0.0f,
              "a stick half way up is a half, up positive");
    }

    return g_failures;
}
