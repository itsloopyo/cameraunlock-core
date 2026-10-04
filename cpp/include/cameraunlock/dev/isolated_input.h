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
//     otherwise trap and recentre the real cursor.
//   - SetForegroundWindow does nothing, so the game cannot take the foreground
//     back.
//   - GetCursorPos answers the point the last `cursor` command gave, once one
//     has: a menu that reads where the cursor is gets the script's cursor, as a
//     WM_MOUSEMOVE at that point and as the position it reads back, and a button
//     then also arrives as the window message a click there would be.
//
// And two things that keep the game in the background without it knowing:
//
//   - The real foreground is watched, and whenever a window of the game holds it
//     the game gives it back to the window that had it before. Only the
//     foreground process may do that, so it is done from in here.
//   - The game's window procedure never sees the messages that say it lost the
//     foreground (WM_ACTIVATEAPP, WM_ACTIVATE and WM_KILLFOCUS), so a game that
//     stops following its mouse when it is deactivated carries on.
//
// For games that read DirectInput 8 (dinput8.dll loaded in the process): the
// keyboard and mouse devices answer GetDeviceState and GetDeviceData from the
// script alone (directinput_state.h), through detours on the device vtable, so
// a device the game makes later is covered too. The real devices are never
// acquired: Acquire and Poll succeed without reaching DirectInput, and
// SetCooperativeLevel is passed on as background and non-exclusive. Any other
// DirectInput device (a controller) is left as it was. dinput8.dll is never
// loaded by this header: a game that has not loaded it gets no such detours.
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

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
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

using GetRawInputData_t = UINT (WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
using GetForegroundWindow_t = HWND (WINAPI*)();
using GetAsyncKeyState_t = SHORT (WINAPI*)(int);
using GetKeyState_t = SHORT (WINAPI*)(int);
using GetKeyboardState_t = BOOL (WINAPI*)(PBYTE);
using ClipCursor_t = BOOL (WINAPI*)(const RECT*);
using GetClipCursor_t = BOOL (WINAPI*)(LPRECT);
using SetCursorPos_t = BOOL (WINAPI*)(int, int);
using SetForegroundWindow_t = BOOL (WINAPI*)(HWND);
using GetCursorPos_t = BOOL (WINAPI*)(LPPOINT);

struct State {
    GetRawInputData_t origGetRawInputData = nullptr;
    GetForegroundWindow_t origGetForegroundWindow = nullptr;
    GetAsyncKeyState_t origGetAsyncKeyState = nullptr;
    GetKeyState_t origGetKeyState = nullptr;
    GetKeyboardState_t origGetKeyboardState = nullptr;
    ClipCursor_t origClipCursor = nullptr;
    GetClipCursor_t origGetClipCursor = nullptr;
    SetCursorPos_t origSetCursorPos = nullptr;
    SetForegroundWindow_t origSetForegroundWindow = nullptr;
    GetCursorPos_t origGetCursorPos = nullptr;

    // The cursor the script placed, in the game window's client area.
    std::atomic<bool> cursorPlaced{false};
    std::atomic<int> cursorX{0};
    std::atomic<int> cursorY{0};
    std::atomic<unsigned> buttonsHeld{0};
    std::atomic<bool> cursorPosRead{false};

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

inline BOOL WINAPI HookedSetCursorPos(int, int) { return TRUE; }

inline BOOL WINAPI HookedGetCursorPos(LPPOINT point) {
    if (!S().cursorPosRead.exchange(true, std::memory_order_acq_rel)) {
        Log("isolated input: the game reads the cursor's position (GetCursorPos)");
    }
    const HWND window = S().window.load(std::memory_order_acquire);
    if (!point || !window || !S().cursorPlaced.load(std::memory_order_acquire)) return S().origGetCursorPos(point);
    *point = {S().cursorX.load(std::memory_order_relaxed), S().cursorY.load(std::memory_order_relaxed)};
    return ClientToScreen(window, point);
}
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

static_assert(offsetof(DIDEVICEOBJECTDATA, dwOfs) == offsetof(DirectInputEvent, offset)
              && offsetof(DIDEVICEOBJECTDATA, dwData) == offsetof(DirectInputEvent, data)
              && offsetof(DIDEVICEOBJECTDATA, dwTimeStamp) == offsetof(DirectInputEvent, timeMs)
              && offsetof(DIDEVICEOBJECTDATA, dwSequence) == offsetof(DirectInputEvent, sequence),
              "DirectInputEvent is the head of DIDEVICEOBJECTDATA");
static_assert(sizeof(DIMOUSESTATE) == kMouseStateSize && sizeof(DIMOUSESTATE2) == kMouseState2Size
              && offsetof(DIMOUSESTATE, lX) == kMouseOffsetX && offsetof(DIMOUSESTATE, lY) == kMouseOffsetY
              && offsetof(DIMOUSESTATE, rgbButtons) == kMouseOffsetButton0,
              "directinput_state.h writes the standard mouse data format");
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
    kDiMethods
};
inline constexpr int kDiSlots[kDiMethods] = {2, 3, 7, 9, 10, 13, 25};
// A and W interface, keyboard and mouse: at most this many addresses for one
// method.
inline constexpr int kDiVariants = 4;

using DiRelease_t = ULONG (STDMETHODCALLTYPE*)(void*);
using DiGetCapabilities_t = HRESULT (STDMETHODCALLTYPE*)(void*, LPDIDEVCAPS);
using DiAcquire_t = HRESULT (STDMETHODCALLTYPE*)(void*);
using DiGetDeviceState_t = HRESULT (STDMETHODCALLTYPE*)(void*, DWORD, LPVOID);
using DiGetDeviceData_t = HRESULT (STDMETHODCALLTYPE*)(void*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
using DiSetCooperativeLevel_t = HRESULT (STDMETHODCALLTYPE*)(void*, HWND, DWORD);
using DirectInput8Create_t = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using DiCreateDevice_t = HRESULT (STDMETHODCALLTYPE*)(void*, REFGUID, void**, LPUNKNOWN);

// Spelled out here so that a mod needs neither dxguid.lib nor dinput8.lib.
inline constexpr GUID kIidDirectInput8A = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
inline constexpr GUID kIidDirectInput8W = {0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
inline constexpr GUID kGuidSysMouse = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
inline constexpr GUID kGuidSysKeyboard = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

struct DirectInputDevice {
    void* object = nullptr;
    // False for a device that is neither a keyboard nor a mouse, which is left
    // to DirectInput.
    bool synthetic = false;
    bool stateLogged = false;
    bool dataLogged = false;
    DirectInputReader reader;
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

// The entry for a device, made the first time the device is seen. Called with
// `lock` held, and lets go of it while it asks DirectInput what the device is.
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
    lock.lock();
    for (DirectInputDevice& device : d.devices) {
        if (device.object == object) return device;
    }
    DirectInputDevice device;
    device.object = object;
    device.synthetic = type == DI8DEVTYPE_KEYBOARD || type == DI8DEVTYPE_MOUSE;
    device.reader = d.state.Open(type == DI8DEVTYPE_MOUSE ? DirectInputKind::kMouse : DirectInputKind::kKeyboard);
    d.devices.push_back(device);
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
// answer. Only the standard data formats are answered.
inline bool AnswerDeviceState(void* object, DWORD size, LPVOID data, HRESULT& result) {
    DirectInput& d = D();
    const char* logKind = nullptr;
    {
        std::unique_lock<std::mutex> lock(d.mutex);
        DirectInputDevice& device = DiDevice(object, lock);
        if (!device.synthetic) return false;
        if (!device.stateLogged) {
            device.stateLogged = true;
            logKind = DiKindName(device);
        }
        auto* const out = static_cast<uint8_t*>(data);
        if (out == nullptr) {
            result = DIERR_INVALIDPARAM;
        } else if (device.reader.kind == DirectInputKind::kMouse) {
            result = d.state.MouseState(device.reader, out, size) ? DI_OK : DIERR_INVALIDPARAM;
        } else if (size == kKeyboardStateSize) {
            d.state.KeyboardState(out);
            result = DI_OK;
        } else {
            result = DIERR_INVALIDPARAM;
        }
    }
    if (logKind) {
        LogDeviceRead(logKind, "GetDeviceState", size);
        if (FAILED(result)) Log("isolated input: that is not the standard data format, the only one isolated input answers");
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
                                                    (flags & DIGDD_PEEK) != 0, overflowed);
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

template <int Variant>
inline void* DiDetour(DirectInputMethod method) {
    switch (method) {
        case kDiRelease:             return reinterpret_cast<void*>(&HookedDiRelease<Variant>);
        case kDiAcquire:             return reinterpret_cast<void*>(&HookedDiAcquire<Variant>);
        case kDiGetDeviceState:      return reinterpret_cast<void*>(&HookedDiGetDeviceState<Variant>);
        case kDiGetDeviceData:       return reinterpret_cast<void*>(&HookedDiGetDeviceData<Variant>);
        case kDiSetCooperativeLevel: return reinterpret_cast<void*>(&HookedDiSetCooperativeLevel<Variant>);
        case kDiPoll:                return reinterpret_cast<void*>(&HookedDiPoll<Variant>);
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

inline bool SendRawKey(int vk, bool down) {
    const bool wasDown = S().keyDown[vk].exchange(down, std::memory_order_acq_rel);
    if (down) S().keyPressedSinceRead[vk].store(true, std::memory_order_release);
    // Only the command-file thread writes the toggle, so this read and write cannot interleave.
    if (down && !wasDown) {
        S().keyToggled[vk].store(!S().keyToggled[vk].load(std::memory_order_relaxed), std::memory_order_release);
    }

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

inline bool Play(const InputStep& step) {
    switch (step.action) {
        case InputAction::kKeyDown:   return SendKey(step.vk, true);
        case InputAction::kKeyUp:     return SendKey(step.vk, false);
        case InputAction::kMouseDown:
            SendDirectInputButton(step.button, true);
            PostCursorButton(step.button, true);
            return SendMouse(ButtonFlag(step.button, true), 0, 0);
        case InputAction::kMouseUp:
            SendDirectInputButton(step.button, false);
            PostCursorButton(step.button, false);
            return SendMouse(ButtonFlag(step.button, false), 0, 0);
        case InputAction::kMouseMove:
            SendDirectInputMove(step.dx, step.dy);
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
    const WNDPROC original = S().windowProc.load(std::memory_order_acquire);
    return IsWindowUnicode(window) ? CallWindowProcW(original, window, message, wParam, lParam)
                                   : CallWindowProcA(original, window, message, wParam, lParam);
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

inline bool StartIsolatedInput(const std::wstring& commandFile, IsolatedInputLogFn log) {
    using namespace isolated;
    State& s = S();
    s.log = log;
    s.commandFile = commandFile;
    FindDevices();

    static const wchar_t* const kUser32 = L"user32.dll";
    const bool hooked[] = {
        Hook(kUser32, "GetRawInputData", &HookedGetRawInputData, s.origGetRawInputData),
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
    };
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
