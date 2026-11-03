// Tests for what an isolated DirectInput keyboard and mouse report
// (cameraunlock/dev/directinput_state.h). A game reads these blind: a key at
// the wrong code is another key, mouse movement reported twice turns the view
// twice as far, and an event handed out twice is a key pressed twice.

#include <cameraunlock/dev/directinput_state.h>

#include <cstring>
#include <iostream>

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << name << "\n";
    if (!cond) ++g_failures;
}

using cameraunlock::dev::DirectInputEvent;
using cameraunlock::dev::DirectInputFormat;
using cameraunlock::dev::DirectInputGuid;
using cameraunlock::dev::DirectInputKeyStroke;
using cameraunlock::dev::DirectInputKind;
using cameraunlock::dev::DirectInputReader;
using cameraunlock::dev::DirectInputState;
using cameraunlock::dev::kMouseOffsetZ;
using cameraunlock::dev::MouseButton;
using cameraunlock::dev::UsKeyStroke;
using cameraunlock::dev::VkToDik;

bool Types(char character, int dik, bool shift) {
    const DirectInputKeyStroke stroke = UsKeyStroke(character);
    return stroke.dik == dik && stroke.shift == shift;
}

int32_t Axis(const uint8_t* state, size_t offset) {
    int32_t value = 0;
    std::memcpy(&value, state + offset, sizeof(value));
    return value;
}

// c_dfDIKeyboard as DirectInput reports it back: each key at its DIK code.
DirectInputFormat StandardKeyboard() {
    DirectInputFormat format;
    format.dataSize = 256;
    for (uint32_t dik = 1; dik < 256; ++dik) format.Add(DirectInputKind::kKeyboard, dik, DirectInputGuid::kKey, dik);
    return format;
}

// c_dfDIMouse (16 bytes, four buttons) or c_dfDIMouse2 (20 bytes, eight).
DirectInputFormat StandardMouse(size_t size) {
    DirectInputFormat format;
    format.dataSize = size;
    format.Add(DirectInputKind::kMouse, 0, DirectInputGuid::kXAxis, 0);
    format.Add(DirectInputKind::kMouse, 4, DirectInputGuid::kYAxis, 1);
    format.Add(DirectInputKind::kMouse, 8, DirectInputGuid::kZAxis, 2);
    for (uint32_t button = 0; button < size - 12; ++button) {
        format.Add(DirectInputKind::kMouse, 12 + button, DirectInputGuid::kButton, 3 + button);
    }
    return format;
}

}  // namespace

int RunDirectInputStateTests() {
    std::cout << "DirectInput state:\n";

    Check(VkToDik('W') == 0x11 && VkToDik('A') == 0x1E && VkToDik('Z') == 0x2C && VkToDik('M') == 0x32,
          "letters map to their DIK codes");
    Check(VkToDik('1') == 0x02 && VkToDik('9') == 0x0A && VkToDik('0') == 0x0B, "the digit row maps, 0 after 9");
    Check(VkToDik(0x1B) == 0x01 && VkToDik(0x0D) == 0x1C && VkToDik(0x20) == 0x39 && VkToDik(0x09) == 0x0F,
          "Escape, Return, Space and Tab map");
    Check(VkToDik(0x10) == 0x2A && VkToDik(0x11) == 0x1D && VkToDik(0x12) == 0x38,
          "Shift, Ctrl and Alt are the left-hand keys");
    Check(VkToDik(0xA1) == 0x36 && VkToDik(0xA3) == 0x9D && VkToDik(0xA5) == 0xB8, "the right-hand modifiers map");
    Check(VkToDik(0x23) == 0xCF && VkToDik(0x21) == 0xC9 && VkToDik(0x22) == 0xD1 && VkToDik(0x2D) == 0xD2
              && VkToDik(0x24) == 0xC7 && VkToDik(0x2E) == 0xD3,
          "the navigation keys the hotkeys use map to the extended codes");
    Check(VkToDik(0x25) == 0xCB && VkToDik(0x26) == 0xC8 && VkToDik(0x27) == 0xCD && VkToDik(0x28) == 0xD0,
          "the arrow keys map");
    Check(VkToDik(0x70) == 0x3B && VkToDik(0x79) == 0x44 && VkToDik(0x7A) == 0x57 && VkToDik(0x7B) == 0x58,
          "the function keys map, F11 and F12 out of line");
    Check(VkToDik(0x60) == 0x52 && VkToDik(0x67) == 0x47 && VkToDik(0x69) == 0x49, "the numpad digits map");
    Check(VkToDik(0xC0) == 0x29, "the backquote, a console key, maps");
    Check(VkToDik(0x01) == 0 && VkToDik(0xFF) == 0 && VkToDik(-1) == 0 && VkToDik(300) == 0,
          "a virtual key with no DirectInput code is 0");

    Check(Types('a', 0x1E, false) && Types('A', 0x1E, true), "a letter is its key, with Shift for upper case");
    Check(Types('7', 0x08, false) && Types('&', 0x08, true), "a digit is its key, with Shift for its symbol");
    Check(Types(' ', 0x39, false) && Types('.', 0x34, false) && Types('-', 0x0C, false) && Types('_', 0x0C, true),
          "space and punctuation type");
    Check(Types('"', 0x28, true) && Types('\'', 0x28, false) && Types('~', 0x29, true) && Types('?', 0x35, true),
          "shifted punctuation holds Shift");
    Check(UsKeyStroke('\t').dik == 0 && UsKeyStroke(static_cast<char>(0xE9)).dik == 0,
          "a character a US keyboard has no key for is no key");

    {
        DirectInputState state;
        DirectInputReader keyboard = state.Open(DirectInputKind::kKeyboard);
        const DirectInputFormat format = StandardKeyboard();
        uint8_t keys[256];
        std::memset(keys, 0xFF, sizeof(keys));
        Check(state.FormattedState(keyboard, format, keys, sizeof(keys)), "the standard keyboard format is answered");
        bool allUp = true;
        for (const uint8_t key : keys) allUp = allUp && key == 0;
        Check(allUp, "no key is down before anything is played");

        Check(state.Key(0x11, true, 100) && state.Key(0x2A, true, 100), "a key press is taken");
        state.FormattedState(keyboard, format, keys, sizeof(keys));
        Check(keys[0x11] == 0x80 && keys[0x2A] == 0x80 && keys[0x1E] == 0, "held keys read 0x80 at their code");
        state.Key(0x11, false, 120);
        state.FormattedState(keyboard, format, keys, sizeof(keys));
        Check(keys[0x11] == 0 && keys[0x2A] == 0x80, "a released key reads 0 and the others stay held");
        Check(!state.Key(0, true, 130) && !state.Key(256, true, 130), "a code that is no key is refused");
    }

    {
        DirectInputState state;
        DirectInputReader mouse = state.Open(DirectInputKind::kMouse);
        const DirectInputFormat four = StandardMouse(16);
        const DirectInputFormat eight = StandardMouse(20);
        uint8_t small[16];
        uint8_t large[20];

        state.Move(-260, 10, 5);
        state.Move(60, 5, 6);
        state.Button(MouseButton::kRight, true, 7);
        Check(state.FormattedState(mouse, four, small, sizeof(small)) && Axis(small, 0) == -200 && Axis(small, 4) == 15
                  && Axis(small, 8) == 0,
              "mouse state is the movement since the last read");
        Check(small[12] == 0 && small[13] == 0x80 && small[14] == 0 && small[15] == 0,
              "a held button reads 0x80 in its slot: left, right, middle");
        Check(state.FormattedState(mouse, four, small, sizeof(small)) && Axis(small, 0) == 0 && Axis(small, 4) == 0,
              "movement is reported once");
        Check(small[13] == 0x80, "a button stays held between reads");

        state.Move(3, 0, 8);
        std::memset(large, 0xFF, sizeof(large));
        Check(state.FormattedState(mouse, eight, large, sizeof(large)) && Axis(large, 0) == 3 && large[13] == 0x80
                  && large[16] == 0 && large[19] == 0,
              "the eight-button state is answered too");

        uint8_t wrong[24] = {};
        state.Move(9, 9, 9);
        Check(!state.FormattedState(mouse, four, wrong, sizeof(wrong)), "a size that is not the format's is refused");
        Check(state.FormattedState(mouse, four, small, sizeof(small)) && Axis(small, 0) == 9,
              "a refused read spends no movement");

        DirectInputReader second = state.Open(DirectInputKind::kMouse);
        state.Move(4, 0, 10);
        Check(state.FormattedState(second, four, small, sizeof(small)) && Axis(small, 0) == 4
                  && state.FormattedState(mouse, four, small, sizeof(small)) && Axis(small, 0) == 4,
              "each device has the movement once, and a new device none from before it");

        DirectInputReader buffered = state.Open(DirectInputKind::kMouse);
        state.Wheel(-240, 11);
        state.Wheel(120, 12);
        Check(state.FormattedState(mouse, four, small, sizeof(small)) && Axis(small, 8) == -120 && Axis(small, 0) == 0,
              "the wheel reads its turn since the last read");
        Check(state.FormattedState(mouse, four, small, sizeof(small)) && Axis(small, 8) == 0, "a turn is reported once");
        DirectInputReader late = state.Open(DirectInputKind::kMouse);
        Check(state.FormattedState(late, four, small, sizeof(small)) && Axis(small, 8) == 0,
              "a new device is owed no turn from before it");
        DirectInputEvent turns[4];
        bool lost = false;
        Check(state.ReadEvents(buffered, turns, 4, false, lost) == 2 && turns[0].offset == kMouseOffsetZ
                  && static_cast<int32_t>(turns[0].data) == -240 && turns[1].sequence != turns[0].sequence,
              "each turn is a buffered wheel event of its own");
    }

    {
        // F.E.A.R.'s layout: every object the device lists, four bytes apart, so its keyboard is
        // 126 keys in 504 bytes and its mouse X, Y, wheel and eight buttons in 44.
        DirectInputFormat keyboardFormat;
        keyboardFormat.dataSize = 504;
        const uint32_t listed[] = {0x01, 0x11, 0x2D, 0x21, 0x32};
        for (uint32_t i = 0; i < 126; ++i) {
            const uint32_t dik = i < 5 ? listed[i] : 0x80 + i;
            keyboardFormat.Add(DirectInputKind::kKeyboard, 4 * i, DirectInputGuid::kKey, dik);
        }
        DirectInputFormat mouseFormat;
        mouseFormat.dataSize = 44;
        Check(mouseFormat.Add(DirectInputKind::kMouse, 0, DirectInputGuid::kXAxis, 0)
                  && mouseFormat.Add(DirectInputKind::kMouse, 4, DirectInputGuid::kYAxis, 1)
                  && mouseFormat.Add(DirectInputKind::kMouse, 8, DirectInputGuid::kZAxis, 2),
              "a mouse's axes take their place in a format");
        for (uint32_t button = 0; button < 8; ++button) {
            mouseFormat.Add(DirectInputKind::kMouse, 12 + 4 * button, DirectInputGuid::kButton, 3 + button);
        }

        DirectInputState state;
        DirectInputReader keyboard = state.Open(DirectInputKind::kKeyboard);
        DirectInputReader mouse = state.Open(DirectInputKind::kMouse);
        uint8_t keys[504];
        uint8_t buttons[44];
        state.Key(0x2D, true, 10);
        state.Key(0x1E, true, 11);
        Check(state.FormattedState(keyboard, keyboardFormat, keys, sizeof(keys)) && keys[8] == 0x80 && keys[4] == 0
                  && keys[9] == 0 && keys[0] == 0,
              "a custom keyboard format reads a held key at its own offset, and nothing else");
        bool onlyX = true;
        for (size_t i = 0; i < sizeof(keys); ++i) onlyX = onlyX && (i == 8 || keys[i] == 0);
        Check(onlyX, "a held key the format leaves out shows nowhere");

        state.Move(7, -3, 12);
        state.Button(MouseButton::kLeft, true, 13);
        state.Button(MouseButton::kMiddle, true, 14);
        Check(state.FormattedState(mouse, mouseFormat, buttons, sizeof(buttons)) && Axis(buttons, 0) == 7
                  && Axis(buttons, 4) == -3 && Axis(buttons, 8) == 0 && buttons[12] == 0x80 && buttons[16] == 0
                  && buttons[20] == 0x80 && buttons[24] == 0,
              "a custom mouse format reads axes and buttons at its own offsets");
        Check(!state.FormattedState(keyboard, keyboardFormat, keys, 256), "a read of the standard size is refused");

        DirectInputEvent events[8];
        bool overflowed = false;
        state.Key(0x2D, false, 20);
        size_t count = state.ReadEvents(keyboard, events, 8, false, overflowed, &keyboardFormat);
        Check(count == 2 && events[0].offset == 8 && events[0].data == 0x80 && events[1].offset == 8 && events[1].data == 0,
              "buffered key events carry the key's offset in the format");
        count = state.ReadEvents(mouse, events, 8, false, overflowed, &mouseFormat);
        Check(count == 4 && events[0].offset == 0 && events[1].offset == 4 && events[2].offset == 12
                  && events[3].offset == 20,
              "buffered mouse events carry the axis or button offset in the format");

        state.Key(0x1E, false, 30);
        state.Key(0x11, true, 31);
        Check(state.ReadEvents(keyboard, events, 1, false, overflowed, &keyboardFormat) == 1 && events[0].offset == 4
                  && state.ReadEvents(keyboard, events, 8, false, overflowed, &keyboardFormat) == 0,
              "an event of a key the format leaves out is passed over, and does not take a place in the read");
    }

    {
        DirectInputFormat format;
        Check(!format.Add(DirectInputKind::kKeyboard, 0, DirectInputGuid::kButton, 3)
                  && !format.Add(DirectInputKind::kKeyboard, 0, DirectInputGuid::kKey, 0)
                  && !format.Add(DirectInputKind::kMouse, 0, DirectInputGuid::kKey, 0x11)
                  && !format.Add(DirectInputKind::kMouse, 0, DirectInputGuid::kButton, 11)
                  && !format.Add(DirectInputKind::kMouse, 0, DirectInputGuid::kOther, 0) && format.entries.empty(),
              "an object a keyboard or mouse state has no place for is left out");

        // A format set before isolated input could see it is read back with no size.
        DirectInputFormat readBack;
        readBack.Add(DirectInputKind::kKeyboard, 40, DirectInputGuid::kKey, 0x11);
        DirectInputState state;
        DirectInputReader keyboard = state.Open(DirectInputKind::kKeyboard);
        uint8_t keys[64];
        state.Key(0x11, true, 1);
        Check(state.FormattedState(keyboard, readBack, keys, 44) && keys[40] == 0x80
                  && !state.FormattedState(keyboard, readBack, keys, 40),
              "a format read back with no size takes any read it fits in");
    }

    {
        DirectInputState state;
        DirectInputReader keyboard = state.Open(DirectInputKind::kKeyboard);
        DirectInputReader mouse = state.Open(DirectInputKind::kMouse);
        DirectInputEvent events[8];
        bool overflowed = true;

        Check(state.ReadEvents(keyboard, events, 8, false, overflowed) == 0 && !overflowed,
              "nothing is buffered before anything is played");

        state.Key(0x12, true, 1000);
        state.Key(0x12, true, 1010);
        state.Move(5, -7, 1020);
        state.Key(0x12, false, 1030);
        state.Button(MouseButton::kLeft, true, 1040);
        state.Move(0, 2, 1050);

        size_t count = state.ReadEvents(keyboard, events, 8, true, overflowed);
        Check(count == 2 && events[0].offset == 0x12 && events[0].data == 0x80 && events[0].timeMs == 1000
                  && events[1].offset == 0x12 && events[1].data == 0 && events[1].timeMs == 1030,
              "a keyboard gets its presses and releases, and a key already held is no second press");
        Check(events[1].sequence > events[0].sequence, "later events have later sequence numbers");
        Check(state.ReadEvents(keyboard, nullptr, 0xFFFFFFFFu, true, overflowed) == 2,
              "a peek leaves the events, and no buffer counts them");
        Check(state.ReadEvents(keyboard, events, 1, false, overflowed) == 1 && events[0].data == 0x80,
              "a read takes no more than the caller has room for");
        Check(state.ReadEvents(keyboard, events, 8, false, overflowed) == 1 && events[0].data == 0
                  && state.ReadEvents(keyboard, events, 8, false, overflowed) == 0,
              "what was read is gone and the rest follows");

        const uint32_t keyRelease = events[0].sequence;
        count = state.ReadEvents(mouse, events, 8, false, overflowed);
        Check(count == 4 && events[0].offset == 0 && static_cast<int32_t>(events[0].data) == 5
                  && events[1].offset == 4 && static_cast<int32_t>(events[1].data) == -7
                  && events[2].offset == 12 && events[2].data == 0x80
                  && events[3].offset == 4 && events[3].data == 2,
              "a mouse gets X and Y movement and buttons, at the standard offsets, and no event for an axis that did not move");
        Check(events[0].sequence == events[1].sequence, "the X and Y of one movement share a sequence number");
        Check(events[0].sequence < keyRelease && keyRelease < events[2].sequence && events[2].sequence < events[3].sequence,
              "sequence numbers order events across the keyboard and the mouse");

        state.Button(MouseButton::kMiddle, true, 1060);
        state.Button(MouseButton::kRight, true, 1070);
        Check(state.ReadEvents(mouse, nullptr, 0xFFFFFFFFu, false, overflowed) == 2
                  && state.ReadEvents(mouse, events, 8, false, overflowed) == 0,
              "a read with no buffer throws the events away");

        state.Key(0x1E, true, 2000);
        DirectInputReader late = state.Open(DirectInputKind::kKeyboard);
        state.Key(0x1E, false, 2010);
        Check(state.ReadEvents(late, events, 8, false, overflowed) == 1 && events[0].data == 0,
              "a device gets nothing from before it started reading");
    }

    {
        DirectInputState state;
        DirectInputReader keyboard = state.Open(DirectInputKind::kKeyboard);
        static DirectInputEvent events[cameraunlock::dev::kDirectInputEventSlots + 8];
        bool overflowed = false;
        const uint32_t presses = static_cast<uint32_t>(cameraunlock::dev::kDirectInputEventSlots) / 2 + 3;
        for (uint32_t i = 0; i < presses; ++i) {
            state.Key(0x39, true, i);
            state.Key(0x39, false, i);
        }
        const size_t count = state.ReadEvents(keyboard, events, cameraunlock::dev::kDirectInputEventSlots + 8, false,
                                              overflowed);
        Check(overflowed && count == cameraunlock::dev::kDirectInputEventSlots,
              "a device that fell behind is told it overflowed and gets the newest events");
        Check(events[0].timeMs == 3 && events[0].data == 0x80 && events[count - 1].timeMs == presses - 1
                  && events[count - 1].data == 0,
              "the events kept are the last ones, in order");
        state.Key(0x39, true, 9000);
        Check(state.ReadEvents(keyboard, events, 8, false, overflowed) == 1 && !overflowed,
              "the overflow is reported once");
    }

    return g_failures;
}
