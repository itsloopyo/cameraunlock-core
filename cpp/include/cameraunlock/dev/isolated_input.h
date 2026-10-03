#pragma once

// Isolated input: a lab build of a mod feeds its game keyboard and mouse input
// from inside the process, so a test session can run behind other windows
// while the person at the machine keeps their own mouse and keyboard.
//
// For games that read raw input (RegisterRawInputDevices and WM_INPUT, read
// back through GetRawInputData). Each synthetic event is posted to the game's
// window as a WM_INPUT message whose handle is one of ours, and the
// GetRawInputData detour answers for those handles. The real devices send the
// game nothing while it is in the background, which is what keeps the two
// apart.
//
// What else is detoured, and why, all in user32:
//
//   - GetForegroundWindow answers the game's own window, so neither the game
//     nor the mod stands down for being in the background.
//   - GetAsyncKeyState answers from the synthetic key state alone, so the mod's
//     hotkeys fire for the script and not for what is typed into another app.
//   - ClipCursor and SetCursorPos do nothing, and GetClipCursor answers what the
//     game last asked for. A game that believes it has the foreground would
//     otherwise trap and recentre the real cursor.
//   - SetForegroundWindow does nothing, so the game cannot take the foreground
//     back.
//
// Never in a release build: with these detours in, the game does not answer to
// the real keyboard. Requires MinHook, already initialised. Exactly one
// translation unit defines CAMERAUNLOCK_ISOLATED_INPUT_IMPLEMENTATION before
// including this.
//
// The harness writes a command file (input_script.h is the language): a whole
// number on the first line, commands after it. The file is played each time
// that number changes, and `<file>.done` then holds the number, or the number
// and the line that did not parse.

#include "cameraunlock/dev/input_script.h"

#include <string>

namespace cameraunlock::dev {

using IsolatedInputLogFn = void (*)(const char*);

// Installs the detours and starts the thread that plays `commandFile`. False
// when a detour could not be installed, in which case none is left in.
bool StartIsolatedInput(const std::wstring& commandFile, IsolatedInputLogFn log);

}  // namespace cameraunlock::dev

#ifdef CAMERAUNLOCK_ISOLATED_INPUT_IMPLEMENTATION

#include <MinHook.h>
#include <Windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>

namespace cameraunlock::dev {
namespace isolated {

// Events stay readable this long after they are posted: a ring, so a handle is
// reused only after this many later events.
inline constexpr int kEventSlots = 256;
inline constexpr unsigned kPollMs = 50;
inline constexpr unsigned kTextKeyMs = 40;

using GetRawInputData_t = UINT (WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
using GetForegroundWindow_t = HWND (WINAPI*)();
using GetAsyncKeyState_t = SHORT (WINAPI*)(int);
using ClipCursor_t = BOOL (WINAPI*)(const RECT*);
using GetClipCursor_t = BOOL (WINAPI*)(LPRECT);
using SetCursorPos_t = BOOL (WINAPI*)(int, int);
using SetForegroundWindow_t = BOOL (WINAPI*)(HWND);

struct State {
    GetRawInputData_t origGetRawInputData = nullptr;
    GetForegroundWindow_t origGetForegroundWindow = nullptr;
    GetAsyncKeyState_t origGetAsyncKeyState = nullptr;
    ClipCursor_t origClipCursor = nullptr;
    GetClipCursor_t origGetClipCursor = nullptr;
    SetCursorPos_t origSetCursorPos = nullptr;
    SetForegroundWindow_t origSetForegroundWindow = nullptr;

    RAWINPUT events[kEventSlots] = {};
    std::atomic<unsigned> nextEvent{0};
    std::atomic<bool> keyDown[256] = {};
    // Set on a press and cleared by the read that reports it, as the low bit of
    // GetAsyncKeyState is.
    std::atomic<bool> keyPressedSinceRead[256] = {};

    std::atomic<HWND> window{nullptr};
    HANDLE keyboard = nullptr;
    HANDLE mouse = nullptr;

    std::atomic<bool> clipRequested{false};
    RECT clip = {};

    IsolatedInputLogFn log = nullptr;
    std::wstring commandFile;
};

inline State& S() {
    static State s;
    return s;
}

inline void Log(const char* text) {
    if (S().log) S().log(text);
}

inline bool IsOurs(HRAWINPUT handle) {
    const auto at = reinterpret_cast<uintptr_t>(handle);
    const auto first = reinterpret_cast<uintptr_t>(&S().events[0]);
    return at >= first && at < first + sizeof(S().events) && (at - first) % sizeof(RAWINPUT) == 0;
}

inline UINT WINAPI HookedGetRawInputData(HRAWINPUT handle, UINT command, LPVOID data, PUINT size,
                                         UINT headerSize) {
    if (!IsOurs(handle)) return S().origGetRawInputData(handle, command, data, size, headerSize);
    const RAWINPUT& event = *reinterpret_cast<const RAWINPUT*>(handle);
    if (size == nullptr || headerSize != sizeof(RAWINPUTHEADER)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return static_cast<UINT>(-1);
    }
    const UINT needed = command == RID_HEADER ? sizeof(RAWINPUTHEADER) : event.header.dwSize;
    if (data == nullptr) {
        *size = needed;
        return 0;
    }
    if (*size < needed) {
        *size = needed;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return static_cast<UINT>(-1);
    }
    std::memcpy(data, &event, needed);
    return needed;
}

inline HWND WINAPI HookedGetForegroundWindow() {
    const HWND window = S().window.load(std::memory_order_acquire);
    return window ? window : S().origGetForegroundWindow();
}

inline SHORT WINAPI HookedGetAsyncKeyState(int vk) {
    if (vk < 0 || vk > 255) return 0;
    SHORT state = S().keyDown[vk].load(std::memory_order_acquire) ? static_cast<SHORT>(0x8000) : 0;
    if (S().keyPressedSinceRead[vk].exchange(false, std::memory_order_acq_rel)) state |= 1;
    return state;
}

inline BOOL WINAPI HookedClipCursor(const RECT* rect) {
    if (rect) S().clip = *rect;
    S().clipRequested.store(rect != nullptr, std::memory_order_release);
    return TRUE;
}

inline BOOL WINAPI HookedGetClipCursor(LPRECT rect) {
    if (!S().clipRequested.load(std::memory_order_acquire)) return S().origGetClipCursor(rect);
    *rect = S().clip;
    return TRUE;
}

inline BOOL WINAPI HookedSetCursorPos(int, int) { return TRUE; }
inline BOOL WINAPI HookedSetForegroundWindow(HWND) { return TRUE; }

struct WindowSearch {
    DWORD pid;
    HWND best;
    long bestArea;
};

inline BOOL CALLBACK PickLargestWindow(HWND hwnd, LPARAM param) {
    auto& search = *reinterpret_cast<WindowSearch*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    RECT rect = {};
    if (pid != search.pid || !IsWindowVisible(hwnd) || !GetWindowRect(hwnd, &rect)) return TRUE;
    const long area = (rect.right - rect.left) * (rect.bottom - rect.top);
    if (area > search.bestArea) {
        search.best = hwnd;
        search.bestArea = area;
    }
    return TRUE;
}

// The game's main window: the largest visible top-level window of this process.
// Null until the game has made one.
inline HWND FindGameWindow() {
    WindowSearch search = {GetCurrentProcessId(), nullptr, 0};
    EnumWindows(&PickLargestWindow, reinterpret_cast<LPARAM>(&search));
    return search.best;
}

// The first keyboard and the first mouse raw input knows of, so an event names
// a device the game can look up.
inline void FindDevices() {
    UINT count = 0;
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0 || count == 0) return;
    std::vector<RAWINPUTDEVICELIST> devices(count);
    const UINT got = GetRawInputDeviceList(devices.data(), &count, sizeof(RAWINPUTDEVICELIST));
    if (got == static_cast<UINT>(-1)) return;
    for (UINT i = 0; i < got; ++i) {
        if (devices[i].dwType == RIM_TYPEKEYBOARD && !S().keyboard) S().keyboard = devices[i].hDevice;
        if (devices[i].dwType == RIM_TYPEMOUSE && !S().mouse) S().mouse = devices[i].hDevice;
    }
}

inline bool Post(const RAWINPUT& event) {
    const HWND window = S().window.load(std::memory_order_acquire);
    if (!window) return false;
    const unsigned slot = S().nextEvent.fetch_add(1, std::memory_order_acq_rel) % kEventSlots;
    S().events[slot] = event;
    return PostMessageW(window, WM_INPUT, RIM_INPUT, reinterpret_cast<LPARAM>(&S().events[slot])) != 0;
}

inline bool IsExtendedKey(int vk) {
    switch (vk) {
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_RCONTROL: case VK_RMENU:
        case VK_DIVIDE: case VK_NUMLOCK:
            return true;
        default:
            return false;
    }
}

inline bool SendKey(int vk, bool down) {
    S().keyDown[vk].store(down, std::memory_order_release);
    if (down) S().keyPressedSinceRead[vk].store(true, std::memory_order_release);

    RAWINPUT event = {};
    event.header.dwType = RIM_TYPEKEYBOARD;
    event.header.dwSize = sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD);
    event.header.hDevice = S().keyboard;
    event.data.keyboard.MakeCode = static_cast<USHORT>(MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC));
    event.data.keyboard.Flags = static_cast<USHORT>((down ? RI_KEY_MAKE : RI_KEY_BREAK) | (IsExtendedKey(vk) ? RI_KEY_E0 : 0));
    event.data.keyboard.VKey = static_cast<USHORT>(vk);
    event.data.keyboard.Message = down ? WM_KEYDOWN : WM_KEYUP;
    return Post(event);
}

inline bool SendMouse(USHORT buttonFlags, int dx, int dy) {
    RAWINPUT event = {};
    event.header.dwType = RIM_TYPEMOUSE;
    event.header.dwSize = sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE);
    event.header.hDevice = S().mouse;
    event.data.mouse.usFlags = MOUSE_MOVE_RELATIVE;
    event.data.mouse.usButtonFlags = buttonFlags;
    event.data.mouse.lLastX = dx;
    event.data.mouse.lLastY = dy;
    return Post(event);
}

inline USHORT ButtonFlag(MouseButton button, bool down) {
    switch (button) {
        case MouseButton::kLeft:  return down ? RI_MOUSE_LEFT_BUTTON_DOWN : RI_MOUSE_LEFT_BUTTON_UP;
        case MouseButton::kRight: return down ? RI_MOUSE_RIGHT_BUTTON_DOWN : RI_MOUSE_RIGHT_BUTTON_UP;
        default:                  return down ? RI_MOUSE_MIDDLE_BUTTON_DOWN : RI_MOUSE_MIDDLE_BUTTON_UP;
    }
}

// One character as the key presses that type it on the current layout, and as
// the WM_CHAR a text field reads.
inline bool SendCharacter(char character) {
    const SHORT scan = VkKeyScanA(character);
    if (scan == -1) return false;
    const int vk = scan & 0xFF;
    const bool shift = (scan & 0x100) != 0;
    bool ok = true;
    if (shift) ok = SendKey(VK_SHIFT, true) && ok;
    ok = SendKey(vk, true) && ok;
    ok = PostMessageW(S().window.load(std::memory_order_acquire), WM_CHAR, static_cast<WPARAM>(character), 1) != 0 && ok;
    Sleep(kTextKeyMs);
    ok = SendKey(vk, false) && ok;
    if (shift) ok = SendKey(VK_SHIFT, false) && ok;
    Sleep(kTextKeyMs);
    return ok;
}

inline bool Play(const InputStep& step) {
    switch (step.action) {
        case InputAction::kKeyDown:   return SendKey(step.vk, true);
        case InputAction::kKeyUp:     return SendKey(step.vk, false);
        case InputAction::kMouseDown: return SendMouse(ButtonFlag(step.button, true), 0, 0);
        case InputAction::kMouseUp:   return SendMouse(ButtonFlag(step.button, false), 0, 0);
        case InputAction::kMouseMove: return SendMouse(0, step.dx, step.dy);
        case InputAction::kWait:      Sleep(step.waitMs); return true;
        case InputAction::kText: {
            bool ok = true;
            for (const char character : step.text) ok = SendCharacter(character) && ok;
            return ok;
        }
    }
    return false;
}

inline void WriteDone(const std::string& text) {
    std::ofstream done(S().commandFile + L".done", std::ios::trunc);
    done << text << "\n";
}

// Plays the command file once: every line parsed before any is played, so a
// script with a mistake in it sends the game nothing.
inline void PlayFile(long long sequence, std::ifstream& file) {
    std::vector<InputStep> steps;
    std::string line, error;
    int lineNumber = 1;
    char report[320];
    while (std::getline(file, line)) {
        ++lineNumber;
        if (!ParseInputLine(line, steps, error)) {
            std::snprintf(report, sizeof(report), "%lld line %d: %s", sequence, lineNumber, error.c_str());
            Log(report);
            WriteDone(report);
            return;
        }
    }
    int failed = 0;
    for (const InputStep& step : steps) {
        if (!Play(step)) ++failed;
    }
    if (failed == 0) {
        std::snprintf(report, sizeof(report), "%lld", sequence);
    } else {
        std::snprintf(report, sizeof(report), "%lld %d of %zu steps could not be sent (no game window yet?)",
                      sequence, failed, steps.size());
    }
    char logLine[360];
    std::snprintf(logLine, sizeof(logLine), "isolated input: played script %s (%zu steps)", report, steps.size());
    Log(logLine);
    WriteDone(report);
}

inline void RunCommandFile() {
    long long lastSequence = -1;
    bool first = true;
    for (;;) {
        Sleep(kPollMs);
        // From the moment the game has a window it is told that window has the
        // foreground, script or no script.
        if (!S().window.load(std::memory_order_acquire)) {
            if (const HWND window = FindGameWindow()) {
                S().window.store(window, std::memory_order_release);
                Log("isolated input: game window found");
            }
        }
        std::ifstream file(S().commandFile);
        std::string header;
        if (!file || !std::getline(file, header)) continue;
        long long sequence = 0;
        if (std::sscanf(header.c_str(), "%lld", &sequence) != 1) continue;
        // Whatever the file holds when the game starts was written for an
        // earlier run.
        if (first) {
            first = false;
            lastSequence = sequence;
            continue;
        }
        if (sequence == lastSequence) continue;
        lastSequence = sequence;
        PlayFile(sequence, file);
    }
}

template <typename Detour, typename Original>
inline bool Hook(const char* name, Detour detour, Original& original) {
    void* target = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), name));
    if (target && MH_CreateHook(target, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(&original)) == MH_OK
        && MH_EnableHook(target) == MH_OK) {
        return true;
    }
    char line[120];
    std::snprintf(line, sizeof(line), "isolated input: could not hook %s", name);
    Log(line);
    if (target) MH_RemoveHook(target);
    return false;
}

inline void Unhook(const char* name) {
    void* target = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), name));
    if (target) {
        MH_DisableHook(target);
        MH_RemoveHook(target);
    }
}

}  // namespace isolated

inline bool StartIsolatedInput(const std::wstring& commandFile, IsolatedInputLogFn log) {
    using namespace isolated;
    State& s = S();
    s.log = log;
    s.commandFile = commandFile;
    FindDevices();

    static const char* const kNames[] = {"GetRawInputData", "GetForegroundWindow", "GetAsyncKeyState", "ClipCursor",
                                         "GetClipCursor", "SetCursorPos", "SetForegroundWindow"};
    const bool hooked[] = {
        Hook(kNames[0], &HookedGetRawInputData, s.origGetRawInputData),
        Hook(kNames[1], &HookedGetForegroundWindow, s.origGetForegroundWindow),
        Hook(kNames[2], &HookedGetAsyncKeyState, s.origGetAsyncKeyState),
        Hook(kNames[3], &HookedClipCursor, s.origClipCursor),
        Hook(kNames[4], &HookedGetClipCursor, s.origGetClipCursor),
        Hook(kNames[5], &HookedSetCursorPos, s.origSetCursorPos),
        Hook(kNames[6], &HookedSetForegroundWindow, s.origSetForegroundWindow),
    };
    bool all = true;
    for (const bool one : hooked) all = all && one;
    if (!all) {
        for (size_t i = 0; i < sizeof(hooked) / sizeof(hooked[0]); ++i) {
            if (hooked[i]) Unhook(kNames[i]);
        }
        return false;
    }
    std::thread(&RunCommandFile).detach();
    Log("isolated input: the game takes its keyboard and mouse from the command file, not from the real devices");
    return true;
}

}  // namespace cameraunlock::dev

#endif  // CAMERAUNLOCK_ISOLATED_INPUT_IMPLEMENTATION
