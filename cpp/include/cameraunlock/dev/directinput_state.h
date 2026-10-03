#pragma once

// What a DirectInput 8 keyboard and mouse report, built from the input a test
// script plays (isolated_input.h). This header is the pure half: the key codes,
// the immediate state a device answers GetDeviceState with, and the buffered
// events it answers GetDeviceData with.
//
// A device is one DirectInputReader. Each reader has its own place in the
// events and its own count of the mouse movement it has reported, so two
// devices of a kind each see everything once.

#include "cameraunlock/dev/input_script.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cameraunlock::dev {

inline constexpr int kDikLeftShift = 0x2A;

// The DirectInput key code (DIK_*) of a virtual key, for the key that carries
// it on a US keyboard. 0 for a virtual key that has none.
inline int VkToDik(int vk) {
    static constexpr unsigned char kLetters[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
                                                   0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
                                                   0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
    static constexpr unsigned char kNumpad[10] = {0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49};
    if (vk >= 'A' && vk <= 'Z') return kLetters[vk - 'A'];
    if (vk >= '1' && vk <= '9') return 0x02 + (vk - '1');
    if (vk >= 0x60 && vk <= 0x69) return kNumpad[vk - 0x60];
    if (vk >= 0x70 && vk <= 0x79) return 0x3B + (vk - 0x70);
    switch (vk) {
        case 0x08: return 0x0E;  // Backspace
        case 0x09: return 0x0F;  // Tab
        case 0x0D: return 0x1C;  // Return
        case 0x10: return 0x2A;  // Shift, as the left one
        case 0x11: return 0x1D;  // Ctrl, as the left one
        case 0x12: return 0x38;  // Alt, as the left one
        case 0x13: return 0xC5;  // Pause
        case 0x14: return 0x3A;  // CapsLock
        case 0x1B: return 0x01;  // Escape
        case 0x20: return 0x39;  // Space
        case 0x21: return 0xC9;  // PageUp
        case 0x22: return 0xD1;  // PageDown
        case 0x23: return 0xCF;  // End
        case 0x24: return 0xC7;  // Home
        case 0x25: return 0xCB;  // Left
        case 0x26: return 0xC8;  // Up
        case 0x27: return 0xCD;  // Right
        case 0x28: return 0xD0;  // Down
        case 0x2C: return 0xB7;  // PrintScreen
        case 0x2D: return 0xD2;  // Insert
        case 0x2E: return 0xD3;  // Delete
        case 0x30: return 0x0B;  // 0
        case 0x5B: return 0xDB;  // left Windows
        case 0x5C: return 0xDC;  // right Windows
        case 0x5D: return 0xDD;  // Menu
        case 0x6A: return 0x37;  // numpad *
        case 0x6B: return 0x4E;  // numpad +
        case 0x6D: return 0x4A;  // numpad -
        case 0x6E: return 0x53;  // numpad .
        case 0x6F: return 0xB5;  // numpad /
        case 0x7A: return 0x57;  // F11
        case 0x7B: return 0x58;  // F12
        case 0x7C: return 0x64;  // F13
        case 0x7D: return 0x65;  // F14
        case 0x7E: return 0x66;  // F15
        case 0x90: return 0x45;  // NumLock
        case 0x91: return 0x46;  // ScrollLock
        case 0xA0: return 0x2A;  // left Shift
        case 0xA1: return 0x36;  // right Shift
        case 0xA2: return 0x1D;  // left Ctrl
        case 0xA3: return 0x9D;  // right Ctrl
        case 0xA4: return 0x38;  // left Alt
        case 0xA5: return 0xB8;  // right Alt
        case 0xBA: return 0x27;  // ;
        case 0xBB: return 0x0D;  // =
        case 0xBC: return 0x33;  // ,
        case 0xBD: return 0x0C;  // -
        case 0xBE: return 0x34;  // .
        case 0xBF: return 0x35;  // /
        case 0xC0: return 0x29;  // `
        case 0xDB: return 0x1A;  // [
        case 0xDC: return 0x2B;  // backslash
        case 0xDD: return 0x1B;  // ]
        case 0xDE: return 0x28;  // '
        default:   return 0;
    }
}

struct DirectInputKeyStroke {
    int dik = 0;
    bool shift = false;
};

// The key that types a character on a US keyboard, and whether Shift is held
// with it. `dik` is 0 for a character that keyboard has no key for: anything
// outside printable ASCII.
inline DirectInputKeyStroke UsKeyStroke(char character) {
    static constexpr char kPlain[] = "`1234567890-=[]\\;',./";
    static constexpr char kShifted[] = "~!@#$%^&*()_+{}|:\"<>?";
    static constexpr unsigned char kCodes[] = {0x29, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
                                               0x0C, 0x0D, 0x1A, 0x1B, 0x2B, 0x27, 0x28, 0x33, 0x34, 0x35};
    DirectInputKeyStroke stroke;
    if (character == ' ') {
        stroke.dik = 0x39;
    } else if (character >= 'a' && character <= 'z') {
        stroke.dik = VkToDik(character - 'a' + 'A');
    } else if (character >= 'A' && character <= 'Z') {
        stroke.dik = VkToDik(character);
        stroke.shift = true;
    } else {
        for (size_t i = 0; i < sizeof(kCodes); ++i) {
            if (character != kPlain[i] && character != kShifted[i]) continue;
            stroke.dik = kCodes[i];
            stroke.shift = character == kShifted[i];
            break;
        }
    }
    return stroke;
}

enum class DirectInputKind { kKeyboard, kMouse };

// A buffered event, laid out as the first four members of DIDEVICEOBJECTDATA.
struct DirectInputEvent {
    uint32_t offset = 0;
    uint32_t data = 0;
    uint32_t timeMs = 0;
    uint32_t sequence = 0;
};

// Where a mouse event's `offset` points, in the standard mouse data format.
inline constexpr uint32_t kMouseOffsetX = 0;
inline constexpr uint32_t kMouseOffsetY = 4;
inline constexpr uint32_t kMouseOffsetButton0 = 12;

inline constexpr size_t kKeyboardStateSize = 256;
// DIMOUSESTATE, with four buttons, and DIMOUSESTATE2, with eight.
inline constexpr size_t kMouseStateSize = 16;
inline constexpr size_t kMouseState2Size = 20;

// How many events a device that is not being read keeps. The oldest go first.
inline constexpr size_t kDirectInputEventSlots = 256;

struct DirectInputReader {
    DirectInputKind kind = DirectInputKind::kKeyboard;
    uint64_t next = 0;
    int64_t seenX = 0;
    int64_t seenY = 0;
};

class DirectInputState {
public:
    // False for a code that is no key (0, which VkToDik gives a virtual key
    // with no DirectInput code), and nothing is pressed. Holding a key that is
    // already down, or releasing one that is up, is no event.
    bool Key(int dik, bool down, uint32_t timeMs) {
        if (dik < 1 || dik > 255) return false;
        if (keys_[dik] == down) return true;
        keys_[dik] = down;
        Push(keyboard_, static_cast<uint32_t>(dik), down ? 0x80u : 0u, timeMs, sequence_++);
        return true;
    }

    void Button(MouseButton button, bool down, uint32_t timeMs) {
        const int index = static_cast<int>(button);
        if (buttons_[index] == down) return;
        buttons_[index] = down;
        Push(mouse_, kMouseOffsetButton0 + static_cast<uint32_t>(index), down ? 0x80u : 0u, timeMs, sequence_++);
    }

    // The X and Y of one movement carry one sequence number, so a reader can
    // put them back together.
    void Move(int dx, int dy, uint32_t timeMs) {
        if (dx == 0 && dy == 0) return;
        totalX_ += dx;
        totalY_ += dy;
        if (dx != 0) Push(mouse_, kMouseOffsetX, static_cast<uint32_t>(dx), timeMs, sequence_);
        if (dy != 0) Push(mouse_, kMouseOffsetY, static_cast<uint32_t>(dy), timeMs, sequence_);
        ++sequence_;
    }

    // A device that starts reading now: it is owed nothing from before.
    DirectInputReader Open(DirectInputKind kind) const {
        DirectInputReader reader;
        reader.kind = kind;
        reader.next = (kind == DirectInputKind::kKeyboard ? keyboard_ : mouse_).count;
        reader.seenX = totalX_;
        reader.seenY = totalY_;
        return reader;
    }

    // `out` is kKeyboardStateSize bytes: 0x80 at the code of each key held.
    void KeyboardState(uint8_t* out) const {
        for (size_t dik = 0; dik < kKeyboardStateSize; ++dik) out[dik] = keys_[dik] ? 0x80 : 0;
    }

    // The mouse as a relative-axis device reports it: the movement since this
    // reader last asked, which is then spent. False, with nothing written or
    // spent, for a size that is neither mouse state.
    bool MouseState(DirectInputReader& reader, uint8_t* out, size_t size) const {
        if (size != kMouseStateSize && size != kMouseState2Size) return false;
        const int32_t axes[3] = {static_cast<int32_t>(totalX_ - reader.seenX),
                                 static_cast<int32_t>(totalY_ - reader.seenY), 0};
        reader.seenX = totalX_;
        reader.seenY = totalY_;
        std::memset(out, 0, size);
        std::memcpy(out, axes, sizeof(axes));
        for (int i = 0; i < 3; ++i) out[sizeof(axes) + i] = buttons_[i] ? 0x80 : 0;
        return true;
    }

    // The events this reader has not had, oldest first, at most `capacity` of
    // them. `out` may be null, to count or to throw away. A peek leaves them to
    // be read again. `overflowed` is set when events were lost because more
    // than kDirectInputEventSlots went unread.
    size_t ReadEvents(DirectInputReader& reader, DirectInputEvent* out, size_t capacity, bool peek,
                      bool& overflowed) const {
        const Log& log = reader.kind == DirectInputKind::kKeyboard ? keyboard_ : mouse_;
        uint64_t next = reader.next;
        overflowed = log.count - next > kDirectInputEventSlots;
        if (overflowed) next = log.count - kDirectInputEventSlots;
        size_t count = 0;
        for (; count < capacity && next < log.count; ++count, ++next) {
            if (out) out[count] = log.events[next % kDirectInputEventSlots];
        }
        if (!peek) reader.next = next;
        return count;
    }

private:
    struct Log {
        DirectInputEvent events[kDirectInputEventSlots] = {};
        uint64_t count = 0;
    };

    static void Push(Log& log, uint32_t offset, uint32_t data, uint32_t timeMs, uint32_t sequence) {
        DirectInputEvent& event = log.events[log.count++ % kDirectInputEventSlots];
        event.offset = offset;
        event.data = data;
        event.timeMs = timeMs;
        event.sequence = sequence;
    }

    bool keys_[kKeyboardStateSize] = {};
    bool buttons_[3] = {};
    int64_t totalX_ = 0;
    int64_t totalY_ = 0;
    uint32_t sequence_ = 1;
    Log keyboard_;
    Log mouse_;
};

}  // namespace cameraunlock::dev
