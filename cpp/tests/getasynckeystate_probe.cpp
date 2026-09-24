// `cameraunlock_tests --probe-getasynckeystate`, run by `pixi run probe-n1`: the Windows check that
// normalisation N1 rests on. N1 imports a legacy hotkey code outside 0x01-0xFE as unbound, which is
// no change only while GetAsyncKeyState never reports such a code for a key that is down.
//
// Not part of the suite: it injects a key press with SendInput, which needs an interactive desktop
// and reaches whatever window has focus. F24 is held because no standard keyboard has it.

#include <iostream>

#ifdef _WIN32

#include <windows.h>

#include <string>
#include <vector>

namespace {

constexpr int kHeld = 0x87;  // VK_F24

bool Down(int code) { return (GetAsyncKeyState(code) & 0x8000) != 0; }

bool Send(WORD vk, bool up) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    if (SendInput(1, &input, sizeof(input)) == 1) return true;
    std::cout << "  SendInput failed, error " << GetLastError() << "\n";
    return false;
}

// The injected event reaches the async key state through the raw input thread, so wait for it.
bool WaitFor(int code, bool down) {
    for (int i = 0; i < 100; ++i) {
        if (Down(code) == down) return true;
        Sleep(10);
    }
    return false;
}

std::string Hex(int code) {
    char text[16];
    wsprintfA(text, "0x%X", static_cast<unsigned>(code));
    return text;
}

class Released {
public:
    explicit Released(WORD vk) : vk_(vk) {}
    ~Released() { Send(vk_, true); }
    Released(const Released&) = delete;
    Released& operator=(const Released&) = delete;

private:
    WORD vk_;
};

}  // namespace

int RunGetAsyncKeyStateProbe() {
    std::cout << "GetAsyncKeyState probe for normalisation N1\n";

    HDESK desk = OpenInputDesktop(0, FALSE, GENERIC_READ);
    if (desk == nullptr) {
        std::cout << "  no interactive input desktop (error " << GetLastError()
                  << "), so the probe cannot run\n";
        return 2;
    }
    CloseDesktop(desk);

    if (Down(kHeld)) {
        std::cout << "  [FAIL] F24 is already down before the probe\n";
        return 1;
    }

    int failures = 0;
    {
        if (!Send(kHeld, false)) return 1;
        Released release(kHeld);
        if (!WaitFor(kHeld, true)) {
            std::cout << "  [FAIL] F24 (0x87) held with SendInput does not report down\n";
            return 1;
        }
        std::cout << "  [PASS] F24 (0x87) held: GetAsyncKeyState(0x87) reports down\n";

        const std::vector<int> codes = {kHeld | 0x100, kHeld | 0x200, kHeld | 0x10000, kHeld - 0x100, 0, 0xFF, 0x100, -1};
        for (int code : codes) {
            const bool down = Down(code);
            std::cout << "  [" << (down ? "FAIL" : "PASS") << "] F24 held: GetAsyncKeyState(" << code << ", " << Hex(code)
                      << ") reports " << (down ? "down" : "up") << "\n";
            if (down) ++failures;
        }
    }
    if (!WaitFor(kHeld, false)) {
        std::cout << "  [FAIL] F24 still reports down after its release\n";
        ++failures;
    }

    // Not a pass condition: 0xFF is outside 0x01-0xFE yet is a code the system delivers for a
    // scan code the layout does not map, so a legacy hotkey on 0xFF could fire. Printed so the
    // record says what N1 changes.
    if (Send(0xFF, false)) {
        Released release(0xFF);
        std::cout << "  fact: VK 0xFF held with SendInput: GetAsyncKeyState(0xFF) reports "
                  << (WaitFor(0xFF, true) ? "down" : "up") << "\n";
    }
    WaitFor(0xFF, false);

    std::cout << (failures == 0 ? "No code outside 0x01-0xFE reported the held key.\n"
                                : "A code outside 0x01-0xFE reported the held key: N1 goes back to the owner.\n");
    return failures == 0 ? 0 : 1;
}

#else

int RunGetAsyncKeyStateProbe() {
    std::cout << "The GetAsyncKeyState probe runs on Windows only\n";
    return 2;
}

#endif
