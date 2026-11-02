#pragma once

// Isolated input: a lab build of a mod feeds its game keyboard and mouse input
// from inside the process, so a test session can run behind other windows
// while the person at the machine keeps their own mouse and keyboard.
//
// For games that read raw input (RegisterRawInputDevices and WM_INPUT, read
// back through GetRawInputData). Each synthetic event is posted to the game's
// window as a WM_INPUT message whose handle is one of ours, and the
// GetRawInputData detour answers for those handles. Windows sends a background
// window no raw input unless it registered with RIDEV_INPUTSINK, and a real
// keyboard or mouse event that does arrive is emptied by the same detour and by
// GetRawInputBuffer's, so it moves and presses nothing.
//
// A key also arrives as WM_KEYDOWN and WM_KEYUP, as it does from Windows, for a
// game that takes its keys from its window procedure. Not where the game
// registered its keyboard with RIDEV_NOLEGACY: Windows sends none there.
//
// What else is detoured, and why, all in user32:
//
//   - GetForegroundWindow answers the game's own window, so neither the game
//     nor the mod stands down for being in the background.
//   - GetAsyncKeyState answers from the synthetic key state alone, so the mod's
//     hotkeys fire for the script and not for what is typed into another app.
//   - GetKeyState and GetKeyboardState answer from the same synthetic state, for
//     an engine that polls its keys or its modifiers through them. Their low bit
//     is the toggle Windows keeps for every key: it flips on each press.
//   - ClipCursor and SetCursorPos do nothing, and GetClipCursor answers what the
//     game last asked for. A game that believes it has the foreground would
//     otherwise trap and recentre the real cursor. A mod that places the cursor
//     itself, and has to see it land, asks for SetCursorPos to move the script's
//     cursor instead (LetGameMoveScriptedCursor).
//   - SetForegroundWindow does nothing, so the game cannot take the foreground
//     back.
//   - GetCursorPos never answers the real cursor once the game window is found.
//     It answers the middle of the window until a `cursor` command gives a
//     point: a menu that reads where the cursor is gets the script's cursor, as a
//     WM_MOUSEMOVE at that point and as the position it reads back, and a button
//     then also arrives as the window message a click there would be.
//   - GetCursorInfo answers the cursor as the game last set it, shown or
//     hidden, and where GetCursorPos says it is. Windows answers for the
//     foreground window, which is never the game's, so a mod that holds its
//     pose while a menu shows the cursor would read the desktop's cursor. The
//     game's setting is followed through ShowCursor's display count, the cursor
//     SetCursor was last given, and the cursor the window has after
//     WM_SETCURSOR, which is where DefWindowProc puts the class cursor back.
//
// And two things that keep the game in the background without it knowing:
//
//   - The real foreground is watched, and whenever a window of the game holds it
//     the game gives it back to the window that had it before. Only the
//     foreground process may do that, so it is done from in here.
//   - The game's window procedure never sees the messages that say it lost the
//     foreground (WM_ACTIVATEAPP, WM_ACTIVATE, WM_KILLFOCUS and a WM_NCACTIVATE
//     that deactivates), so a game that stops following its mouse when it is
//     deactivated carries on.
//
// For games that read DirectInput 8 (dinput8.dll loaded in the process): the
// keyboard and mouse devices answer GetDeviceState and GetDeviceData from the
// script alone (directinput_state.h), through detours on the device vtable, so
// a device the game makes later is covered too. They answer in the data format
// the game set, whatever its layout: DirectInput is asked which object sits at
// each offset of it. The real devices are never
// acquired: Acquire and Poll succeed without reaching DirectInput, and
// SetCooperativeLevel is passed on as background and non-exclusive. Any other
// DirectInput device (a controller) is left as it was. dinput8.dll is never
// loaded by this header: a game that has not loaded it gets no such detours.
//
// For games that read GameInput (GameInputRedist.dll or GameInput.dll loaded in
// the process): the readings of API version 2 answer their keyboard, mouse and
// gamepad from the script (gameinput_state.h) and report no other controller,
// through the vtables GameInput hands out, so the game's own object is covered
// however early it was made. Every other version is given no readings, and a
// version this header does not know is refused when the game asks for it.
// GameInput is never loaded by this header either.
//
// Never in a release build: with these detours in, the game does not answer to
// the real keyboard. Requires MinHook, already initialised. Exactly one
// translation unit defines CAMERAUNLOCK_ISOLATED_INPUT_IMPLEMENTATION before
// including this. A mod with no native code loads cpp/tools/isolated_input_host,
// which is this header as a DLL.
//
// The harness writes a command file (input_script.h is the language): a whole
// number on the first line, commands after it. The file is played each time
// that number changes, and `<file>.done` then holds the number, or the number
// and the line that did not parse.

#include "cameraunlock/dev/directinput_state.h"
#include "cameraunlock/dev/gameinput_state.h"
#include "cameraunlock/dev/input_script.h"

#include <string>

namespace cameraunlock::dev {

using IsolatedInputLogFn = void (*)(const char*);

// Installs the detours and starts the thread that plays `commandFile`. False
// when a detour could not be installed, in which case none is left in.
bool StartIsolatedInput(const std::wstring& commandFile, IsolatedInputLogFn log);

// With this on, a SetCursorPos from the game or the mod moves the script's
// cursor to that point, as it would move the real one: GetCursorPos answers it
// and the window gets its WM_MOUSEMOVE. The real cursor still does not move.
// Off by default, where SetCursorPos does nothing: most games only recentre the
// cursor with it, which would pull the script's cursor off the menu item it was
// put on. It takes effect once a `cursor` command has placed the cursor.
void LetGameMoveScriptedCursor(bool allowed);

// Holds a key down, or lets it go, for GetAsyncKeyState, GetKeyState and
// GetKeyboardState alone, from any thread: nothing is posted to the game's
// window. For a test harness inside the game that feeds the game's own keyboard
// another way and has to reach a mod's hotkey poller too, as Project Zomboid's
// Java probe does. A modifier is held as the script holds one: VK_SHIFT, VK_CONTROL or
// VK_MENU holds the left-hand key too, and the left-hand key alone is not the generic
// one. False for a key code outside 1 to 255.
bool HoldIsolatedKey(int vk, bool down);

}  // namespace cameraunlock::dev

#ifdef CAMERAUNLOCK_ISOLATED_INPUT_IMPLEMENTATION

#include <MinHook.h>
#include <Windows.h>

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#include <dbt.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace cameraunlock::dev {
namespace isolated {

// Events stay readable this long after they are posted: a ring, so a handle is
// reused only after this many later events.
inline constexpr int kEventSlots = 256;
inline constexpr unsigned kPollMs = 50;
inline constexpr unsigned kTextKeyMs = 40;
// A pad's first command is held back until the game has read the new pad this
// many times, or given up on after the wait.
inline constexpr unsigned kPadPlugReads = 3;
inline constexpr unsigned kPadPlugWaitMs = 5000;

using GetRawInputData_t = UINT (WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
using GetRawInputBuffer_t = UINT (WINAPI*)(PRAWINPUT, PUINT, UINT);
using GetForegroundWindow_t = HWND (WINAPI*)();
using GetAsyncKeyState_t = SHORT (WINAPI*)(int);
using GetKeyState_t = SHORT (WINAPI*)(int);
using GetKeyboardState_t = BOOL (WINAPI*)(PBYTE);
using ClipCursor_t = BOOL (WINAPI*)(const RECT*);
using GetClipCursor_t = BOOL (WINAPI*)(LPRECT);
using SetCursorPos_t = BOOL (WINAPI*)(int, int);
using SetForegroundWindow_t = BOOL (WINAPI*)(HWND);
using GetCursorPos_t = BOOL (WINAPI*)(LPPOINT);
using ShowCursor_t = int (WINAPI*)(BOOL);
using SetCursor_t = HCURSOR (WINAPI*)(HCURSOR);
using GetCursorInfo_t = BOOL (WINAPI*)(PCURSORINFO);
using XInputGetState_t = DWORD (WINAPI*)(DWORD, void*);
using XInputSetState_t = DWORD (WINAPI*)(DWORD, void*);
using XInputGetCapabilities_t = DWORD (WINAPI*)(DWORD, DWORD, void*);

// The XInput DLLs a game can be reading its controllers through. Only the ones
// the game has already loaded are detoured.
inline constexpr const wchar_t* kXInputDlls[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll",
                                                 L"xinput1_2.dll", L"xinput1_1.dll"};
inline constexpr int kXInputDllCount = sizeof(kXInputDlls) / sizeof(kXInputDlls[0]);

struct State {
    GetRawInputData_t origGetRawInputData = nullptr;
    GetRawInputBuffer_t origGetRawInputBuffer = nullptr;
    std::atomic<bool> realRawInputSeen{false};
    GetForegroundWindow_t origGetForegroundWindow = nullptr;
    GetAsyncKeyState_t origGetAsyncKeyState = nullptr;
    GetKeyState_t origGetKeyState = nullptr;
    GetKeyboardState_t origGetKeyboardState = nullptr;
    ClipCursor_t origClipCursor = nullptr;
    GetClipCursor_t origGetClipCursor = nullptr;
    SetCursorPos_t origSetCursorPos = nullptr;
    SetForegroundWindow_t origSetForegroundWindow = nullptr;
    GetCursorPos_t origGetCursorPos = nullptr;
    ShowCursor_t origShowCursor = nullptr;
    SetCursor_t origSetCursor = nullptr;
    GetCursorInfo_t origGetCursorInfo = nullptr;

    // The cursor as the game last set it, from whichever of its threads set it
    // last. The arrow until the game sets one.
    std::atomic<int> displayCount{0};
    std::atomic<HCURSOR> cursor{nullptr};
    std::atomic<bool> cursorInfoRead{false};

    // The cursor the script placed, in the game window's client area.
    std::atomic<bool> cursorPlaced{false};
    std::atomic<int> cursorX{0};
    std::atomic<int> cursorY{0};
    std::atomic<unsigned> buttonsHeld{0};
    std::atomic<bool> cursorPosRead{false};
    std::atomic<bool> gameMovesCursor{false};

    RAWINPUT events[kEventSlots] = {};
    std::atomic<unsigned> nextEvent{0};
    std::atomic<bool> keyDown[256] = {};
    // Set on a press and cleared by the read that reports it, as the low bit of
    // GetAsyncKeyState is.
    std::atomic<bool> keyPressedSinceRead[256] = {};
    // Flipped by each press, as the low bit of GetKeyState is.
    std::atomic<bool> keyToggled[256] = {};
    std::atomic<bool> keyStateRead{false};
    std::atomic<bool> keyboardStateRead{false};

    std::atomic<HWND> window{nullptr};
    // The game window's own procedure, under ours.
    std::atomic<WNDPROC> windowProc{nullptr};
    // The last window of another process seen holding the real foreground.
    HWND lastForeground = nullptr;
    HANDLE keyboard = nullptr;
    HANDLE mouse = nullptr;

    std::atomic<bool> clipRequested{false};
    RECT clip = {};

    // The script's controllers. The real ones are never read.
    std::mutex padLock;
    PadState pads[kPadCount];
    XInputGetState_t origXInputGetState[kXInputDllCount] = {};
    XInputSetState_t origXInputSetState[kXInputDllCount] = {};
    XInputGetCapabilities_t origXInputGetCapabilities[kXInputDllCount] = {};
    std::atomic<bool> padRead{false};
    // Reads of each script pad that found it plugged in.
    std::atomic<unsigned> padReads[kPadCount] = {};

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

// A real keyboard or mouse event, emptied in place. A window registered with
// RIDEV_INPUTSINK is sent raw input while it is in the background (Rewired
// registers its own window that way), so the real devices do reach a
// background game, and the person's mouse turned The Forest's camera from
// another window until this withheld it. The event keeps its header, so the
// game's reader sees a well-formed event that moves and presses nothing. Other
// HID devices are left as they are.
inline void WithholdRealDevice(RAWINPUT& event) {
    if (event.header.dwType == RIM_TYPEMOUSE) {
        event.data.mouse.ulButtons = 0;
        event.data.mouse.ulRawButtons = 0;
        event.data.mouse.lLastX = 0;
        event.data.mouse.lLastY = 0;
    } else if (event.header.dwType == RIM_TYPEKEYBOARD) {
        event.data.keyboard.MakeCode = 0;
        event.data.keyboard.VKey = 0xFF;
        event.data.keyboard.Message = WM_NULL;
    } else {
        return;
    }
    if (!S().realRawInputSeen.exchange(true, std::memory_order_acq_rel)) {
        Log("isolated input: the game is sent raw input from the real keyboard or mouse while in the "
            "background (RIDEV_INPUTSINK); it is withheld");
    }
}

inline UINT WINAPI HookedGetRawInputBuffer(PRAWINPUT data, PUINT size, UINT headerSize) {
    const UINT count = S().origGetRawInputBuffer(data, size, headerSize);
    if (count == static_cast<UINT>(-1) || data == nullptr || headerSize != sizeof(RAWINPUTHEADER)) return count;
    PRAWINPUT event = data;
    for (UINT i = 0; i < count; ++i) {
        WithholdRealDevice(*event);
        // NEXTRAWINPUTBLOCK, which names QWORD and so needs a typedef the SDK leaves to the caller.
        const auto next = reinterpret_cast<uintptr_t>(event) + event->header.dwSize;
        event = reinterpret_cast<PRAWINPUT>((next + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1));
    }
    return count;
}

inline UINT WINAPI HookedGetRawInputData(HRAWINPUT handle, UINT command, LPVOID data, PUINT size,
                                         UINT headerSize) {
    if (!IsOurs(handle)) {
        const UINT copied = S().origGetRawInputData(handle, command, data, size, headerSize);
        if (copied != static_cast<UINT>(-1) && copied >= sizeof(RAWINPUTHEADER) && command == RID_INPUT &&
            data != nullptr && headerSize == sizeof(RAWINPUTHEADER)) {
            WithholdRealDevice(*static_cast<RAWINPUT*>(data));
        }
        return copied;
    }
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

inline SHORT WINAPI HookedGetKeyState(int vk) {
    if (!S().keyStateRead.exchange(true, std::memory_order_acq_rel)) {
        Log("isolated input: the game reads key state through GetKeyState");
    }
    if (vk < 0 || vk > 255) return 0;
    SHORT state = S().keyDown[vk].load(std::memory_order_acquire) ? static_cast<SHORT>(0x8000) : 0;
    if (S().keyToggled[vk].load(std::memory_order_acquire)) state |= 1;
    return state;
}

inline BOOL WINAPI HookedGetKeyboardState(PBYTE state) {
    if (!S().keyboardStateRead.exchange(true, std::memory_order_acq_rel)) {
        Log("isolated input: the game reads key state through GetKeyboardState");
    }
    if (!state) return S().origGetKeyboardState(state);
    for (int vk = 0; vk < 256; ++vk) {
        state[vk] = static_cast<BYTE>((S().keyDown[vk].load(std::memory_order_acquire) ? 0x80 : 0)
                                      | (S().keyToggled[vk].load(std::memory_order_acquire) ? 0x01 : 0));
    }
    return TRUE;
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

inline BOOL WINAPI HookedSetCursorPos(int x, int y) {
    const HWND window = S().window.load(std::memory_order_acquire);
    if (!window || !S().gameMovesCursor.load(std::memory_order_acquire) ||
        !S().cursorPlaced.load(std::memory_order_acquire))
        return TRUE;
    POINT point{x, y};
    if (!ScreenToClient(window, &point)) return TRUE;
    S().cursorX.store(point.x, std::memory_order_relaxed);
    S().cursorY.store(point.y, std::memory_order_relaxed);
    PostMessageW(window, WM_MOUSEMOVE, S().buttonsHeld.load(std::memory_order_relaxed),
                 MAKELPARAM(point.x, point.y));
    return TRUE;
}

inline BOOL ScriptCursorPos(LPPOINT point) {
    const HWND window = S().window.load(std::memory_order_acquire);
    if (!point || !window) return S().origGetCursorPos(point);
    if (!S().cursorPlaced.load(std::memory_order_acquire)) {
        // Never the real cursor: a game that turns its view or moves a pointer by
        // where the cursor is would follow the person's mouse in another window.
        RECT client;
        if (!GetClientRect(window, &client)) return S().origGetCursorPos(point);
        *point = {(client.right - client.left) / 2, (client.bottom - client.top) / 2};
        return ClientToScreen(window, point);
    }
    *point = {S().cursorX.load(std::memory_order_relaxed), S().cursorY.load(std::memory_order_relaxed)};
    return ClientToScreen(window, point);
}

inline BOOL WINAPI HookedGetCursorPos(LPPOINT point) {
    if (!S().cursorPosRead.exchange(true, std::memory_order_acq_rel)) {
        Log("isolated input: the game reads the cursor's position (GetCursorPos)");
    }
    return ScriptCursorPos(point);
}

inline int WINAPI HookedShowCursor(BOOL show) {
    const int count = S().origShowCursor(show);
    S().displayCount.store(count, std::memory_order_release);
    return count;
}

inline HCURSOR WINAPI HookedSetCursor(HCURSOR cursor) {
    const HCURSOR previous = S().origSetCursor(cursor);
    S().cursor.store(cursor, std::memory_order_release);
    return previous;
}

// Shown while the game's display count is not negative and it has a cursor set.
inline BOOL WINAPI HookedGetCursorInfo(PCURSORINFO info) {
    if (!info || info->cbSize != sizeof(CURSORINFO)) return S().origGetCursorInfo(info);
    if (!S().cursorInfoRead.exchange(true, std::memory_order_acq_rel)) {
        Log("isolated input: GetCursorInfo is read, and answers the cursor as the game set it");
    }
    const HCURSOR cursor = S().cursor.load(std::memory_order_acquire);
    info->flags = S().displayCount.load(std::memory_order_acquire) >= 0 && cursor ? CURSOR_SHOWING : 0;
    info->hCursor = cursor;
    return ScriptCursorPos(&info->ptScreenPos);
}

inline BOOL WINAPI HookedSetForegroundWindow(HWND) { return TRUE; }

// XINPUT_STATE: the packet number, then XINPUT_GAMEPAD.
#pragma pack(push, 1)
struct XInputStateLayout {
    DWORD packet;
    WORD buttons;
    BYTE leftTrigger;
    BYTE rightTrigger;
    SHORT leftX;
    SHORT leftY;
    SHORT rightX;
    SHORT rightY;
};
#pragma pack(pop)
static_assert(sizeof(XInputStateLayout) == 16, "XINPUT_STATE is a DWORD and a 12-byte XINPUT_GAMEPAD");

inline DWORD WINAPI HookedXInputGetState(DWORD index, void* state) {
    if (!S().padRead.exchange(true)) Log("isolated input: the game reads controllers through XInput");
    if (index >= static_cast<DWORD>(kPadCount) || !state) return ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard<std::mutex> lock(S().padLock);
    const PadState& pad = S().pads[index];
    if (!pad.connected) return ERROR_DEVICE_NOT_CONNECTED;
    S().padReads[index].fetch_add(1, std::memory_order_release);
    const XInputStateLayout out = {pad.packet, pad.buttons, pad.leftTrigger, pad.rightTrigger,
                                   pad.leftX, pad.leftY, pad.rightX, pad.rightY};
    std::memcpy(state, &out, sizeof(out));
    return ERROR_SUCCESS;
}

// XINPUT_CAPABILITIES: type, subtype, flags, then an XINPUT_GAMEPAD and an
// XINPUT_VIBRATION saying which controls and motors are there.
#pragma pack(push, 1)
struct XInputCapabilitiesLayout {
    BYTE type;
    BYTE subType;
    WORD flags;
    WORD buttons;
    BYTE leftTrigger;
    BYTE rightTrigger;
    SHORT leftX;
    SHORT leftY;
    SHORT rightX;
    SHORT rightY;
    WORD leftMotor;
    WORD rightMotor;
};
#pragma pack(pop)
static_assert(sizeof(XInputCapabilitiesLayout) == 20, "XINPUT_CAPABILITIES is 4 bytes, an XINPUT_GAMEPAD and an XINPUT_VIBRATION");

// A game that sets a controller up only once XInput describes it (Wreckfest asks
// for its capabilities when a pad appears, and sets up none the call fails for)
// would otherwise take a script pad only while a real one sat at the same index.
inline DWORD WINAPI HookedXInputGetCapabilities(DWORD index, DWORD, void* capabilities) {
    if (index >= static_cast<DWORD>(kPadCount) || !capabilities) return ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard<std::mutex> lock(S().padLock);
    if (!S().pads[index].connected) return ERROR_DEVICE_NOT_CONNECTED;
    // XINPUT_DEVTYPE_GAMEPAD and XINPUT_DEVSUBTYPE_GAMEPAD, with every button, both
    // triggers, both sticks and both motors.
    const XInputCapabilitiesLayout out = {1, 1, 0, 0xF3FF, 0xFF, 0xFF, -64, -64, -64, -64, 0xFFFF, 0xFFFF};
    std::memcpy(capabilities, &out, sizeof(out));
    return ERROR_SUCCESS;
}

// Rumble goes nowhere: the real controller is not this pad.
inline DWORD WINAPI HookedXInputSetState(DWORD index, void*) {
    if (index >= static_cast<DWORD>(kPadCount)) return ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard<std::mutex> lock(S().padLock);
    return S().pads[index].connected ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
}

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
    // A console the process owns (a mod loader's log console) is not the game, and once it had
    // been taken for the game every script went to it.
    char className[32] = {};
    GetClassNameA(hwnd, className, sizeof(className));
    if (std::strcmp(className, "ConsoleWindowClass") == 0) return TRUE;
    const long area = (rect.right - rect.left) * (rect.bottom - rect.top);
    if (area > search.bestArea) {
        search.best = hwnd;
        search.bestArea = area;
    }
    return TRUE;
}

// The game's main window: the largest visible top-level window of this process that is not a console.
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

// The window this process registered to receive raw input of the event's kind, when it is
// not the game's main window. An input library can register a window of its own (Rewired
// does, in a Unity game), and Windows delivers a kind of raw input to one window per process.
inline HWND RawInputTarget(DWORD type) {
    const USHORT usage = type == RIM_TYPEKEYBOARD ? 0x06 : 0x02;
    RAWINPUTDEVICE devices[16];
    UINT count = 16;
    const UINT got = GetRegisteredRawInputDevices(devices, &count, sizeof(RAWINPUTDEVICE));
    if (got == static_cast<UINT>(-1)) return nullptr;
    for (UINT i = 0; i < got; ++i) {
        if (devices[i].usUsagePage == 0x01 && devices[i].usUsage == usage) return devices[i].hwndTarget;
    }
    return nullptr;
}

inline bool Post(const RAWINPUT& event) {
    const HWND window = S().window.load(std::memory_order_acquire);
    if (!window) return false;
    const unsigned slot = S().nextEvent.fetch_add(1, std::memory_order_acq_rel) % kEventSlots;
    S().events[slot] = event;
    const LPARAM handle = reinterpret_cast<LPARAM>(&S().events[slot]);
    const HWND target = RawInputTarget(event.header.dwType);
    if (target && target != window) {
        static std::atomic<bool> said{false};
        if (!said.exchange(true)) Log("isolated input: raw input is registered to a window other than the game's, and goes to both");
        PostMessageW(target, WM_INPUT, RIM_INPUT, handle);
    }
    return PostMessageW(window, WM_INPUT, RIM_INPUT, handle) != 0;
}

static_assert(offsetof(DIDEVICEOBJECTDATA, dwOfs) == offsetof(DirectInputEvent, offset)
              && offsetof(DIDEVICEOBJECTDATA, dwData) == offsetof(DirectInputEvent, data)
              && offsetof(DIDEVICEOBJECTDATA, dwTimeStamp) == offsetof(DirectInputEvent, timeMs)
              && offsetof(DIDEVICEOBJECTDATA, dwSequence) == offsetof(DirectInputEvent, sequence),
              "DirectInputEvent is the head of DIDEVICEOBJECTDATA");
static_assert(offsetof(DIMOUSESTATE2, lX) == kMouseOffsetX && offsetof(DIMOUSESTATE2, lY) == kMouseOffsetY
              && offsetof(DIMOUSESTATE2, lZ) == kMouseOffsetZ && offsetof(DIMOUSESTATE2, rgbButtons) == kMouseOffsetButton0,
              "directinput_state.h names a mouse control by its offset in DIMOUSESTATE2");
static_assert(DIK_LSHIFT == kDikLeftShift, "directinput_state.h holds this Shift for a shifted character");

// IDirectInputDevice8 vtable slots, the same in the A and the W interface.
enum DirectInputMethod {
    kDiRelease,
    kDiGetCapabilities,
    kDiAcquire,
    kDiGetDeviceState,
    kDiGetDeviceData,
    kDiSetCooperativeLevel,
    kDiPoll,
    kDiSetDataFormat,
    kDiMethods
};
inline constexpr int kDiSlots[kDiMethods] = {2, 3, 7, 9, 10, 13, 25, 11};
inline constexpr int kDiGetObjectInfoSlot = 14;
// How far into a format whose size is not known DirectInput is asked for
// objects: a keyboard laid out four bytes a key reaches 1024.
inline constexpr DWORD kFormatProbeBytes = 2048;
// A and W interface, keyboard and mouse: at most this many addresses for one
// method.
inline constexpr int kDiVariants = 4;

using DiRelease_t = ULONG (STDMETHODCALLTYPE*)(void*);
using DiGetCapabilities_t = HRESULT (STDMETHODCALLTYPE*)(void*, LPDIDEVCAPS);
using DiAcquire_t = HRESULT (STDMETHODCALLTYPE*)(void*);
using DiGetDeviceState_t = HRESULT (STDMETHODCALLTYPE*)(void*, DWORD, LPVOID);
using DiGetDeviceData_t = HRESULT (STDMETHODCALLTYPE*)(void*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
using DiSetCooperativeLevel_t = HRESULT (STDMETHODCALLTYPE*)(void*, HWND, DWORD);
using DiSetDataFormat_t = HRESULT (STDMETHODCALLTYPE*)(void*, LPCDIDATAFORMAT);
using DiGetObjectInfo_t = HRESULT (STDMETHODCALLTYPE*)(void*, void*, DWORD, DWORD);
using DirectInput8Create_t = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using DiCreateDevice_t = HRESULT (STDMETHODCALLTYPE*)(void*, REFGUID, void**, LPUNKNOWN);

// Spelled out here so that a mod needs neither dxguid.lib nor dinput8.lib.
inline constexpr GUID kIidDirectInput8A = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
inline constexpr GUID kIidDirectInput8W = {0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
inline constexpr GUID kGuidSysMouse = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
inline constexpr GUID kGuidSysKeyboard = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
inline constexpr GUID kGuidXAxis = {0xA36D02E0, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
inline constexpr GUID kGuidYAxis = {0xA36D02E1, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
inline constexpr GUID kGuidZAxis = {0xA36D02E2, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
inline constexpr GUID kGuidButton = {0xA36D02F0, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
inline constexpr GUID kGuidKey = {0x55728220, 0xD33C, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

struct DirectInputDevice {
    void* object = nullptr;
    // False for a device that is neither a keyboard nor a mouse, which is left
    // to DirectInput.
    bool synthetic = false;
    bool stateLogged = false;
    bool dataLogged = false;
    DirectInputReader reader;
    DirectInputFormat format;
};

struct DirectInput {
    std::mutex mutex;
    DirectInputState state;
    std::vector<DirectInputDevice> devices;
    bool hooked = false;
    bool attempted = false;
    void* targets[kDiMethods][kDiVariants] = {};
    void* originals[kDiMethods][kDiVariants] = {};
};

// Never destroyed: the game may release its devices after this module's
// statics have been torn down, and the Release detour still runs then.
inline DirectInput& D() {
    static DirectInput* const d = new DirectInput;
    return *d;
}

template <typename Function>
inline Function DiOriginal(DirectInputMethod method, int variant) {
    return reinterpret_cast<Function>(D().originals[method][variant]);
}

inline void** Vtable(void* object) { return *reinterpret_cast<void***>(object); }

inline const char* DiKindName(const DirectInputDevice& device) {
    return device.reader.kind == DirectInputKind::kKeyboard ? "keyboard" : "mouse";
}

inline DirectInputGuid ObjectGuid(const GUID& guid) {
    if (guid == kGuidKey) return DirectInputGuid::kKey;
    if (guid == kGuidXAxis) return DirectInputGuid::kXAxis;
    if (guid == kGuidYAxis) return DirectInputGuid::kYAxis;
    if (guid == kGuidZAxis) return DirectInputGuid::kZAxis;
    if (guid == kGuidButton) return DirectInputGuid::kButton;
    return DirectInputGuid::kOther;
}

// The device's data format as it stands, asked of DirectInput one offset at a
// time (GetObjectInfo with DIPH_BYOFFSET answers the object at an offset of the
// format set, where EnumObjects answers the device's own layout). Called without
// the lock. The A and the W interface take objects of different sizes, so the
// W size is tried first and the A size when DirectInput refuses it.
inline DirectInputFormat ReadDataFormat(void* object, DirectInputKind kind, DWORD bytes, DWORD dataSize) {
    const auto getObjectInfo = reinterpret_cast<DiGetObjectInfo_t>(Vtable(object)[kDiGetObjectInfoSlot]);
    DIDEVICEOBJECTINSTANCEW info = {};
    DWORD infoSize = sizeof(DIDEVICEOBJECTINSTANCEW);
    DirectInputFormat format;
    format.dataSize = dataSize;
    for (DWORD offset = 0; offset < bytes; ++offset) {
        info.dwSize = infoSize;
        HRESULT result = getObjectInfo(object, &info, offset, DIPH_BYOFFSET);
        if (result == DIERR_INVALIDPARAM && infoSize == sizeof(DIDEVICEOBJECTINSTANCEW)) {
            infoSize = sizeof(DIDEVICEOBJECTINSTANCEA);
            info.dwSize = infoSize;
            result = getObjectInfo(object, &info, offset, DIPH_BYOFFSET);
        }
        if (FAILED(result)) continue;
        format.Add(kind, offset, ObjectGuid(info.guidType), DIDFT_GETINSTANCE(info.dwType));
    }
    return format;
}

// The entry for a device, made the first time the device is seen. Called with
// `lock` held, and lets go of it while it asks DirectInput what the device is
// and what data format it has.
inline DirectInputDevice& DiDevice(void* object, std::unique_lock<std::mutex>& lock) {
    DirectInput& d = D();
    for (DirectInputDevice& device : d.devices) {
        if (device.object == object) return device;
    }
    lock.unlock();
    DIDEVCAPS caps = {};
    caps.dwSize = sizeof(caps);
    reinterpret_cast<DiGetCapabilities_t>(Vtable(object)[kDiSlots[kDiGetCapabilities]])(object, &caps);
    const BYTE type = GET_DIDEVICE_TYPE(caps.dwDevType);
    const bool synthetic = type == DI8DEVTYPE_KEYBOARD || type == DI8DEVTYPE_MOUSE;
    const DirectInputKind kind = type == DI8DEVTYPE_MOUSE ? DirectInputKind::kMouse : DirectInputKind::kKeyboard;
    DirectInputFormat format;
    if (synthetic) format = ReadDataFormat(object, kind, kFormatProbeBytes, 0);
    lock.lock();
    for (DirectInputDevice& device : d.devices) {
        if (device.object == object) return device;
    }
    DirectInputDevice device;
    device.object = object;
    device.synthetic = synthetic;
    device.reader = d.state.Open(kind);
    device.format = std::move(format);
    d.devices.push_back(std::move(device));
    return d.devices.back();
}

inline bool IsSyntheticDevice(void* object) {
    std::unique_lock<std::mutex> lock(D().mutex);
    return DiDevice(object, lock).synthetic;
}

inline void LogDeviceRead(const char* kind, const char* call, DWORD size) {
    char line[160];
    std::snprintf(line, sizeof(line), "isolated input: the game reads its DirectInput %s through %s (%lu bytes)", kind,
                  call, size);
    Log(line);
}

// True when the device is a keyboard or a mouse, and `result` is then the
// answer, in the data format the game set.
inline bool AnswerDeviceState(void* object, DWORD size, LPVOID data, HRESULT& result) {
    DirectInput& d = D();
    const char* logKind = nullptr;
    size_t objects = 0;
    {
        std::unique_lock<std::mutex> lock(d.mutex);
        DirectInputDevice& device = DiDevice(object, lock);
        if (!device.synthetic) return false;
        if (!device.stateLogged) {
            device.stateLogged = true;
            logKind = DiKindName(device);
            objects = device.format.entries.size();
        }
        auto* const out = static_cast<uint8_t*>(data);
        result = out != nullptr && d.state.FormattedState(device.reader, device.format, out, size) ? DI_OK
                                                                                                  : DIERR_INVALIDPARAM;
    }
    if (logKind) {
        LogDeviceRead(logKind, "GetDeviceState", size);
        char line[160];
        std::snprintf(line, sizeof(line), "isolated input: its data format holds %zu keys, buttons and axes it answers%s",
                      objects, FAILED(result) ? ", and that size is not the format's, so the read is refused" : "");
        Log(line);
    }
    return true;
}

inline bool AnswerDeviceData(void* object, DWORD objectSize, LPDIDEVICEOBJECTDATA data, LPDWORD inOut, DWORD flags,
                             HRESULT& result) {
    DirectInput& d = D();
    const char* logKind = nullptr;
    {
        std::unique_lock<std::mutex> lock(d.mutex);
        DirectInputDevice& device = DiDevice(object, lock);
        if (!device.synthetic) return false;
        if (!device.dataLogged) {
            device.dataLogged = true;
            logKind = DiKindName(device);
        }
        if (inOut == nullptr || objectSize < sizeof(DirectInputEvent)) {
            result = DIERR_INVALIDPARAM;
        } else {
            DirectInputEvent events[kDirectInputEventSlots];
            bool overflowed = false;
            const size_t count = d.state.ReadEvents(device.reader, data ? events : nullptr, *inOut,
                                                    (flags & DIGDD_PEEK) != 0, overflowed, &device.format);
            for (size_t i = 0; data && i < count; ++i) {
                BYTE* const at = reinterpret_cast<BYTE*>(data) + i * objectSize;
                std::memset(at, 0, objectSize);
                std::memcpy(at, &events[i], sizeof(DirectInputEvent));
            }
            *inOut = static_cast<DWORD>(count);
            result = overflowed ? DI_BUFFEROVERFLOW : DI_OK;
        }
    }
    if (logKind) LogDeviceRead(logKind, "GetDeviceData", objectSize);
    return true;
}

template <int Variant>
inline ULONG STDMETHODCALLTYPE HookedDiRelease(void* object) {
    const ULONG left = DiOriginal<DiRelease_t>(kDiRelease, Variant)(object);
    if (left != 0) return left;
    DirectInput& d = D();
    std::lock_guard<std::mutex> lock(d.mutex);
    for (size_t i = 0; i < d.devices.size(); ++i) {
        if (d.devices[i].object != object) continue;
        d.devices.erase(d.devices.begin() + static_cast<std::ptrdiff_t>(i));
        break;
    }
    return 0;
}

// The real keyboard and mouse are never acquired. The game is told it has the
// foreground, and so is DirectInput (GetForegroundWindow is detoured for every
// caller), so a real exclusive Acquire could succeed behind other windows and
// take the mouse from the person using the machine.
template <int Variant>
inline HRESULT STDMETHODCALLTYPE HookedDiAcquire(void* object) {
    if (IsSyntheticDevice(object)) return DI_OK;
    return DiOriginal<DiAcquire_t>(kDiAcquire, Variant)(object);
}

template <int Variant>
inline HRESULT STDMETHODCALLTYPE HookedDiPoll(void* object) {
    if (IsSyntheticDevice(object)) return DI_OK;
    return DiOriginal<DiAcquire_t>(kDiPoll, Variant)(object);
}

template <int Variant>
inline HRESULT STDMETHODCALLTYPE HookedDiGetDeviceState(void* object, DWORD size, LPVOID data) {
    HRESULT result = DI_OK;
    if (AnswerDeviceState(object, size, data, result)) return result;
    return DiOriginal<DiGetDeviceState_t>(kDiGetDeviceState, Variant)(object, size, data);
}

template <int Variant>
inline HRESULT STDMETHODCALLTYPE HookedDiGetDeviceData(void* object, DWORD objectSize, LPDIDEVICEOBJECTDATA data,
                                                       LPDWORD inOut, DWORD flags) {
    HRESULT result = DI_OK;
    if (AnswerDeviceData(object, objectSize, data, inOut, flags, result)) return result;
    return DiOriginal<DiGetDeviceData_t>(kDiGetDeviceData, Variant)(object, objectSize, data, inOut, flags);
}

// DirectInput refuses background with exclusive for a keyboard, so both
// devices are made non-exclusive.
template <int Variant>
inline HRESULT STDMETHODCALLTYPE HookedDiSetCooperativeLevel(void* object, HWND window, DWORD flags) {
    const auto original = DiOriginal<DiSetCooperativeLevel_t>(kDiSetCooperativeLevel, Variant);
    if (!IsSyntheticDevice(object)) return original(object, window, flags);
    const HRESULT result = original(object, window, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    char line[160];
    std::snprintf(line, sizeof(line),
                  "isolated input: DirectInput SetCooperativeLevel 0x%lX passed on as background, non-exclusive: 0x%08lX",
                  flags, static_cast<unsigned long>(result));
    Log(line);
    return result;
}

// Passed on, so DirectInput checks the format against the real device, and read
// back from DirectInput once it has taken it.
template <int Variant>
inline HRESULT STDMETHODCALLTYPE HookedDiSetDataFormat(void* object, LPCDIDATAFORMAT format) {
    const HRESULT result = DiOriginal<DiSetDataFormat_t>(kDiSetDataFormat, Variant)(object, format);
    if (FAILED(result) || !IsSyntheticDevice(object)) return result;
    DirectInput& d = D();
    DirectInputKind kind;
    {
        std::unique_lock<std::mutex> lock(d.mutex);
        kind = DiDevice(object, lock).reader.kind;
    }
    DirectInputFormat read = ReadDataFormat(object, kind, format->dwDataSize, format->dwDataSize);
    char line[200];
    std::snprintf(line, sizeof(line),
                  "isolated input: the game set its DirectInput %s data format: %lu bytes, %lu objects, %zu of them answered",
                  kind == DirectInputKind::kKeyboard ? "keyboard" : "mouse", format->dwDataSize, format->dwNumObjs,
                  read.entries.size());
    {
        std::unique_lock<std::mutex> lock(d.mutex);
        DiDevice(object, lock).format = std::move(read);
    }
    Log(line);
    return result;
}

template <int Variant>
inline void* DiDetour(DirectInputMethod method) {
    switch (method) {
        case kDiRelease:             return reinterpret_cast<void*>(&HookedDiRelease<Variant>);
        case kDiAcquire:             return reinterpret_cast<void*>(&HookedDiAcquire<Variant>);
        case kDiGetDeviceState:      return reinterpret_cast<void*>(&HookedDiGetDeviceState<Variant>);
        case kDiGetDeviceData:       return reinterpret_cast<void*>(&HookedDiGetDeviceData<Variant>);
        case kDiSetCooperativeLevel: return reinterpret_cast<void*>(&HookedDiSetCooperativeLevel<Variant>);
        case kDiPoll:                return reinterpret_cast<void*>(&HookedDiPoll<Variant>);
        case kDiSetDataFormat:       return reinterpret_cast<void*>(&HookedDiSetDataFormat<Variant>);
        default:                     return nullptr;
    }
}

inline void* DiDetour(DirectInputMethod method, int variant) {
    switch (variant) {
        case 0:  return DiDetour<0>(method);
        case 1:  return DiDetour<1>(method);
        case 2:  return DiDetour<2>(method);
        default: return DiDetour<3>(method);
    }
}

// Adds the vtables of a keyboard and a mouse device made from a throwaway
// IDirectInput8 of one interface. CreateDevice has the same slot and the same
// arguments in IDirectInput8A and IDirectInput8W.
inline bool CollectDeviceVtables(DirectInput8Create_t create, const GUID& interfaceId, std::vector<void**>& vtables) {
    void* directInput = nullptr;
    if (FAILED(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, interfaceId, &directInput, nullptr))) return false;
    bool ok = true;
    for (const GUID* guid : {&kGuidSysKeyboard, &kGuidSysMouse}) {
        void* device = nullptr;
        if (FAILED(reinterpret_cast<DiCreateDevice_t>(Vtable(directInput)[3])(directInput, *guid, &device, nullptr))) {
            ok = false;
            continue;
        }
        vtables.push_back(Vtable(device));
        reinterpret_cast<DiRelease_t>(Vtable(device)[kDiSlots[kDiRelease]])(device);
    }
    reinterpret_cast<DiRelease_t>(Vtable(directInput)[2])(directInput);
    return ok;
}

inline void RemoveDirectInputHooks() {
    for (auto& method : D().targets) {
        for (void*& target : method) {
            if (!target) continue;
            MH_DisableHook(target);
            MH_RemoveHook(target);
            target = nullptr;
        }
    }
}

// Detours the device methods once dinput8.dll is in the process. Run from the
// command-file thread, never from StartIsolatedInput: making a DirectInput
// device is not something to do under the loader lock, and a game may load
// dinput8.dll after the mod.
inline void InstallDirectInput() {
    DirectInput& d = D();
    if (d.attempted) return;
    const HMODULE module = GetModuleHandleW(L"dinput8.dll");
    if (!module) return;
    d.attempted = true;

    const auto create = reinterpret_cast<DirectInput8Create_t>(
        reinterpret_cast<void*>(GetProcAddress(module, "DirectInput8Create")));
    std::vector<void**> vtables;
    if (!create || !CollectDeviceVtables(create, kIidDirectInput8A, vtables)
        || !CollectDeviceVtables(create, kIidDirectInput8W, vtables)) {
        Log("isolated input: could not make DirectInput devices to find their vtable, so DirectInput is not detoured");
        return;
    }

    int detours = 0;
    for (int method = 0; method < kDiMethods; ++method) {
        if (method == kDiGetCapabilities) continue;
        int variants = 0;
        for (void** vtable : vtables) {
            void* const target = vtable[kDiSlots[method]];
            bool known = false;
            for (int i = 0; i < variants; ++i) known = known || d.targets[method][i] == target;
            if (known) continue;
            const bool created = MH_CreateHook(target, DiDetour(static_cast<DirectInputMethod>(method), variants),
                                               &d.originals[method][variants]) == MH_OK;
            if (created) d.targets[method][variants++] = target;
            if (!created || MH_EnableHook(target) != MH_OK) {
                Log("isolated input: could not hook a DirectInput device method, so DirectInput is not detoured");
                RemoveDirectInputHooks();
                return;
            }
            ++detours;
        }
    }
    {
        std::lock_guard<std::mutex> lock(d.mutex);
        d.hooked = true;
    }
    char line[200];
    std::snprintf(line, sizeof(line),
                  "isolated input: DirectInput keyboard and mouse devices answer from the command file (%d detours over %zu device vtables)",
                  detours, vtables.size());
    Log(line);
}

// False only when the game reads DirectInput and the key has no DirectInput
// code.
inline bool SendDirectInputKey(int dik, bool down) {
    DirectInput& d = D();
    std::lock_guard<std::mutex> lock(d.mutex);
    return d.state.Key(dik, down, GetTickCount()) || !d.hooked;
}

inline void SendDirectInputButton(MouseButton button, bool down) {
    DirectInput& d = D();
    std::lock_guard<std::mutex> lock(d.mutex);
    d.state.Button(button, down, GetTickCount());
}

inline void SendDirectInputMove(int dx, int dy) {
    DirectInput& d = D();
    std::lock_guard<std::mutex> lock(d.mutex);
    d.state.Move(dx, dy, GetTickCount());
}

// GameInput, the GDK's input API: GameInputRedist.dll from the Microsoft.GameInput NuGet package,
// or the inbox GameInput.dll. GameInput reads the devices itself and does not stand down for a
// game in the background: the real mouse turned Minecraft Bedrock's view from behind other windows.
//
// One object answers every API version, each through a vtable of its own, and so does each
// reading. Version 2's are answered from the script by writing over their vtable slots, so the
// game's own object is covered whenever it was made. Every other version is given no readings at
// all, and GameInputInitialize refuses a version it does not know.
inline constexpr int kGameInputVersions = 5;
inline constexpr int kGameInputAnswered = 2;
inline constexpr GUID kIidGameInput[kGameInputVersions] = {
    {0x11BE2A7E, 0x4254, 0x445A, {0x9C, 0x09, 0xFF, 0xC4, 0x0F, 0x00, 0x69, 0x18}},
    {0x40FFB7E4, 0x6150, 0x407A, {0xB4, 0x39, 0x13, 0x2B, 0xAD, 0xC0, 0x8D, 0x2D}},
    {0xBBAA66D2, 0x837A, 0x40F7, {0xA3, 0x03, 0x91, 0x7D, 0x50, 0x09, 0x55, 0xF4}},
    {0x20EFC1C7, 0x5D9A, 0x43BA, {0xB2, 0x6F, 0xB8, 0x07, 0xFA, 0x48, 0x60, 0x9C}},
    {0xF2DAAE4C, 0x4D21, 0x4250, {0x89, 0x53, 0x4F, 0x79, 0x1E, 0x62, 0x79, 0xAB}},
};
inline constexpr HRESULT kGameInputReadingNotFound = static_cast<HRESULT>(0x838A0003L);
inline constexpr uint32_t kGameInputKindKeyboard = 0x10;
inline constexpr uint32_t kGameInputKindMouse = 0x20;
inline constexpr uint32_t kGameInputKindGamepad = 0x40000;

// IGameInput slots. Version 0 has GetTemporalReading at 7 and RegisterReadingCallback at 8.
enum GameInputMethod { kGiGetCurrentReading, kGiGetNextReading, kGiGetPreviousReading, kGiRegisterReadingCallback, kGiMethods };
inline constexpr int kGiSlots[kGiMethods] = {4, 5, 6, 7};
inline constexpr int kGiV0GetTemporalReading = 7;
inline constexpr int kGiV0RegisterReadingCallback = 8;

// Version 2's IGameInputReading slots, GetInputKind and then GetControllerAxisCount to
// GetUiNavigationState, the ones answered.
enum GameInputReadingSlot {
    kGrGetInputKind = 3,
    kGrAxisCount = 6, kGrAxisState, kGrButtonCount, kGrButtonState, kGrSwitchCount, kGrSwitchState,
    kGrKeyCount, kGrKeyState, kGrMouseState, kGrSensorsState, kGrArcadeStickState, kGrFlightStickState,
    kGrGamepadState, kGrRacingWheelState, kGrUiNavigationState, kGrSlotEnd
};
inline constexpr int kGrMaxVtables = 16;

using GameInputInitialize_t = HRESULT (WINAPI*)(REFIID, void**);
using GameInputCreate_t = HRESULT (WINAPI*)(void**);
using GiRelease_t = ULONG (STDMETHODCALLTYPE*)(void*);
using GiGetCurrentReading_t = HRESULT (STDMETHODCALLTYPE*)(void*, uint32_t, void*, void**);
using GiGetOtherReading_t = HRESULT (STDMETHODCALLTYPE*)(void*, void*, uint32_t, void*, void**);
using GiReadingCallback_t = void (CALLBACK*)(uint64_t, void*, void*);
using GiRegisterReadingCallback_t = HRESULT (STDMETHODCALLTYPE*)(void*, void*, uint32_t, void*, GiReadingCallback_t, uint64_t*);
using GrGetInputKind_t = uint32_t (STDMETHODCALLTYPE*)(void*);
using GrCount_t = uint32_t (STDMETHODCALLTYPE*)(void*);
using GrKeyState_t = uint32_t (STDMETHODCALLTYPE*)(void*, uint32_t, GameInputKeyState*);
using GrMouseState_t = bool (STDMETHODCALLTYPE*)(void*, GameInputMouseState*);
using GrGamepadState_t = bool (STDMETHODCALLTYPE*)(void*, GameInputGamepadState*);

struct GameInputReadingVtable {
    void** vtable = nullptr;
    void* originals[kGrSlotEnd] = {};
};

struct GameInput {
    std::mutex mutex;
    GameInputMouse mouse;
    bool haveRealMouse = false;
    int64_t realMouseX = 0;
    int64_t realMouseY = 0;

    bool redistAttempted = false;
    bool inboxAttempted = false;
    void* originals[kGiMethods] = {};
    GameInputInitialize_t origInitialize = nullptr;

    std::mutex patchMutex;
    GameInputReadingVtable readings[kGrMaxVtables];
    std::atomic<int> readingCount{0};

    std::atomic<bool> calledLogged[kGiMethods] = {};
    std::atomic<bool> refusedLogged[kGameInputVersions] = {};
    std::atomic<bool> realMouseLogged{false};
    std::atomic<bool> realKeysLogged{false};
    std::atomic<bool> realPadLogged{false};
    std::atomic<bool> controllerLogged{false};
};

// Never destroyed: a game reads GameInput until its last thread stops.
inline GameInput& G() {
    static GameInput* const g = new GameInput;
    return *g;
}

inline void LogOnce(std::atomic<bool>& said, const char* text) {
    if (!said.exchange(true, std::memory_order_acq_rel)) Log(text);
}

// A pointer-sized write, so a call through the slot sees the old function or the new one.
inline bool WriteSlot(void** vtable, int slot, void* detour) {
    DWORD protection = 0;
    if (!VirtualProtect(&vtable[slot], sizeof(void*), PAGE_READWRITE, &protection)) return false;
    InterlockedExchangePointer(&vtable[slot], detour);
    VirtualProtect(&vtable[slot], sizeof(void*), protection, &protection);
    return true;
}

inline void* ReadingOriginal(void* reading, GameInputReadingSlot slot) {
    void** const vtable = Vtable(reading);
    const int count = G().readingCount.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i) {
        if (G().readings[i].vtable == vtable) return G().readings[i].originals[slot];
    }
    // Only reached through a vtable that was written over, and that one is listed first.
    std::abort();
}

inline bool IsKeyboardReading(void* reading) {
    return (reinterpret_cast<GrGetInputKind_t>(Vtable(reading)[kGrGetInputKind])(reading) & kGameInputKindKeyboard) != 0;
}

inline uint32_t STDMETHODCALLTYPE GrNoControllerCount(void*) {
    LogOnce(G().controllerLogged, "isolated input: the game reads GameInput controller axes, buttons or switches; it is given none");
    return 0;
}

inline uint32_t STDMETHODCALLTYPE GrNoControllerState(void*, uint32_t, void*) { return 0; }

inline bool STDMETHODCALLTYPE GrNoState(void*, void*) { return false; }

inline size_t ScriptKeys(uint8_t* keys, size_t capacity) {
    bool down[256];
    for (int vk = 0; vk < 256; ++vk) down[vk] = S().keyDown[vk].load(std::memory_order_acquire);
    return GameInputHeldKeys(down, keys, capacity);
}

inline uint32_t STDMETHODCALLTYPE GrKeyCount(void* reading) {
    const uint32_t real = reinterpret_cast<GrCount_t>(ReadingOriginal(reading, kGrKeyCount))(reading);
    if (!IsKeyboardReading(reading)) return real;
    if (real != 0) {
        LogOnce(G().realKeysLogged, "isolated input: the game was handed a GameInput reading of the real keyboard; it reads the script's keys in its place");
    }
    return static_cast<uint32_t>(ScriptKeys(nullptr, 0));
}

inline uint32_t STDMETHODCALLTYPE GrKeyState(void* reading, uint32_t capacity, GameInputKeyState* states) {
    if (!IsKeyboardReading(reading)) {
        return reinterpret_cast<GrKeyState_t>(ReadingOriginal(reading, kGrKeyState))(reading, capacity, states);
    }
    uint8_t keys[256];
    const size_t held = ScriptKeys(keys, sizeof(keys));
    const uint32_t written = static_cast<uint32_t>(held < capacity ? held : capacity);
    for (uint32_t i = 0; i < written; ++i) {
        const UINT character = MapVirtualKeyW(keys[i], MAPVK_VK_TO_CHAR);
        states[i] = {MapVirtualKeyW(keys[i], MAPVK_VK_TO_VSC_EX), character & 0xFFFF, keys[i], (character & 0x80000000) != 0};
    }
    return written;
}

inline bool STDMETHODCALLTYPE GrMouseState(void* reading, GameInputMouseState* state) {
    GameInputMouseState real = {};
    if (!reinterpret_cast<GrMouseState_t>(ReadingOriginal(reading, kGrMouseState))(reading, &real)) return false;
    GameInput& g = G();
    bool realMoved = false;
    {
        std::lock_guard<std::mutex> lock(g.mutex);
        realMoved = real.buttons != 0 || (g.haveRealMouse && (real.positionX != g.realMouseX || real.positionY != g.realMouseY));
        g.haveRealMouse = true;
        g.realMouseX = real.positionX;
        g.realMouseY = real.positionY;
        *state = g.mouse.State();
    }
    if (realMoved) {
        LogOnce(g.realMouseLogged, "isolated input: the game was handed a GameInput reading of the real mouse moving or pressed; it reads the script's mouse in its place");
    }
    return true;
}

inline bool STDMETHODCALLTYPE GrGamepadState(void* reading, GameInputGamepadState* state) {
    GameInputGamepadState real = {};
    if (!reinterpret_cast<GrGamepadState_t>(ReadingOriginal(reading, kGrGamepadState))(reading, &real)) return false;
    if (real.buttons != 0 || real.leftTrigger != 0.0f || real.rightTrigger != 0.0f || real.leftThumbstickX != 0.0f
        || real.leftThumbstickY != 0.0f || real.rightThumbstickX != 0.0f || real.rightThumbstickY != 0.0f) {
        LogOnce(G().realPadLogged, "isolated input: the game was handed a GameInput reading of a real gamepad; it reads script pad 0 in its place");
    }
    std::lock_guard<std::mutex> lock(S().padLock);
    *state = GameInputPad(S().pads[0]);
    if (S().pads[0].connected) S().padReads[0].fetch_add(1, std::memory_order_release);
    return true;
}

inline void* ReadingDetour(int slot) {
    switch (slot) {
        case kGrAxisCount: case kGrButtonCount: case kGrSwitchCount:
            return reinterpret_cast<void*>(&GrNoControllerCount);
        case kGrAxisState: case kGrButtonState: case kGrSwitchState:
            return reinterpret_cast<void*>(&GrNoControllerState);
        case kGrKeyCount:   return reinterpret_cast<void*>(&GrKeyCount);
        case kGrKeyState:   return reinterpret_cast<void*>(&GrKeyState);
        case kGrMouseState: return reinterpret_cast<void*>(&GrMouseState);
        case kGrGamepadState: return reinterpret_cast<void*>(&GrGamepadState);
        default:            return reinterpret_cast<void*>(&GrNoState);
    }
}

// Writes the script's answers over a version 2 reading's vtable, the first time a reading of
// that vtable is seen. Every reading of the vtable, the game's included, then answers from the
// script.
inline void AnswerReadingsOf(void* reading) {
    if (!reading) return;
    void** const vtable = Vtable(reading);
    GameInput& g = G();
    for (int i = 0; i < g.readingCount.load(std::memory_order_acquire); ++i) {
        if (g.readings[i].vtable == vtable) return;
    }
    std::lock_guard<std::mutex> lock(g.patchMutex);
    const int count = g.readingCount.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i) {
        if (g.readings[i].vtable == vtable) return;
    }
    if (count == kGrMaxVtables) {
        Log("isolated input: more GameInput reading vtables than isolated input keeps; stopping the game rather than let one through");
        std::abort();
    }
    GameInputReadingVtable& entry = g.readings[count];
    entry.vtable = vtable;
    for (int slot = kGrAxisCount; slot < kGrSlotEnd; ++slot) entry.originals[slot] = vtable[slot];
    g.readingCount.store(count + 1, std::memory_order_release);
    for (int slot = kGrAxisCount; slot < kGrSlotEnd; ++slot) {
        if (!WriteSlot(vtable, slot, ReadingDetour(slot))) {
            Log("isolated input: could not write over a GameInput reading's vtable; stopping the game rather than let the real devices through");
            std::abort();
        }
    }
    Log("isolated input: GameInput readings answer from the command file");
}

inline void LogGameInputCall(GameInputMethod method, uint32_t kind) {
    if (G().calledLogged[method].exchange(true, std::memory_order_acq_rel)) return;
    static constexpr const char* kNames[kGiMethods] = {"GetCurrentReading", "GetNextReading", "GetPreviousReading",
                                                       "RegisterReadingCallback"};
    char line[160];
    std::snprintf(line, sizeof(line), "isolated input: the game reads GameInput through %s (kinds 0x%X)", kNames[method], kind);
    Log(line);
}

inline HRESULT STDMETHODCALLTYPE GiGetCurrentReading(void* self, uint32_t kind, void* device, void** reading) {
    LogGameInputCall(kGiGetCurrentReading, kind);
    const HRESULT result = reinterpret_cast<GiGetCurrentReading_t>(G().originals[kGiGetCurrentReading])(self, kind, device, reading);
    if (SUCCEEDED(result)) AnswerReadingsOf(*reading);
    return result;
}

template <GameInputMethod Method>
inline HRESULT STDMETHODCALLTYPE GiGetOtherReading(void* self, void* reference, uint32_t kind, void* device, void** reading) {
    LogGameInputCall(Method, kind);
    const HRESULT result = reinterpret_cast<GiGetOtherReading_t>(G().originals[Method])(self, reference, kind, device, reading);
    if (SUCCEEDED(result)) AnswerReadingsOf(*reading);
    return result;
}

struct GameInputCallback {
    GiReadingCallback_t function;
    void* context;
};

inline void CALLBACK GiOnReading(uint64_t token, void* context, void* reading) {
    const auto* callback = static_cast<const GameInputCallback*>(context);
    AnswerReadingsOf(reading);
    callback->function(token, callback->context, reading);
}

// The game's callback is called through ours, which first makes sure the reading answers from
// the script. Ours is never freed: GameInput can still be calling it as the game unregisters.
inline HRESULT STDMETHODCALLTYPE GiRegisterReadingCallback(void* self, void* device, uint32_t kind, void* context,
                                                           GiReadingCallback_t function, uint64_t* token) {
    LogGameInputCall(kGiRegisterReadingCallback, kind);
    auto* const callback = new GameInputCallback{function, context};
    return reinterpret_cast<GiRegisterReadingCallback_t>(G().originals[kGiRegisterReadingCallback])(
        self, device, kind, callback, &GiOnReading, token);
}

template <int Version>
inline void LogRefused() {
    if (G().refusedLogged[Version].exchange(true, std::memory_order_acq_rel)) return;
    char line[160];
    std::snprintf(line, sizeof(line),
                  "isolated input: the game reads GameInput API version %d, which isolated input does not answer; it is given no readings",
                  Version);
    Log(line);
}

template <int Version>
inline HRESULT STDMETHODCALLTYPE GiRefuseCurrentReading(void*, uint32_t, void*, void** reading) {
    LogRefused<Version>();
    if (reading) *reading = nullptr;
    return kGameInputReadingNotFound;
}

template <int Version>
inline HRESULT STDMETHODCALLTYPE GiRefuseOtherReading(void*, void*, uint32_t, void*, void** reading) {
    LogRefused<Version>();
    if (reading) *reading = nullptr;
    return kGameInputReadingNotFound;
}

inline HRESULT STDMETHODCALLTYPE GiRefuseTemporalReading(void*, uint64_t, void*, void** reading) {
    LogRefused<0>();
    if (reading) *reading = nullptr;
    return kGameInputReadingNotFound;
}

template <int Version>
inline HRESULT STDMETHODCALLTYPE GiRefuseReadingCallback(void*, void*, uint32_t, void*, void*, uint64_t* token) {
    LogRefused<Version>();
    if (token) *token = 0;
    return E_NOTIMPL;
}

inline HRESULT STDMETHODCALLTYPE GiRefuseReadingCallbackV0(void*, void*, uint32_t, float, void*, void*, uint64_t* token) {
    LogRefused<0>();
    if (token) *token = 0;
    return E_NOTIMPL;
}

template <int Version>
inline bool RefuseGameInputVersion(void** vtable) {
    bool ok = WriteSlot(vtable, kGiSlots[kGiGetCurrentReading], reinterpret_cast<void*>(&GiRefuseCurrentReading<Version>))
              && WriteSlot(vtable, kGiSlots[kGiGetNextReading], reinterpret_cast<void*>(&GiRefuseOtherReading<Version>))
              && WriteSlot(vtable, kGiSlots[kGiGetPreviousReading], reinterpret_cast<void*>(&GiRefuseOtherReading<Version>));
    if constexpr (Version == 0) {
        ok = ok && WriteSlot(vtable, kGiV0GetTemporalReading, reinterpret_cast<void*>(&GiRefuseTemporalReading))
             && WriteSlot(vtable, kGiV0RegisterReadingCallback, reinterpret_cast<void*>(&GiRefuseReadingCallbackV0));
    } else {
        ok = ok && WriteSlot(vtable, kGiSlots[kGiRegisterReadingCallback], reinterpret_cast<void*>(&GiRefuseReadingCallback<Version>));
    }
    return ok;
}

inline bool AnswerGameInputVersion2(void** vtable) {
    GameInput& g = G();
    void* const detours[kGiMethods] = {reinterpret_cast<void*>(&GiGetCurrentReading),
                                       reinterpret_cast<void*>(&GiGetOtherReading<kGiGetNextReading>),
                                       reinterpret_cast<void*>(&GiGetOtherReading<kGiGetPreviousReading>),
                                       reinterpret_cast<void*>(&GiRegisterReadingCallback)};
    for (int method = 0; method < kGiMethods; ++method) {
        g.originals[method] = vtable[kGiSlots[method]];
        if (!WriteSlot(vtable, kGiSlots[method], detours[method])) return false;
    }
    return true;
}

// True when the vtable already answers through isolated input: the redist hands the inbox
// GameInput.dll its own object.
inline bool GameInputVtableTaken(void** vtable) {
    const void* const slot = vtable[kGiSlots[kGiGetCurrentReading]];
    return slot == reinterpret_cast<void*>(&GiGetCurrentReading) || slot == reinterpret_cast<void*>(&GiRefuseCurrentReading<0>)
           || slot == reinterpret_cast<void*>(&GiRefuseCurrentReading<1>) || slot == reinterpret_cast<void*>(&GiRefuseCurrentReading<3>)
           || slot == reinterpret_cast<void*>(&GiRefuseCurrentReading<4>);
}

inline bool WrapGameInputVersion(int version, void** vtable) {
    if (GameInputVtableTaken(vtable)) return true;
    switch (version) {
        case 0:  return RefuseGameInputVersion<0>(vtable);
        case 1:  return RefuseGameInputVersion<1>(vtable);
        case 2:  return AnswerGameInputVersion2(vtable);
        case 3:  return RefuseGameInputVersion<3>(vtable);
        default: return RefuseGameInputVersion<4>(vtable);
    }
}

inline HRESULT WINAPI HookedGameInputInitialize(REFIID riid, void** gameInput) {
    for (const GUID& known : kIidGameInput) {
        if (IsEqualGUID(riid, known)) return G().origInitialize(riid, gameInput);
    }
    char line[200];
    std::snprintf(line, sizeof(line),
                  "isolated input: the game asked for a GameInput API version isolated input does not know "
                  "({%08lX-%04X-%04X-...}); it is refused",
                  riid.Data1, riid.Data2, riid.Data3);
    Log(line);
    if (gameInput) *gameInput = nullptr;
    return E_NOINTERFACE;
}

// Makes a throwaway reading of each kind the script answers, so readings that reach the game
// through a callback it registered before isolated input started answer from the script too.
inline void AnswerCurrentReadings(void* gameInput) {
    const auto current = reinterpret_cast<GiGetCurrentReading_t>(G().originals[kGiGetCurrentReading]);
    for (const uint32_t kind : {kGameInputKindKeyboard, kGameInputKindMouse, kGameInputKindGamepad}) {
        void* reading = nullptr;
        if (FAILED(current(gameInput, kind, nullptr, &reading))) continue;
        AnswerReadingsOf(reading);
        reinterpret_cast<GiRelease_t>(Vtable(reading)[2])(reading);
    }
}

// False when a vtable could not be written over, which leaves the game reading the real devices.
inline bool WrapGameInputModule(const wchar_t* name, HMODULE module) {
    const auto initialize = reinterpret_cast<GameInputInitialize_t>(reinterpret_cast<void*>(GetProcAddress(module, "GameInputInitialize")));
    if (!initialize) {
        const auto create = reinterpret_cast<GameInputCreate_t>(reinterpret_cast<void*>(GetProcAddress(module, "GameInputCreate")));
        void* gameInput = nullptr;
        if (!create || FAILED(create(&gameInput))) {
            Log("isolated input: GameInput.dll is loaded and GameInputCreate made nothing, so there is nothing of it to answer");
            return true;
        }
        const bool wrapped = WrapGameInputVersion(0, Vtable(gameInput));
        reinterpret_cast<GiRelease_t>(Vtable(gameInput)[2])(gameInput);
        return wrapped;
    }
    for (int version = 0; version < kGameInputVersions; ++version) {
        void* gameInput = nullptr;
        if (FAILED(initialize(kIidGameInput[version], &gameInput))) continue;
        const bool wrapped = WrapGameInputVersion(version, Vtable(gameInput));
        if (wrapped && version == kGameInputAnswered) AnswerCurrentReadings(gameInput);
        reinterpret_cast<GiRelease_t>(Vtable(gameInput)[2])(gameInput);
        if (!wrapped) return false;
    }
    void* const target = reinterpret_cast<void*>(initialize);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&HookedGameInputInitialize), reinterpret_cast<void**>(&G().origInitialize)) != MH_OK
        || MH_EnableHook(target) != MH_OK) {
        return false;
    }
    char line[200];
    std::snprintf(line, sizeof(line),
                  "isolated input: GameInput (%ls) answers version 2 from the command file and gives every other version no readings",
                  name);
    Log(line);
    return true;
}

// Run from the command-file thread, as InstallDirectInput is: a game loads GameInput when it
// starts reading input, which can be after the mod. GameInput is never loaded by this header.
inline void InstallGameInput() {
    GameInput& g = G();
    struct Module { const wchar_t* name; bool& attempted; };
    for (const Module& candidate : {Module{L"GameInputRedist.dll", g.redistAttempted}, Module{L"GameInput.dll", g.inboxAttempted}}) {
        if (candidate.attempted) continue;
        const HMODULE module = GetModuleHandleW(candidate.name);
        if (!module) continue;
        candidate.attempted = true;
        if (!WrapGameInputModule(candidate.name, module)) {
            Log("isolated input: GameInput could not be wrapped; stopping the game rather than let the real devices through");
            std::abort();
        }
    }
}

inline void SendGameInputMove(int dx, int dy) {
    std::lock_guard<std::mutex> lock(G().mutex);
    G().mouse.Move(dx, dy);
}

inline void SendGameInputButton(MouseButton button, bool down) {
    std::lock_guard<std::mutex> lock(G().mutex);
    G().mouse.Button(button, down);
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

// The left-hand key a script's Ctrl, Shift or Alt stands for, or 0.
inline int LeftHandModifier(int vk) {
    switch (vk) {
        case VK_SHIFT: return VK_LSHIFT;
        case VK_CONTROL: return VK_LCONTROL;
        case VK_MENU: return VK_LMENU;
        default: return 0;
    }
}

// The state GetAsyncKeyState, GetKeyState and GetKeyboardState answer from.
inline void HoldKeyState(int vk, bool down) {
    const bool wasDown = S().keyDown[vk].exchange(down, std::memory_order_acq_rel);
    if (down) S().keyPressedSinceRead[vk].store(true, std::memory_order_release);
    // HoldIsolatedKey can press from another thread than the command file's, so the flip is one exchange.
    if (down && !wasDown) {
        bool toggled = S().keyToggled[vk].load(std::memory_order_relaxed);
        while (!S().keyToggled[vk].compare_exchange_weak(toggled, !toggled, std::memory_order_acq_rel)) {
        }
    }
}

// Windows reports a held mouse button through the key state functions as well, under
// VK_LBUTTON, VK_RBUTTON and VK_MBUTTON. A Plague Tale: Innocence raised its sling for a
// scripted right button only once this did too.
inline void HoldButtonKeyState(MouseButton button, bool down) {
    HoldKeyState(button == MouseButton::kLeft ? VK_LBUTTON : button == MouseButton::kRight ? VK_RBUTTON : VK_MBUTTON, down);
}

inline bool SendRawKey(int vk, bool down) {
    HoldKeyState(vk, down);
    // Windows reports a held Ctrl, Shift or Alt under the key's own side as well. Unity's
    // Input.GetKey(KeyCode.LeftControl) stayed false for a scripted Ctrl until this did too.
    if (const int left = LeftHandModifier(vk)) S().keyDown[left].store(down, std::memory_order_release);

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

// True when the game registered its keyboard for raw input with RIDEV_NOLEGACY:
// Windows then sends it no WM_KEYDOWN or WM_KEYUP, so the script sends none
// either.
inline bool LegacyKeyMessagesOff() {
    RAWINPUTDEVICE devices[16];
    UINT count = 16;
    const UINT got = GetRegisteredRawInputDevices(devices, &count, sizeof(RAWINPUTDEVICE));
    if (got == static_cast<UINT>(-1)) return false;
    for (UINT i = 0; i < got; ++i) {
        if (devices[i].usUsagePage == 0x01 && devices[i].usUsage == 0x06 &&
            (devices[i].dwFlags & RIDEV_NOLEGACY) == RIDEV_NOLEGACY)
            return true;
    }
    return false;
}

// The key as the window message Windows sends beside raw input. A game that
// takes its keys from its window procedure (Unreal Engine 4 does) reads nothing
// else.
inline void PostKeyMessage(int vk, bool down) {
    if (LegacyKeyMessagesOff()) return;
    const LPARAM scan = static_cast<LPARAM>(MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC) & 0xFF) << 16;
    const LPARAM extended = IsExtendedKey(vk) ? (static_cast<LPARAM>(1) << 24) : 0;
    const LPARAM released = down ? 0 : static_cast<LPARAM>(0xC0000000);
    PostMessageW(S().window.load(std::memory_order_acquire), down ? WM_KEYDOWN : WM_KEYUP,
                 static_cast<WPARAM>(vk), 1 | scan | extended | released);
}

// The game's top-level windows, shown or not: a game can have more than the one
// isolated input takes for its window.
inline std::vector<HWND> GameWindows() {
    std::vector<HWND> windows;
    EnumWindows(
        [](HWND window, LPARAM context) -> BOOL {
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            if (pid == GetCurrentProcessId()) reinterpret_cast<std::vector<HWND>*>(context)->push_back(window);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&windows));
    return windows;
}

// A `post` key: the window messages a keypress makes, to every top-level window
// of the game, and nothing to raw input, DirectInput or the key state. The title
// screens of Dishonored, Mirror's Edge and Arkham City answered a Return posted
// this way and no `tap` (2026-11-02). A letter types its lower-case character.
inline bool PostKey(int vk, bool down) {
    const LPARAM scan = static_cast<LPARAM>(MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC) & 0xFF) << 16;
    const LPARAM extended = IsExtendedKey(vk) ? (static_cast<LPARAM>(1) << 24) : 0;
    const LPARAM lParam = 1 | scan | extended | (down ? 0 : static_cast<LPARAM>(0xC0000000));
    UINT character = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_CHAR);
    if (character & 0x80000000) character = 0;
    if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
    const std::vector<HWND> windows = GameWindows();
    bool ok = !windows.empty();
    for (const HWND window : windows) {
        ok = PostMessageW(window, down ? WM_KEYDOWN : WM_KEYUP, static_cast<WPARAM>(vk), lParam) != 0 && ok;
        if (down && character) ok = PostMessageW(window, WM_CHAR, character, lParam) != 0 && ok;
    }
    return ok;
}

// `activate`: every top-level window of the game is told it has the foreground,
// as the window isolated input takes for the game is when it is found. A
// minimized Dishonored or Mirror's Edge window drew nothing until
// WM_ACTIVATEAPP and WM_ACTIVATE were posted to it (2026-11-02).
inline bool PostActivate() {
    const std::vector<HWND> windows = GameWindows();
    bool ok = !windows.empty();
    for (const HWND window : windows) {
        ok = PostMessageW(window, WM_ACTIVATEAPP, TRUE, 0) != 0 && ok;
        ok = PostMessageW(window, WM_NCACTIVATE, TRUE, 0) != 0 && ok;
        ok = PostMessageW(window, WM_ACTIVATE, WA_ACTIVE, 0) != 0 && ok;
        ok = PostMessageW(window, WM_SETFOCUS, 0, 0) != 0 && ok;
    }
    return ok;
}

inline bool SendKey(int vk, bool down) {
    const bool directInput = SendDirectInputKey(VkToDik(vk), down);
    PostKeyMessage(vk, down);
    return SendRawKey(vk, down) && directInput;
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

// The window messages of a cursor at the script's point: where it is, and a
// button going down or up there.
inline LPARAM CursorPoint() {
    return MAKELPARAM(S().cursorX.load(std::memory_order_relaxed), S().cursorY.load(std::memory_order_relaxed));
}

inline bool PlaceCursor(int x, int y) {
    const HWND window = S().window.load(std::memory_order_acquire);
    if (!window) return false;
    S().cursorX.store(x, std::memory_order_relaxed);
    S().cursorY.store(y, std::memory_order_relaxed);
    S().cursorPlaced.store(true, std::memory_order_release);
    return PostMessageW(window, WM_MOUSEMOVE, S().buttonsHeld.load(std::memory_order_relaxed), CursorPoint()) != 0;
}

// Nothing until a `cursor` command has placed the cursor.
inline void PostCursorButton(MouseButton button, bool down) {
    if (!S().cursorPlaced.load(std::memory_order_acquire)) return;
    const unsigned key = button == MouseButton::kLeft ? MK_LBUTTON : button == MouseButton::kRight ? MK_RBUTTON : MK_MBUTTON;
    const UINT message = button == MouseButton::kLeft    ? (down ? WM_LBUTTONDOWN : WM_LBUTTONUP)
                         : button == MouseButton::kRight ? (down ? WM_RBUTTONDOWN : WM_RBUTTONUP)
                                                         : (down ? WM_MBUTTONDOWN : WM_MBUTTONUP);
    const unsigned held = down ? S().buttonsHeld.fetch_or(key, std::memory_order_acq_rel) | key
                               : S().buttonsHeld.fetch_and(~key, std::memory_order_acq_rel) & ~key;
    PostMessageW(S().window.load(std::memory_order_acquire), message, held, CursorPoint());
}

inline USHORT ButtonFlag(MouseButton button, bool down) {
    switch (button) {
        case MouseButton::kLeft:  return down ? RI_MOUSE_LEFT_BUTTON_DOWN : RI_MOUSE_LEFT_BUTTON_UP;
        case MouseButton::kRight: return down ? RI_MOUSE_RIGHT_BUTTON_DOWN : RI_MOUSE_RIGHT_BUTTON_UP;
        default:                  return down ? RI_MOUSE_MIDDLE_BUTTON_DOWN : RI_MOUSE_MIDDLE_BUTTON_UP;
    }
}

// One character as the key presses that type it on the current layout, and as
// the WM_CHAR a text field reads. A DirectInput keyboard gets the key that
// types it on a US keyboard instead: a game that reads key codes turns them
// into characters itself, whatever layout Windows has.
inline bool SendCharacter(char character) {
    const SHORT scan = VkKeyScanA(character);
    if (scan == -1) return false;
    const int vk = scan & 0xFF;
    const bool shift = (scan & 0x100) != 0;
    const DirectInputKeyStroke stroke = UsKeyStroke(character);
    bool ok = true;
    if (shift) ok = SendRawKey(VK_SHIFT, true) && ok;
    if (stroke.shift) SendDirectInputKey(kDikLeftShift, true);
    ok = SendRawKey(vk, true) && ok;
    ok = SendDirectInputKey(stroke.dik, true) && ok;
    ok = PostMessageW(S().window.load(std::memory_order_acquire), WM_CHAR, static_cast<WPARAM>(character), 1) != 0 && ok;
    Sleep(kTextKeyMs);
    ok = SendRawKey(vk, false) && ok;
    SendDirectInputKey(stroke.dik, false);
    if (shift) ok = SendRawKey(VK_SHIFT, false) && ok;
    if (stroke.shift) SendDirectInputKey(kDikLeftShift, false);
    Sleep(kTextKeyMs);
    return ok;
}

// The device-change messages a game waits for before it looks for a controller
// that was not there a moment ago: Unreal Engine 4 looks on any change of the
// devices, Wreckfest only on an interface arriving. The arrival names an XInput
// controller's HID interface as Windows names one (IG_ marks it XInput). Windows
// refuses to post it, since it carries a pointer, so it is sent, and given up on
// if the game's window does not answer.
inline bool TellGamePadArrived(int index) {
    const HWND window = S().window.load();
    if (!window) return false;
    const bool changed = PostMessageW(window, WM_DEVICECHANGE, DBT_DEVNODES_CHANGED, 0) != 0;
    constexpr std::size_t kNameLength = 96;
    alignas(DEV_BROADCAST_DEVICEINTERFACE_W) BYTE bytes[sizeof(DEV_BROADCAST_DEVICEINTERFACE_W) + kNameLength * sizeof(wchar_t)] = {};
    auto* arrival = reinterpret_cast<DEV_BROADCAST_DEVICEINTERFACE_W*>(bytes);
    arrival->dbcc_size = sizeof(bytes);
    arrival->dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    // GUID_DEVINTERFACE_HID.
    arrival->dbcc_classguid = {0x4D1E55B2, 0xF16F, 0x11CF, {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
    std::swprintf(arrival->dbcc_name, kNameLength,
                  L"\\\\?\\HID#VID_045E&PID_028E&IG_%02d#cameraunlock&%d#{4d1e55b2-f16f-11cf-88cb-001111000030}", index,
                  index);
    DWORD_PTR answer = 0;
    const bool arrived = SendMessageTimeoutW(window, WM_DEVICECHANGE, DBT_DEVICEARRIVAL, reinterpret_cast<LPARAM>(arrival),
                                             SMTO_ABORTIFHUNG, 2000, &answer) != 0;
    return changed && arrived;
}

// A pad command. A pad's first command plugs it in with nothing held, tells the
// game, and is played only once the game has read the new pad: Wreckfest rebuilds
// its controllers on the arrival, and a press already held when the rebuilt
// controller first reads the pad is never seen going down.
inline bool SendPad(const InputStep& step) {
    bool plugged = false;
    {
        std::lock_guard<std::mutex> lock(S().padLock);
        PadState& pad = S().pads[step.pad];
        plugged = !pad.connected;
        if (plugged) {
            pad.connected = true;
            ++pad.packet;
        } else {
            ApplyPadStep(pad, step.padControl, step.padButton, step.padDown, step.padX, step.padY);
        }
    }
    if (!plugged) return true;
    const bool told = TellGamePadArrived(step.pad);
    const unsigned before = S().padReads[step.pad].load(std::memory_order_acquire);
    const ULONGLONG start = GetTickCount64();
    unsigned reads = 0;
    while ((reads = S().padReads[step.pad].load(std::memory_order_acquire) - before) < kPadPlugReads
           && GetTickCount64() - start < kPadPlugWaitMs) {
        Sleep(5);
    }
    const unsigned long long waited = GetTickCount64() - start;
    {
        std::lock_guard<std::mutex> lock(S().padLock);
        ApplyPadStep(S().pads[step.pad], step.padControl, step.padButton, step.padDown, step.padX, step.padY);
    }
    const bool read = reads >= kPadPlugReads;
    char line[200];
    std::snprintf(line, sizeof(line),
                  read ? "isolated input: pad %d plugged in; the game read it %u times in %llu ms, then its first command was played"
                       : "isolated input: pad %d plugged in, and the game read it only %u times in %llu ms; its first command was played anyway and may be lost",
                  step.pad, reads, waited);
    Log(line);
    return told && read;
}

inline bool Play(const InputStep& step) {
    switch (step.action) {
        case InputAction::kPad:       return SendPad(step);
        case InputAction::kKeyDown:   return SendKey(step.vk, true);
        case InputAction::kKeyUp:     return SendKey(step.vk, false);
        case InputAction::kPostDown:  return PostKey(step.vk, true);
        case InputAction::kPostUp:    return PostKey(step.vk, false);
        case InputAction::kActivate:  return PostActivate();
        case InputAction::kMouseDown:
            SendDirectInputButton(step.button, true);
            SendGameInputButton(step.button, true);
            HoldButtonKeyState(step.button, true);
            PostCursorButton(step.button, true);
            return SendMouse(ButtonFlag(step.button, true), 0, 0);
        case InputAction::kMouseUp:
            SendDirectInputButton(step.button, false);
            SendGameInputButton(step.button, false);
            HoldButtonKeyState(step.button, false);
            PostCursorButton(step.button, false);
            return SendMouse(ButtonFlag(step.button, false), 0, 0);
        case InputAction::kMouseMove:
            SendDirectInputMove(step.dx, step.dy);
            SendGameInputMove(step.dx, step.dy);
            return SendMouse(0, step.dx, step.dy);
        case InputAction::kCursor:    return PlaceCursor(step.dx, step.dy);
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
        InstallDirectInput();
        InstallGameInput();
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

inline LRESULT CALLBACK GameWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    const bool leftForeground = (message == WM_ACTIVATEAPP && wParam == FALSE) ||
                                (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) || message == WM_KILLFOCUS;
    if (leftForeground) return 0;
    // The frame is told it went inactive, so the title bar is drawn as Windows
    // wants it, and the game is not: Deus Ex: Mankind Divided takes its active
    // state from this message and reads no raw input while it is inactive.
    if (message == WM_NCACTIVATE && wParam == FALSE) {
        return IsWindowUnicode(window) ? DefWindowProcW(window, message, wParam, lParam)
                                       : DefWindowProcA(window, message, wParam, lParam);
    }
    const WNDPROC original = S().windowProc.load(std::memory_order_acquire);
    const LRESULT result = IsWindowUnicode(window) ? CallWindowProcW(original, window, message, wParam, lParam)
                                                   : CallWindowProcA(original, window, message, wParam, lParam);
    // DefWindowProc sets the class cursor without passing through the SetCursor
    // detour, so the cursor is read back on the window's own thread.
    if (message == WM_SETCURSOR) S().cursor.store(GetCursor(), std::memory_order_release);
    return result;
}

// From the moment the game has a window it is told that window has the
// foreground, script or no script. Looked for again once that window is gone or
// hidden: the first one a game shows can be its splash screen.
inline void WatchGameWindow() {
    const HWND known = S().window.load(std::memory_order_acquire);
    if (known && IsWindowVisible(known)) return;
    const HWND window = FindGameWindow();
    if (!window || window == known) return;
    const bool unicode = IsWindowUnicode(window) != 0;
    // Stored before ours goes in: a message can arrive between the two calls.
    S().windowProc.store(reinterpret_cast<WNDPROC>(unicode ? GetWindowLongPtrW(window, GWLP_WNDPROC)
                                                           : GetWindowLongPtrA(window, GWLP_WNDPROC)),
                         std::memory_order_release);
    if (unicode) SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&GameWindowProc));
    else SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&GameWindowProc));
    S().window.store(window, std::memory_order_release);
    // A window that lost the foreground before it was found (a mod loaded late, as a C# one
    // is, or a launch that never had the foreground) has already been told so, and an engine
    // that ignores input while deactivated would ignore every script.
    PostMessageW(window, WM_ACTIVATEAPP, TRUE, 0);
    PostMessageW(window, WM_NCACTIVATE, TRUE, 0);
    PostMessageW(window, WM_ACTIVATE, WA_ACTIVE, 0);
    PostMessageW(window, WM_SETFOCUS, 0, 0);
    Log("isolated input: game window found");
}

// Keeps the real foreground away from the game: remembers which window of
// another process last held it, and gives it back to that one (to the desktop
// if it has gone) whenever one of this process's windows has taken it. The
// window is taken over first, so the game is not told of the loss. A thread of
// its own, because the command-file thread sleeps through a script's waits.
inline void GuardForeground() {
    bool refused = false;
    for (;;) {
        Sleep(kPollMs);
        WatchGameWindow();
        const HWND foreground = S().origGetForegroundWindow();
        if (!foreground) continue;
        DWORD pid = 0;
        GetWindowThreadProcessId(foreground, &pid);
        if (pid != GetCurrentProcessId()) {
            S().lastForeground = foreground;
            continue;
        }
        const HWND previous = IsWindow(S().lastForeground) ? S().lastForeground : GetShellWindow();
        if (S().origSetForegroundWindow(previous)) {
            refused = false;
            Log("isolated input: the game had the real foreground and gave it back");
        } else if (!refused) {
            refused = true;
            Log("isolated input: the game has the real foreground and could not give it back");
        }
    }
}

// Every function detoured so far, so a start that fails part way takes each one back out.
inline std::vector<void*>& Detoured() {
    static std::vector<void*> targets;
    return targets;
}

template <typename Detour, typename Original>
inline bool Hook(const wchar_t* module, const char* name, Detour detour, Original& original) {
    void* target = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(module), name));
    MH_STATUS status = target
        ? MH_CreateHook(target, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(&original))
        : MH_ERROR_FUNCTION_NOT_FOUND;
    if (status == MH_OK) status = MH_EnableHook(target);
    if (status == MH_OK) {
        Detoured().push_back(target);
        return true;
    }
    // The bytes say what is already sitting on the function: another hook's jump, or a stub
    // MinHook cannot relocate.
    char bytes[3 * 16 + 1] = "";
    if (target) {
        for (int i = 0; i < 16; ++i) {
            std::snprintf(bytes + 3 * i, 4, " %02X", static_cast<const unsigned char*>(target)[i]);
        }
    }
    char line[200];
    std::snprintf(line, sizeof(line), "isolated input: could not hook %s: %s, starts%s", name,
                  MH_StatusToString(status), bytes);
    Log(line);
    if (target) MH_RemoveHook(target);
    return false;
}

inline void UnhookAll() {
    for (void* target : Detoured()) {
        MH_DisableHook(target);
        MH_RemoveHook(target);
    }
    Detoured().clear();
}

}  // namespace isolated

inline void LetGameMoveScriptedCursor(bool allowed) {
    isolated::S().gameMovesCursor.store(allowed, std::memory_order_release);
}

inline bool HoldIsolatedKey(int vk, bool down) {
    if (vk < 1 || vk > 255) return false;
    isolated::HoldKeyState(vk, down);
    if (const int left = isolated::LeftHandModifier(vk)) isolated::S().keyDown[left].store(down, std::memory_order_release);
    return true;
}

inline bool StartIsolatedInput(const std::wstring& commandFile, IsolatedInputLogFn log) {
    using namespace isolated;
    State& s = S();
    s.log = log;
    s.commandFile = commandFile;
    s.cursor.store(LoadCursor(nullptr, IDC_ARROW), std::memory_order_release);
    FindDevices();

    static const wchar_t* const kUser32 = L"user32.dll";
    const bool hooked[] = {
        Hook(kUser32, "GetRawInputData", &HookedGetRawInputData, s.origGetRawInputData),
        Hook(kUser32, "GetRawInputBuffer", &HookedGetRawInputBuffer, s.origGetRawInputBuffer),
        Hook(kUser32, "GetForegroundWindow", &HookedGetForegroundWindow, s.origGetForegroundWindow),
        Hook(kUser32, "GetAsyncKeyState", &HookedGetAsyncKeyState, s.origGetAsyncKeyState),
        Hook(kUser32, "GetKeyState", &HookedGetKeyState, s.origGetKeyState),
        Hook(kUser32, "GetKeyboardState", &HookedGetKeyboardState, s.origGetKeyboardState),
        Hook(kUser32, "ClipCursor", &HookedClipCursor, s.origClipCursor),
        Hook(kUser32, "GetClipCursor", &HookedGetClipCursor, s.origGetClipCursor),
        // user32's SetCursorPos can already be written over when the mod loads: in Resident Evil
        // Requiem with REFramework loaded it starts with a `ret`, which MinHook cannot detour.
        // It is a jump to the system call stub in win32u.dll, so that is detoured in its place
        // and still answers once the bytes above it are put back.
        Hook(kUser32, "SetCursorPos", &HookedSetCursorPos, s.origSetCursorPos)
            || Hook(L"win32u.dll", "NtUserSetCursorPos", &HookedSetCursorPos, s.origSetCursorPos),
        Hook(kUser32, "SetForegroundWindow", &HookedSetForegroundWindow, s.origSetForegroundWindow),
        Hook(kUser32, "GetCursorPos", &HookedGetCursorPos, s.origGetCursorPos),
        Hook(kUser32, "ShowCursor", &HookedShowCursor, s.origShowCursor),
        Hook(kUser32, "SetCursor", &HookedSetCursor, s.origSetCursor),
        Hook(kUser32, "GetCursorInfo", &HookedGetCursorInfo, s.origGetCursorInfo),
    };
    // A controller DLL the game has not loaded is left alone, and one it has is
    // optional: a game is driven by its keyboard and mouse without it.
    for (int i = 0; i < kXInputDllCount; ++i) {
        if (!GetModuleHandleW(kXInputDlls[i])) continue;
        Hook(kXInputDlls[i], "XInputGetState", &HookedXInputGetState, s.origXInputGetState[i]);
        Hook(kXInputDlls[i], "XInputSetState", &HookedXInputSetState, s.origXInputSetState[i]);
        Hook(kXInputDlls[i], "XInputGetCapabilities", &HookedXInputGetCapabilities, s.origXInputGetCapabilities[i]);
    }
    bool all = true;
    for (const bool one : hooked) all = all && one;
    if (!all) {
        UnhookAll();
        return false;
    }
    std::thread(&RunCommandFile).detach();
    std::thread(&GuardForeground).detach();
    Log("isolated input: the game takes its keyboard and mouse from the command file, not from the real devices");
    return true;
}

}  // namespace cameraunlock::dev

#endif  // CAMERAUNLOCK_ISOLATED_INPUT_IMPLEMENTATION
