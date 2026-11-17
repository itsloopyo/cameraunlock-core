// CameraUnlockRigShim.dll: loaded into a game that a background test starts,
// before the game's first window, so the game cannot take the foreground, the
// mouse or the display mode from the person at the machine. Rig tooling, never
// shipped (docs/isolated-input.md, "The guarded launch").
//
// It goes in below user32, at win32u's system call stubs, because user32 reaches
// those from inside itself too (a message box, a dialog) without passing its own
// exports. cameraunlock/dev/isolated_input.h detours user32 above this once the
// mod's dev build loads, and the two stack.
//
//   - Every top-level window is made WS_EX_NOACTIVATE and is shown without being
//     activated. A window is never raised above other programs' windows, and a
//     game's first plain window goes to the bottom of the stack.
//   - Every top-level window that has no owner and is not a tool window sits in
//     the bottom right corner of the work area at the size the game gave it:
//     put there before it is shown, and held there through SetWindowPos,
//     MoveWindow, DeferWindowPos and SetWindowPlacement. A maximised window
//     is left as it is.
//   - SetForegroundWindow, SwitchToThisWindow, BringWindowToTop, FlashWindow(Ex),
//     LockSetForegroundWindow and AllowSetForegroundWindow do nothing and answer
//     success. AttachThreadInput to another process's thread does nothing.
//     SendInput sends nothing.
//   - SetActiveWindow and SetFocus are passed on. They act inside the game's own
//     input queue, which never holds the foreground, and they are what tells the
//     game it is active (WM_ACTIVATE, WM_SETFOCUS). GetForegroundWindow is left
//     telling the truth: isolated input answers it with the game's window once
//     it is in, and watches the real one through the same call.
//   - ChangeDisplaySettings(Ex) and SetDisplayConfig change nothing and answer
//     success; a test of a mode is passed on. Direct3D 9 devices and DXGI swap
//     chains asked for exclusive fullscreen are made windowed, at most 1280x720
//     and in the bottom right corner, and the swap chain answers that it is
//     fullscreen.
//   - ClipCursor and SetCursorPos do nothing. Raw input is registered without
//     the flags that deliver it while another program has the foreground.
//     DirectInput 8 devices are never exclusive.
//   - Every process this one starts gets the shim before its first instruction.

#include <Windows.h>

#include <d3d9.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include <MinHook.h>

#include "inject.h"

namespace {

std::wstring g_folder;
std::wstring g_logFolder;
std::wstring g_logPath;
SRWLOCK g_logLock = SRWLOCK_INIT;

void Log(const char* format, ...) {
    char text[900];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    SYSTEMTIME now;
    GetLocalTime(&now);
    char line[1000];
    const int length = snprintf(line, sizeof(line), "%02u:%02u:%02u.%03u %s\r\n", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, text);
    AcquireSRWLockExclusive(&g_logLock);
    const HANDLE file = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&g_logLock);
}

// A game can ask for the same refused thing twice a second for an hour: the 1st,
// 10th, 100th... time is logged, with the count.
struct Refused {
    const char* what;
    std::atomic<unsigned> count{0};
    void Note() {
        const unsigned n = ++count;
        unsigned power = 1;
        while (power < n && power < 1000000000u) power *= 10;
        if (power == n) Log("refused: %s (%u so far)", what, n);
    }
};

Refused g_foreground{"SetForegroundWindow"}, g_switch{"SwitchToThisWindow"}, g_bringTop{"BringWindowToTop"}, g_flash{"FlashWindow"},
    g_attach{"AttachThreadInput to another process"}, g_lock{"LockSetForegroundWindow or AllowSetForegroundWindow"},
    g_sendInput{"SendInput"}, g_mode{"a display mode change"}, g_clip{"ClipCursor"}, g_cursor{"SetCursorPos"},
    g_rawSink{"raw input while in the background"}, g_activate{"a window activating as it was shown or moved"},
    g_raise{"a window raised above other programs"}, g_place{"a window anywhere but the corner"}, g_exclusive{"exclusive fullscreen"}, g_diExclusive{"an exclusive DirectInput device"};

// ---------------------------------------------------------------------------
// The originals, each reached through its trampoline
// ---------------------------------------------------------------------------

using CreateWindow_t = HWND (WINAPI*)(DWORD, void*, void*, void*, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, void*, DWORD, void*, void*, void*);
using ShowWindow_t = BOOL (WINAPI*)(HWND, int);
using SetWindowPos_t = BOOL (WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using MoveWindow_t = BOOL (WINAPI*)(HWND, int, int, int, int, BOOL);
using SetWindowPlacement_t = BOOL (WINAPI*)(HWND, const WINDOWPLACEMENT*);
using SetWindowLong_t = LONG_PTR (WINAPI*)(HWND, int, LONG, BOOL);
using SetWindowLongPtr_t = LONG_PTR (WINAPI*)(HWND, int, LONG_PTR, BOOL);
using OneWindow_t = BOOL (WINAPI*)(HWND);
using SwitchToThisWindow_t = void (WINAPI*)(HWND, BOOL);
using FlashWindowEx_t = BOOL (WINAPI*)(PFLASHWINFO);
using AttachThreadInput_t = BOOL (WINAPI*)(DWORD, DWORD, BOOL);
using OneCode_t = BOOL (WINAPI*)(UINT_PTR);
using SendInput_t = UINT (WINAPI*)(UINT, LPINPUT, int);
using ChangeDisplaySettings_t = LONG (WINAPI*)(void*, DEVMODEW*, DWORD, void*);
using SetDisplayConfig_t = LONG (WINAPI*)(UINT32, void*, UINT32, void*, UINT32);
using ClipCursor_t = BOOL (WINAPI*)(const RECT*);
using SetCursorPos_t = BOOL (WINAPI*)(int, int);
using RegisterRawInputDevices_t = BOOL (WINAPI*)(PCRAWINPUTDEVICE, UINT, UINT);
using DeferWindowPos_t = HDWP (WINAPI*)(HDWP, HWND, HWND, int, int, int, int, UINT);
using CreateProcessInternal_t = BOOL (WINAPI*)(HANDLE, LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR,
                                               LPSTARTUPINFOW, LPPROCESS_INFORMATION, PHANDLE);

CreateWindow_t oCreateWindow;
ShowWindow_t oShowWindow, oShowWindowAsync;
SetWindowPos_t oSetWindowPos;
MoveWindow_t oMoveWindow;
SetWindowPlacement_t oSetWindowPlacement;
SetWindowLong_t oSetWindowLong;
SetWindowLongPtr_t oSetWindowLongPtr;
OneWindow_t oSetForegroundWindow, oBringWindowToTop;
SwitchToThisWindow_t oSwitchToThisWindow;
FlashWindowEx_t oFlashWindowEx;
AttachThreadInput_t oAttachThreadInput;
OneCode_t oLockSetForegroundWindow, oAllowSetForegroundWindow;
SendInput_t oSendInput;
ChangeDisplaySettings_t oChangeDisplaySettings;
SetDisplayConfig_t oSetDisplayConfig;
ClipCursor_t oClipCursor;
SetCursorPos_t oSetCursorPos, oSetPhysicalCursorPos;
RegisterRawInputDevices_t oRegisterRawInputDevices;
DeferWindowPos_t oDeferWindowPos;
CreateProcessInternal_t oCreateProcessInternal;

// ---------------------------------------------------------------------------
// Windows
// ---------------------------------------------------------------------------

SRWLOCK g_shownLock = SRWLOCK_INIT;
HWND g_shown[64];
unsigned g_shownNext = 0;

bool Ours(HWND window) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    return pid == GetCurrentProcessId();
}

bool TopLevel(HWND window) { return window && !(GetWindowLongW(window, GWL_STYLE) & WS_CHILD); }

// The windows that are kept in the corner: the game's own top-level windows. Not
// a window with an owner (a dialog), a tool window, or a message-only window,
// whose parent is not the desktop.
bool GameWindow(HWND window) {
    return TopLevel(window) && Ours(window) && GetAncestor(window, GA_PARENT) == GetDesktopWindow() && !GetWindow(window, GW_OWNER) &&
           !(GetWindowLongW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW);
}

POINT CornerFor(int width, int height) {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    return {work.right - width, work.bottom - height};
}

// The place a window is restored to is given from the work area's top left,
// for a window that is not a tool window.
bool PlacementToCorner(WINDOWPLACEMENT& placement) {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const RECT asked = placement.rcNormalPosition;
    const POINT corner = CornerFor(asked.right - asked.left, asked.bottom - asked.top);
    const int dx = corner.x - work.left - asked.left, dy = corner.y - work.top - asked.top;
    OffsetRect(&placement.rcNormalPosition, dx, dy);
    return dx || dy;
}

// A minimised window has no place on the screen, so the place it comes back to is
// what is put in the corner.
void Corner(HWND window) {
    if (!GameWindow(window) || IsZoomed(window)) return;
    if (IsIconic(window)) {
        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        if (!GetWindowPlacement(window, &placement) || !PlacementToCorner(placement)) return;
        g_place.Note();
        placement.showCmd = SW_SHOWMINNOACTIVE;
        oSetWindowPlacement(window, &placement);
        return;
    }
    RECT now{};
    GetWindowRect(window, &now);
    const POINT corner = CornerFor(now.right - now.left, now.bottom - now.top);
    if (now.left == corner.x && now.top == corner.y) return;
    g_place.Note();
    oSetWindowPos(window, nullptr, corner.x, corner.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// A resize moves the window too, since the corner it is held by is the bottom
// right one.
void HoldInCorner(HWND window, int& x, int& y, int cx, int cy, UINT& flags) {
    if (!GameWindow(window) || IsIconic(window) || IsZoomed(window)) return;
    if ((flags & SWP_NOMOVE) && (flags & SWP_NOSIZE) && !(flags & SWP_SHOWWINDOW)) return;
    RECT now{};
    GetWindowRect(window, &now);
    const POINT corner = CornerFor((flags & SWP_NOSIZE) ? now.right - now.left : cx, (flags & SWP_NOSIZE) ? now.bottom - now.top : cy);
    if (!(flags & SWP_NOMOVE) && (x != corner.x || y != corner.y)) g_place.Note();
    x = corner.x;
    y = corner.y;
    flags &= ~static_cast<UINT>(SWP_NOMOVE);
}

bool FirstTimeShown(HWND window) {
    AcquireSRWLockExclusive(&g_shownLock);
    bool first = true;
    for (HWND seen : g_shown) if (seen == window) first = false;
    if (first) g_shown[g_shownNext++ % 64] = window;
    ReleaseSRWLockExclusive(&g_shownLock);
    return first;
}

// What a show that would have activated the window owes the game: the game's own
// queue is told the window is active, which is all a game looks at, and the
// window goes under everyone else's. A window with an owner (a dialog, a tool
// tip) stays above its owner where Windows put it.
void Shown(HWND window) {
    if (!FirstTimeShown(window)) return;
    const bool plain = !GetWindow(window, GW_OWNER) && !(GetWindowLongW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW);
    if (GetWindowThreadProcessId(window, nullptr) == GetCurrentThreadId()) SetActiveWindow(window);
    if (plain) oSetWindowPos(window, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

int WithoutActivating(int command) {
    switch (command) {
        case SW_SHOWNORMAL:
        case SW_SHOWMAXIMIZED:
        case SW_RESTORE:
        case SW_SHOWDEFAULT: return SW_SHOWNOACTIVATE;
        case SW_SHOW: return SW_SHOWNA;
        case SW_SHOWMINIMIZED:
        case SW_MINIMIZE: return SW_SHOWMINNOACTIVE;
        default: return command;
    }
}

bool Shows(int command) { return command != SW_HIDE && command != SW_SHOWMINNOACTIVE && command != SW_FORCEMINIMIZE; }

HWND WINAPI HookedCreateWindow(DWORD exStyle, void* className, void* classVersion, void* name, DWORD style, int x, int y, int width, int height,
                               HWND parent, HMENU menu, HINSTANCE instance, void* parameter, DWORD flags, void* a15, void* a16, void* a17) {
    if (style & WS_CHILD) {
        return oCreateWindow(exStyle, className, classVersion, name, style, x, y, width, height, parent, menu, instance, parameter, flags, a15, a16, a17);
    }
    const HWND made = oCreateWindow(exStyle | WS_EX_NOACTIVATE, className, classVersion, name, style & ~WS_VISIBLE, x, y, width, height, parent, menu,
                                    instance, parameter, flags, a15, a16, a17);
    if (made && (style & WS_VISIBLE)) {
        g_activate.Note();
        Corner(made);
        oShowWindow(made, SW_SHOWNA);
        Shown(made);
    }
    return made;
}

BOOL ShowQuietly(ShowWindow_t original, HWND window, int command) {
    if (!TopLevel(window) || !Ours(window)) return original(window, command);
    const int quiet = WithoutActivating(command);
    if (quiet != command) g_activate.Note();
    if (Shows(quiet)) Corner(window);
    const BOOL result = original(window, quiet);
    // Again once it is shown: a minimised or maximised window has just been given back the place it had.
    if (IsWindowVisible(window)) Corner(window);
    if (Shows(quiet)) Shown(window);
    return result;
}
BOOL WINAPI HookedShowWindow(HWND window, int command) { return ShowQuietly(oShowWindow, window, command); }
BOOL WINAPI HookedShowWindowAsync(HWND window, int command) { return ShowQuietly(oShowWindowAsync, window, command); }

// Never activating, and never raised to the top or made topmost. A place among
// its own process's windows, or below, is the game's to choose.
void QuietPosition(HWND& after, UINT& flags) {
    if (!(flags & SWP_NOACTIVATE)) {
        g_activate.Note();
        flags |= SWP_NOACTIVATE;
    }
    const bool overAnother = after != HWND_BOTTOM && after != HWND_NOTOPMOST && IsWindow(after) && !Ours(after);
    if (!(flags & SWP_NOZORDER) && (after == HWND_TOP || after == HWND_TOPMOST || overAnother)) {
        g_raise.Note();
        flags |= SWP_NOZORDER;
    }
}

BOOL WINAPI HookedSetWindowPos(HWND window, HWND after, int x, int y, int cx, int cy, UINT flags) {
    if (!TopLevel(window) || !Ours(window)) return oSetWindowPos(window, after, x, y, cx, cy, flags);
    QuietPosition(after, flags);
    HoldInCorner(window, x, y, cx, cy, flags);
    const BOOL result = oSetWindowPos(window, after, x, y, cx, cy, flags);
    // The window's own procedure can change the size on the way. Not after a call the game
    // did not wait for: the window's thread may be waiting for this one.
    if (!(flags & SWP_ASYNCWINDOWPOS) && IsWindowVisible(window)) Corner(window);
    if (flags & SWP_SHOWWINDOW) Shown(window);
    return result;
}

BOOL WINAPI HookedMoveWindow(HWND window, int x, int y, int cx, int cy, BOOL repaint) {
    UINT flags = 0;
    HoldInCorner(window, x, y, cx, cy, flags);
    const BOOL result = oMoveWindow(window, x, y, cx, cy, repaint);
    if (IsWindowVisible(window)) Corner(window);
    return result;
}

HDWP WINAPI HookedDeferWindowPos(HDWP positions, HWND window, HWND after, int x, int y, int cx, int cy, UINT flags) {
    if (TopLevel(window) && Ours(window)) {
        QuietPosition(after, flags);
        HoldInCorner(window, x, y, cx, cy, flags);
    }
    return oDeferWindowPos(positions, window, after, x, y, cx, cy, flags);
}

BOOL WINAPI HookedSetWindowPlacement(HWND window, const WINDOWPLACEMENT* placement) {
    if (!placement || !TopLevel(window) || !Ours(window)) return oSetWindowPlacement(window, placement);
    WINDOWPLACEMENT quiet = *placement;
    quiet.showCmd = static_cast<UINT>(WithoutActivating(static_cast<int>(placement->showCmd)));
    if (quiet.showCmd != placement->showCmd) g_activate.Note();
    if (GameWindow(window) && PlacementToCorner(quiet)) g_place.Note();
    const BOOL result = oSetWindowPlacement(window, &quiet);
    if (IsWindowVisible(window)) Corner(window);
    if (Shows(static_cast<int>(quiet.showCmd))) Shown(window);
    return result;
}

LONG_PTR WINAPI HookedSetWindowLong(HWND window, int index, LONG value, BOOL ansi) {
    if (index == GWL_EXSTYLE && TopLevel(window) && Ours(window)) value |= WS_EX_NOACTIVATE;
    return oSetWindowLong(window, index, value, ansi);
}
LONG_PTR WINAPI HookedSetWindowLongPtr(HWND window, int index, LONG_PTR value, BOOL ansi) {
    if (index == GWL_EXSTYLE && TopLevel(window) && Ours(window)) value |= WS_EX_NOACTIVATE;
    return oSetWindowLongPtr(window, index, value, ansi);
}

BOOL WINAPI HookedSetForegroundWindow(HWND) { g_foreground.Note(); return TRUE; }
BOOL WINAPI HookedBringWindowToTop(HWND) { g_bringTop.Note(); return TRUE; }
void WINAPI HookedSwitchToThisWindow(HWND, BOOL) { g_switch.Note(); }
BOOL WINAPI HookedFlashWindowEx(PFLASHWINFO) { g_flash.Note(); return FALSE; }
BOOL WINAPI HookedLockSetForegroundWindow(UINT_PTR) { g_lock.Note(); return TRUE; }
BOOL WINAPI HookedAllowSetForegroundWindow(UINT_PTR) { g_lock.Note(); return TRUE; }
UINT WINAPI HookedSendInput(UINT count, LPINPUT, int) { g_sendInput.Note(); return count; }

bool ThreadOfAnotherProcess(DWORD threadId) {
    const HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, threadId);
    if (!thread) return true;
    const DWORD pid = GetProcessIdOfThread(thread);
    CloseHandle(thread);
    return pid != GetCurrentProcessId();
}

// Joined to the foreground thread's input queue, a thread may take the foreground.
BOOL WINAPI HookedAttachThreadInput(DWORD from, DWORD to, BOOL attach) {
    if (attach && (ThreadOfAnotherProcess(from) || ThreadOfAnotherProcess(to))) {
        g_attach.Note();
        return TRUE;
    }
    return oAttachThreadInput(from, to, attach);
}

// ---------------------------------------------------------------------------
// The display mode, the cursor and raw input
// ---------------------------------------------------------------------------

LONG WINAPI HookedChangeDisplaySettings(void* device, DEVMODEW* mode, DWORD flags, void* parameter) {
    if (flags & CDS_TEST) return oChangeDisplaySettings(device, mode, flags, parameter);
    g_mode.Note();
    return DISP_CHANGE_SUCCESSFUL;
}

LONG WINAPI HookedSetDisplayConfig(UINT32 pathCount, void* paths, UINT32 modeCount, void* modes, UINT32 flags) {
    if (!(flags & SDC_APPLY)) return oSetDisplayConfig(pathCount, paths, modeCount, modes, flags);
    g_mode.Note();
    return ERROR_SUCCESS;
}

BOOL WINAPI HookedClipCursor(const RECT*) { g_clip.Note(); return TRUE; }
BOOL WINAPI HookedSetCursorPos(int, int) { g_cursor.Note(); return TRUE; }
BOOL WINAPI HookedSetPhysicalCursorPos(int, int) { g_cursor.Note(); return TRUE; }

BOOL WINAPI HookedRegisterRawInputDevices(PCRAWINPUTDEVICE devices, UINT count, UINT size) {
    // RIDEV_CAPTUREMOUSE and RIDEV_NOHOTKEYS are one bit.
    const DWORD background = RIDEV_INPUTSINK | RIDEV_EXINPUTSINK | RIDEV_CAPTUREMOUSE;
    if (!devices || size != sizeof(RAWINPUTDEVICE)) return oRegisterRawInputDevices(devices, count, size);
    std::vector<RAWINPUTDEVICE> quiet(devices, devices + count);
    for (RAWINPUTDEVICE& device : quiet) {
        if (!(device.dwFlags & RIDEV_REMOVE) && (device.dwFlags & background)) {
            g_rawSink.Note();
            device.dwFlags &= ~background;
        }
    }
    return oRegisterRawInputDevices(quiet.data(), count, size);
}

// ---------------------------------------------------------------------------
// COM objects, by a patch of the slot in their vtable
// ---------------------------------------------------------------------------

struct Patch {
    void** vtable;
    int slot;
    void* original;
};
SRWLOCK g_patchLock = SRWLOCK_INIT;
std::vector<Patch> g_patches;

void* OriginalOf(void* object, int slot) {
    void** vtable = *static_cast<void***>(object);
    void* found = nullptr;
    AcquireSRWLockShared(&g_patchLock);
    for (const Patch& patch : g_patches) if (patch.vtable == vtable && patch.slot == slot) found = patch.original;
    ReleaseSRWLockShared(&g_patchLock);
    return found;
}

void PatchSlot(void* object, int slot, void* detour) {
    void** vtable = *static_cast<void***>(object);
    AcquireSRWLockExclusive(&g_patchLock);
    bool known = false;
    for (const Patch& patch : g_patches) if (patch.vtable == vtable && patch.slot == slot) known = true;
    if (!known && vtable[slot] != detour) {
        DWORD old = 0;
        if (VirtualProtect(&vtable[slot], sizeof(void*), PAGE_READWRITE, &old)) {
            g_patches.push_back({vtable, slot, vtable[slot]});
            vtable[slot] = detour;
            VirtualProtect(&vtable[slot], sizeof(void*), old, &old);
        } else {
            Log("FAILED: a vtable slot could not be written (error %lu), so this object is not covered", GetLastError());
        }
    }
    ReleaseSRWLockExclusive(&g_patchLock);
}

// A window that was asked to be the whole screen, as a small window in the
// bottom right corner of the work area: at most 1280x720, in the proportions
// that were asked for.
void PlaceSmall(HWND window, UINT width, UINT height) {
    if (!window || !Ours(window)) return;
    if (!width || !height) { width = 1280; height = 720; }
    double scale = 1.0;
    if (width > 1280) scale = 1280.0 / width;
    if (height * scale > 720) scale = 720.0 / height;
    RECT frame{0, 0, static_cast<LONG>(width * scale), static_cast<LONG>(height * scale)};
    AdjustWindowRectEx(&frame, static_cast<DWORD>(GetWindowLongW(window, GWL_STYLE)), GetMenu(window) != nullptr,
                       static_cast<DWORD>(GetWindowLongW(window, GWL_EXSTYLE)));
    const int w = frame.right - frame.left, h = frame.bottom - frame.top;
    const POINT corner = CornerFor(w, h);
    oSetWindowPos(window, nullptr, corner.x, corner.y, w, h, SWP_NOACTIVATE | SWP_NOZORDER);
}

// --- Direct3D 9 ---

constexpr int kD3D9CreateDevice = 16, kD3D9CreateDeviceEx = 20, kDeviceReset = 16, kDeviceResetEx = 132;

bool MakeWindowed(D3DPRESENT_PARAMETERS* parameters) {
    if (!parameters || parameters->Windowed) return false;
    g_exclusive.Note();
    parameters->Windowed = TRUE;
    parameters->FullScreen_RefreshRateInHz = 0;
    // A windowed device takes no interval above one.
    if (parameters->PresentationInterval != D3DPRESENT_INTERVAL_DEFAULT && parameters->PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE) {
        parameters->PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    }
    return true;
}

// The game goes on believing what it asked for: it reads the structure back.
void AnswerFullscreen(D3DPRESENT_PARAMETERS* parameters) { parameters->Windowed = FALSE; }

HWND DeviceWindow(IDirect3DDevice9* device, const D3DPRESENT_PARAMETERS* parameters) {
    if (parameters->hDeviceWindow) return parameters->hDeviceWindow;
    D3DDEVICE_CREATION_PARAMETERS creation{};
    return SUCCEEDED(device->GetCreationParameters(&creation)) ? creation.hFocusWindow : nullptr;
}

HRESULT STDMETHODCALLTYPE HookedD3D9Reset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* parameters) {
    const auto original = static_cast<HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*)>(OriginalOf(device, kDeviceReset));
    const bool windowed = MakeWindowed(parameters);
    const HRESULT result = original(device, parameters);
    if (windowed && SUCCEEDED(result)) PlaceSmall(DeviceWindow(device, parameters), parameters->BackBufferWidth, parameters->BackBufferHeight);
    if (windowed) AnswerFullscreen(parameters);
    return result;
}

HRESULT STDMETHODCALLTYPE HookedD3D9ResetEx(IDirect3DDevice9Ex* device, D3DPRESENT_PARAMETERS* parameters, D3DDISPLAYMODEEX* mode) {
    const auto original =
        static_cast<HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*)>(OriginalOf(device, kDeviceResetEx));
    const bool windowed = MakeWindowed(parameters);
    const HRESULT result = original(device, parameters, windowed ? nullptr : mode);
    if (windowed && SUCCEEDED(result)) PlaceSmall(DeviceWindow(device, parameters), parameters->BackBufferWidth, parameters->BackBufferHeight);
    if (windowed) AnswerFullscreen(parameters);
    return result;
}

HRESULT STDMETHODCALLTYPE HookedD3D9CreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
                                                 D3DPRESENT_PARAMETERS* parameters, IDirect3DDevice9** device) {
    const auto original = static_cast<HRESULT (STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**)>(
        OriginalOf(d3d, kD3D9CreateDevice));
    const bool windowed = MakeWindowed(parameters);
    const HRESULT result = original(d3d, adapter, type, focus, flags, parameters, device);
    if (SUCCEEDED(result) && device && *device) {
        PatchSlot(*device, kDeviceReset, reinterpret_cast<void*>(&HookedD3D9Reset));
        if (windowed) PlaceSmall(parameters->hDeviceWindow ? parameters->hDeviceWindow : focus, parameters->BackBufferWidth, parameters->BackBufferHeight);
    }
    if (windowed) AnswerFullscreen(parameters);
    return result;
}

HRESULT STDMETHODCALLTYPE HookedD3D9CreateDeviceEx(IDirect3D9Ex* d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
                                                   D3DPRESENT_PARAMETERS* parameters, D3DDISPLAYMODEEX* mode, IDirect3DDevice9Ex** device) {
    const auto original =
        static_cast<HRESULT (STDMETHODCALLTYPE*)(IDirect3D9Ex*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*, IDirect3DDevice9Ex**)>(
            OriginalOf(d3d, kD3D9CreateDeviceEx));
    const bool windowed = MakeWindowed(parameters);
    const HRESULT result = original(d3d, adapter, type, focus, flags, parameters, windowed ? nullptr : mode, device);
    if (SUCCEEDED(result) && device && *device) {
        PatchSlot(*device, kDeviceReset, reinterpret_cast<void*>(&HookedD3D9Reset));
        PatchSlot(*device, kDeviceResetEx, reinterpret_cast<void*>(&HookedD3D9ResetEx));
        if (windowed) PlaceSmall(parameters->hDeviceWindow ? parameters->hDeviceWindow : focus, parameters->BackBufferWidth, parameters->BackBufferHeight);
    }
    if (windowed) AnswerFullscreen(parameters);
    return result;
}

using Direct3DCreate9_t = IDirect3D9* (WINAPI*)(UINT);
using Direct3DCreate9Ex_t = HRESULT (WINAPI*)(UINT, IDirect3D9Ex**);
Direct3DCreate9_t oDirect3DCreate9;
Direct3DCreate9Ex_t oDirect3DCreate9Ex;

IDirect3D9* WINAPI HookedDirect3DCreate9(UINT version) {
    IDirect3D9* d3d = oDirect3DCreate9(version);
    if (d3d) PatchSlot(d3d, kD3D9CreateDevice, reinterpret_cast<void*>(&HookedD3D9CreateDevice));
    return d3d;
}

HRESULT WINAPI HookedDirect3DCreate9Ex(UINT version, IDirect3D9Ex** d3d) {
    const HRESULT result = oDirect3DCreate9Ex(version, d3d);
    if (SUCCEEDED(result) && d3d && *d3d) {
        PatchSlot(*d3d, kD3D9CreateDevice, reinterpret_cast<void*>(&HookedD3D9CreateDevice));
        PatchSlot(*d3d, kD3D9CreateDeviceEx, reinterpret_cast<void*>(&HookedD3D9CreateDeviceEx));
    }
    return result;
}

// --- DXGI ---

constexpr int kFactoryCreateSwapChain = 10, kFactoryCreateSwapChainForHwnd = 15;
constexpr int kSwapChainSetFullscreenState = 10, kSwapChainGetFullscreenState = 11, kSwapChainResizeTarget = 14;

// The swap chains the game believes are fullscreen.
SRWLOCK g_fullscreenLock = SRWLOCK_INIT;
std::vector<void*> g_fullscreen;

void BelieveFullscreen(void* swapChain, bool fullscreen) {
    AcquireSRWLockExclusive(&g_fullscreenLock);
    for (size_t i = 0; i < g_fullscreen.size(); ++i) {
        if (g_fullscreen[i] == swapChain) {
            g_fullscreen.erase(g_fullscreen.begin() + static_cast<ptrdiff_t>(i));
            break;
        }
    }
    if (fullscreen) g_fullscreen.push_back(swapChain);
    ReleaseSRWLockExclusive(&g_fullscreenLock);
}

bool BelievedFullscreen(void* swapChain) {
    bool found = false;
    AcquireSRWLockShared(&g_fullscreenLock);
    for (void* one : g_fullscreen) if (one == swapChain) found = true;
    ReleaseSRWLockShared(&g_fullscreenLock);
    return found;
}

void PlaceSwapChainSmall(IDXGISwapChain* swapChain) {
    DXGI_SWAP_CHAIN_DESC description{};
    if (SUCCEEDED(swapChain->GetDesc(&description))) PlaceSmall(description.OutputWindow, description.BufferDesc.Width, description.BufferDesc.Height);
}

HRESULT STDMETHODCALLTYPE HookedSetFullscreenState(IDXGISwapChain* swapChain, BOOL fullscreen, IDXGIOutput*) {
    if (fullscreen) {
        g_exclusive.Note();
        PlaceSwapChainSmall(swapChain);
    }
    BelieveFullscreen(swapChain, fullscreen != FALSE);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE HookedGetFullscreenState(IDXGISwapChain* swapChain, BOOL* fullscreen, IDXGIOutput** target) {
    const bool believed = BelievedFullscreen(swapChain);
    if (fullscreen) *fullscreen = believed;
    if (target) {
        *target = nullptr;
        if (believed) swapChain->GetContainingOutput(target);
    }
    return S_OK;
}

// On a fullscreen swap chain this sets the display mode, and on a windowed one
// it resizes the window to the mode's size.
HRESULT STDMETHODCALLTYPE HookedResizeTarget(IDXGISwapChain* swapChain, const DXGI_MODE_DESC* mode) {
    if (BelievedFullscreen(swapChain)) {
        g_mode.Note();
        return S_OK;
    }
    const auto original = static_cast<HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, const DXGI_MODE_DESC*)>(OriginalOf(swapChain, kSwapChainResizeTarget));
    return original(swapChain, mode);
}

void CoverSwapChain(IDXGISwapChain* swapChain, bool askedFullscreen) {
    PatchSlot(swapChain, kSwapChainSetFullscreenState, reinterpret_cast<void*>(&HookedSetFullscreenState));
    PatchSlot(swapChain, kSwapChainGetFullscreenState, reinterpret_cast<void*>(&HookedGetFullscreenState));
    PatchSlot(swapChain, kSwapChainResizeTarget, reinterpret_cast<void*>(&HookedResizeTarget));
    // Also when it was not asked: a swap chain can be made where one that was let go used to be.
    BelieveFullscreen(swapChain, askedFullscreen);
    if (askedFullscreen) {
        g_exclusive.Note();
        PlaceSwapChainSmall(swapChain);
    }
}

HRESULT STDMETHODCALLTYPE HookedCreateSwapChain(IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* description, IDXGISwapChain** swapChain) {
    const auto original =
        static_cast<HRESULT (STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**)>(OriginalOf(factory, kFactoryCreateSwapChain));
    if (!description) return original(factory, device, description, swapChain);
    DXGI_SWAP_CHAIN_DESC windowed = *description;
    windowed.Windowed = TRUE;
    const HRESULT result = original(factory, device, &windowed, swapChain);
    if (SUCCEEDED(result) && swapChain && *swapChain) CoverSwapChain(*swapChain, !description->Windowed);
    return result;
}

HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForHwnd(IDXGIFactory2* factory, IUnknown* device, HWND window, const DXGI_SWAP_CHAIN_DESC1* description,
                                                       const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen, IDXGIOutput* output, IDXGISwapChain1** swapChain) {
    const auto original = static_cast<HRESULT (STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
                                                                   const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**)>(
        OriginalOf(factory, kFactoryCreateSwapChainForHwnd));
    const bool asked = fullscreen && !fullscreen->Windowed;
    const HRESULT result = original(factory, device, window, description, asked ? nullptr : fullscreen, output, swapChain);
    if (SUCCEEDED(result) && swapChain && *swapChain) CoverSwapChain(*swapChain, asked);
    return result;
}

void CoverFactory(IUnknown* made) {
    IDXGIFactory* factory = nullptr;
    if (SUCCEEDED(made->QueryInterface(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory)))) {
        PatchSlot(factory, kFactoryCreateSwapChain, reinterpret_cast<void*>(&HookedCreateSwapChain));
        factory->Release();
    }
    IDXGIFactory2* factory2 = nullptr;
    if (SUCCEEDED(made->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory2)))) {
        PatchSlot(factory2, kFactoryCreateSwapChainForHwnd, reinterpret_cast<void*>(&HookedCreateSwapChainForHwnd));
        factory2->Release();
    }
}

using CreateDXGIFactory_t = HRESULT (WINAPI*)(REFIID, void**);
using CreateDXGIFactory2_t = HRESULT (WINAPI*)(UINT, REFIID, void**);
CreateDXGIFactory_t oCreateDXGIFactory, oCreateDXGIFactory1;
CreateDXGIFactory2_t oCreateDXGIFactory2;

HRESULT WINAPI HookedCreateDXGIFactory(REFIID id, void** factory) {
    const HRESULT result = oCreateDXGIFactory(id, factory);
    if (SUCCEEDED(result) && factory && *factory) CoverFactory(static_cast<IUnknown*>(*factory));
    return result;
}
HRESULT WINAPI HookedCreateDXGIFactory1(REFIID id, void** factory) {
    const HRESULT result = oCreateDXGIFactory1(id, factory);
    if (SUCCEEDED(result) && factory && *factory) CoverFactory(static_cast<IUnknown*>(*factory));
    return result;
}
HRESULT WINAPI HookedCreateDXGIFactory2(UINT flags, REFIID id, void** factory) {
    const HRESULT result = oCreateDXGIFactory2(flags, id, factory);
    if (SUCCEEDED(result) && factory && *factory) CoverFactory(static_cast<IUnknown*>(*factory));
    return result;
}

using D3D11CreateDeviceAndSwapChain_t = HRESULT (WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT,
                                                          const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*,
                                                          ID3D11DeviceContext**);
D3D11CreateDeviceAndSwapChain_t oD3D11CreateDeviceAndSwapChain;

// Direct3D 11 makes this swap chain through a factory of its own that no export hands out.
HRESULT WINAPI HookedD3D11CreateDeviceAndSwapChain(IDXGIAdapter* adapter, D3D_DRIVER_TYPE type, HMODULE software, UINT flags, const D3D_FEATURE_LEVEL* levels,
                                                   UINT levelCount, UINT sdk, const DXGI_SWAP_CHAIN_DESC* description, IDXGISwapChain** swapChain,
                                                   ID3D11Device** device, D3D_FEATURE_LEVEL* level, ID3D11DeviceContext** context) {
    if (!description || !swapChain) {
        return oD3D11CreateDeviceAndSwapChain(adapter, type, software, flags, levels, levelCount, sdk, description, swapChain, device, level, context);
    }
    DXGI_SWAP_CHAIN_DESC windowed = *description;
    windowed.Windowed = TRUE;
    const HRESULT result = oD3D11CreateDeviceAndSwapChain(adapter, type, software, flags, levels, levelCount, sdk, &windowed, swapChain, device, level, context);
    if (SUCCEEDED(result) && *swapChain) CoverSwapChain(*swapChain, !description->Windowed);
    return result;
}

// --- DirectInput 8 ---

constexpr int kDirectInputCreateDevice = 3, kDeviceSetCooperativeLevel = 13;

HRESULT STDMETHODCALLTYPE HookedSetCooperativeLevel(void* device, HWND window, DWORD flags) {
    const auto original = static_cast<HRESULT (STDMETHODCALLTYPE*)(void*, HWND, DWORD)>(OriginalOf(device, kDeviceSetCooperativeLevel));
    if (flags & DISCL_EXCLUSIVE) {
        g_diExclusive.Note();
        flags = (flags & ~static_cast<DWORD>(DISCL_EXCLUSIVE | DISCL_NOWINKEY)) | DISCL_NONEXCLUSIVE;
    }
    return original(device, window, flags);
}

HRESULT STDMETHODCALLTYPE HookedDirectInputCreateDevice(void* directInput, REFGUID id, void** device, IUnknown* outer) {
    const auto original = static_cast<HRESULT (STDMETHODCALLTYPE*)(void*, REFGUID, void**, IUnknown*)>(OriginalOf(directInput, kDirectInputCreateDevice));
    const HRESULT result = original(directInput, id, device, outer);
    if (SUCCEEDED(result) && device && *device) PatchSlot(*device, kDeviceSetCooperativeLevel, reinterpret_cast<void*>(&HookedSetCooperativeLevel));
    return result;
}

using DirectInput8Create_t = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
DirectInput8Create_t oDirectInput8Create;

HRESULT WINAPI HookedDirectInput8Create(HINSTANCE instance, DWORD version, REFIID id, void** directInput, IUnknown* outer) {
    const HRESULT result = oDirectInput8Create(instance, version, id, directInput, outer);
    if (SUCCEEDED(result) && directInput && *directInput) PatchSlot(*directInput, kDirectInputCreateDevice, reinterpret_cast<void*>(&HookedDirectInputCreateDevice));
    return result;
}

// ---------------------------------------------------------------------------
// The processes this one starts
// ---------------------------------------------------------------------------

thread_local bool t_startingInjector = false;

BOOL WINAPI HookedCreateProcessInternal(HANDLE token, LPCWSTR application, LPWSTR commandLine, LPSECURITY_ATTRIBUTES processSecurity,
                                        LPSECURITY_ATTRIBUTES threadSecurity, BOOL inherit, DWORD flags, LPVOID environment, LPCWSTR directory,
                                        LPSTARTUPINFOW startup, LPPROCESS_INFORMATION made, PHANDLE newToken) {
    if (t_startingInjector || !made) {
        return oCreateProcessInternal(token, application, commandLine, processSecurity, threadSecurity, inherit, flags, environment, directory, startup, made,
                                      newToken);
    }
    if (!oCreateProcessInternal(token, application, commandLine, processSecurity, threadSecurity, inherit, flags | CREATE_SUSPENDED, environment, directory,
                                startup, made, newToken)) {
        return FALSE;
    }
    t_startingInjector = true;
    const std::wstring why = rig::Inject(made->hProcess, made->dwProcessId, g_folder, g_logFolder, false);
    t_startingInjector = false;
    if (!why.empty()) {
        Log("FAILED: the process this one started (pid %lu, %ls) could not be given the shim, so it was ended: %ls", made->dwProcessId,
            application ? application : (commandLine ? commandLine : L"?"), why.c_str());
        TerminateProcess(made->hProcess, 0xF07);
        CloseHandle(made->hThread);
        CloseHandle(made->hProcess);
        *made = PROCESS_INFORMATION{};
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    Log("started pid %lu with the shim in before its first instruction: %ls", made->dwProcessId, application ? application : (commandLine ? commandLine : L"?"));
    if (!(flags & CREATE_SUSPENDED)) ResumeThread(made->hThread);
    return TRUE;
}

// ---------------------------------------------------------------------------
// Going in
// ---------------------------------------------------------------------------

bool g_failed = false;

template <class T>
void Detour(const wchar_t* module, const char* name, void* detour, T& original, int stackBytes = -1) {
    const HMODULE loaded = GetModuleHandleW(module);
    auto* target = loaded ? reinterpret_cast<unsigned char*>(GetProcAddress(loaded, name)) : nullptr;
    if (!target) {
        Log("FAILED: %ls has no %s", module, name);
        g_failed = true;
        return;
    }
#ifndef _WIN64
    // A 32-bit system call stub is `mov eax, n; mov edx, x; call edx; ret bytes`. The
    // detour has to pop exactly what the stub pops, so a stub of another shape, or one
    // that takes another number of arguments than this was written for, stops the start.
    if (stackBytes >= 0) {
        const bool shape = target[0] == 0xB8 && target[5] == 0xBA && target[10] == 0xFF && target[11] == 0xD2;
        const int pops = target[12] == 0xC3 ? 0 : (target[12] == 0xC2 ? target[13] | (target[14] << 8) : -1);
        if (!shape || pops != stackBytes) {
            Log("FAILED: %s is not the system call stub this was written for (it pops %d bytes, expected %d)", name, pops, stackBytes);
            g_failed = true;
            return;
        }
    }
#else
    (void)stackBytes;
#endif
    const MH_STATUS created = MH_CreateHook(target, detour, reinterpret_cast<void**>(&original));
    if (created != MH_OK) {
        Log("FAILED: %s could not be detoured: %s", name, MH_StatusToString(created));
        g_failed = true;
    }
}

template <class T>
void DetourExport(HMODULE module, const char* name, void* detour, T& original) {
    void* target = reinterpret_cast<void*>(GetProcAddress(module, name));
    if (!target) return;
    const MH_STATUS status = MH_CreateHook(target, detour, reinterpret_cast<void**>(&original));
    if (status != MH_OK) Log("FAILED: %s could not be detoured (%s), so what goes through it is not covered", name, MH_StatusToString(status));
}

SRWLOCK g_moduleLock = SRWLOCK_INIT;
bool g_d3d9 = false, g_dxgi = false, g_d3d11 = false, g_dinput8 = false;

// Switching detours on stops every other thread while it writes, and finding those
// threads is the slow part, so it is done once for all of a module's detours.
void CoverModule(const wchar_t* name, HMODULE module, bool switchOn) {
    AcquireSRWLockExclusive(&g_moduleLock);
    if (!_wcsicmp(name, L"d3d9.dll") && !g_d3d9) {
        g_d3d9 = true;
        DetourExport(module, "Direct3DCreate9", reinterpret_cast<void*>(&HookedDirect3DCreate9), oDirect3DCreate9);
        DetourExport(module, "Direct3DCreate9Ex", reinterpret_cast<void*>(&HookedDirect3DCreate9Ex), oDirect3DCreate9Ex);
    } else if (!_wcsicmp(name, L"dxgi.dll") && !g_dxgi) {
        g_dxgi = true;
        DetourExport(module, "CreateDXGIFactory", reinterpret_cast<void*>(&HookedCreateDXGIFactory), oCreateDXGIFactory);
        DetourExport(module, "CreateDXGIFactory1", reinterpret_cast<void*>(&HookedCreateDXGIFactory1), oCreateDXGIFactory1);
        DetourExport(module, "CreateDXGIFactory2", reinterpret_cast<void*>(&HookedCreateDXGIFactory2), oCreateDXGIFactory2);
    } else if (!_wcsicmp(name, L"d3d11.dll") && !g_d3d11) {
        g_d3d11 = true;
        DetourExport(module, "D3D11CreateDeviceAndSwapChain", reinterpret_cast<void*>(&HookedD3D11CreateDeviceAndSwapChain), oD3D11CreateDeviceAndSwapChain);
    } else if (!_wcsicmp(name, L"dinput8.dll") && !g_dinput8) {
        g_dinput8 = true;
        DetourExport(module, "DirectInput8Create", reinterpret_cast<void*>(&HookedDirectInput8Create), oDirectInput8Create);
    }
    if (switchOn) {
        const MH_STATUS enabled = MH_EnableHook(MH_ALL_HOOKS);
        if (enabled != MH_OK) Log("FAILED: the detours for %ls could not be switched on (%s), so what goes through it is not covered", name, MH_StatusToString(enabled));
    }
    ReleaseSRWLockExclusive(&g_moduleLock);
}

constexpr const wchar_t* kLateModules[] = {L"d3d9.dll", L"dxgi.dll", L"d3d11.dll", L"dinput8.dll"};

struct UnicodeString {
    USHORT length, maximumLength;
    const wchar_t* buffer;
};
struct DllNotification {
    ULONG flags;
    const UnicodeString* fullName;
    const UnicodeString* baseName;
    void* base;
    ULONG size;
};

void CALLBACK OnDllLoaded(ULONG reason, const DllNotification* data, void*) {
    if (reason != 1) return;
    const std::wstring name(data->baseName->buffer, data->baseName->length / sizeof(wchar_t));
    for (const wchar_t* late : kLateModules) {
        if (!_wcsicmp(name.c_str(), late)) CoverModule(late, static_cast<HMODULE>(data->base), true);
    }
}

BOOL CALLBACK QuietExisting(HWND window, LPARAM count) {
    if (!Ours(window)) return TRUE;
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
#ifdef _WIN64
    oSetWindowLongPtr(window, GWL_EXSTYLE, style | WS_EX_NOACTIVATE, FALSE);
#else
    oSetWindowLong(window, GWL_EXSTYLE, style | WS_EX_NOACTIVATE, FALSE);
#endif
    if (IsWindowVisible(window)) {
        Corner(window);
        ++*reinterpret_cast<int*>(count);
    }
    return TRUE;
}

// Changing a style sends the window a message, and its thread may be waiting for the loader
// lock the shim holds while it loads, so this runs on a thread of its own once the load is over.
DWORD WINAPI QuietExistingWindows(void*) {
    int visible = 0;
    EnumWindows(&QuietExisting, reinterpret_cast<LPARAM>(&visible));
    Log("LATE: the shim went into a process that was already running, with %d visible window(s). They were made non-activating, and those with no owner that are "
        "not tool windows or maximised were put in the corner. "
        "A Direct3D device, a swap chain or a DirectInput device it made before now is not covered",
        visible);
    return 0;
}

bool Start(HMODULE self) {
    const ULONGLONG began = GetTickCount64();
    const HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, rig::ShimConfigName(GetCurrentProcessId()).c_str());
    if (!mapping) return false;
    const auto* config = static_cast<const rig::ShimConfig*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(rig::ShimConfig)));
    if (!config) {
        CloseHandle(mapping);
        return false;
    }
    g_logFolder = config->logFolder;
    const bool running = config->running != FALSE;
    UnmapViewOfFile(config);
    CloseHandle(mapping);
    g_folder = rig::FolderOf(self);

    wchar_t exe[MAX_PATH * 2];
    GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    const wchar_t* leaf = wcsrchr(exe, L'\\');
    g_logPath = g_logFolder + L"\\rigshim." + (leaf ? leaf + 1 : exe) + L"." + std::to_wstring(GetCurrentProcessId()) + L".log";

    LoadLibraryW(L"user32.dll");
    const wchar_t* const win32u = L"win32u.dll";
    const wchar_t* const user32 = L"user32.dll";
    const MH_STATUS initialised = MH_Initialize();
    if (initialised != MH_OK) {
        Log("FAILED: MinHook did not initialise: %s", MH_StatusToString(initialised));
        return false;
    }
    Detour(win32u, "NtUserCreateWindowEx", reinterpret_cast<void*>(&HookedCreateWindow), oCreateWindow, 17 * 4);
    Detour(win32u, "NtUserShowWindow", reinterpret_cast<void*>(&HookedShowWindow), oShowWindow, 2 * 4);
    Detour(win32u, "NtUserShowWindowAsync", reinterpret_cast<void*>(&HookedShowWindowAsync), oShowWindowAsync, 2 * 4);
    Detour(win32u, "NtUserSetWindowPos", reinterpret_cast<void*>(&HookedSetWindowPos), oSetWindowPos, 7 * 4);
    Detour(win32u, "NtUserMoveWindow", reinterpret_cast<void*>(&HookedMoveWindow), oMoveWindow, 6 * 4);
    Detour(win32u, "NtUserSetWindowPlacement", reinterpret_cast<void*>(&HookedSetWindowPlacement), oSetWindowPlacement, 2 * 4);
    Detour(win32u, "NtUserSetWindowLong", reinterpret_cast<void*>(&HookedSetWindowLong), oSetWindowLong, 4 * 4);
#ifdef _WIN64
    Detour(win32u, "NtUserSetWindowLongPtr", reinterpret_cast<void*>(&HookedSetWindowLongPtr), oSetWindowLongPtr);
#endif
    Detour(win32u, "NtUserSetForegroundWindow", reinterpret_cast<void*>(&HookedSetForegroundWindow), oSetForegroundWindow, 1 * 4);
    Detour(win32u, "NtUserSwitchToThisWindow", reinterpret_cast<void*>(&HookedSwitchToThisWindow), oSwitchToThisWindow, 2 * 4);
    Detour(win32u, "NtUserFlashWindowEx", reinterpret_cast<void*>(&HookedFlashWindowEx), oFlashWindowEx, 1 * 4);
    Detour(win32u, "NtUserAttachThreadInput", reinterpret_cast<void*>(&HookedAttachThreadInput), oAttachThreadInput, 3 * 4);
    Detour(win32u, "NtUserLockSetForegroundWindow", reinterpret_cast<void*>(&HookedLockSetForegroundWindow), oLockSetForegroundWindow, 1 * 4);
    Detour(win32u, "NtUserAllowSetForegroundWindow", reinterpret_cast<void*>(&HookedAllowSetForegroundWindow), oAllowSetForegroundWindow, 1 * 4);
    Detour(win32u, "NtUserSendInput", reinterpret_cast<void*>(&HookedSendInput), oSendInput, 3 * 4);
    Detour(win32u, "NtUserChangeDisplaySettings", reinterpret_cast<void*>(&HookedChangeDisplaySettings), oChangeDisplaySettings, 4 * 4);
    Detour(win32u, "NtUserSetDisplayConfig", reinterpret_cast<void*>(&HookedSetDisplayConfig), oSetDisplayConfig, 5 * 4);
    Detour(win32u, "NtUserClipCursor", reinterpret_cast<void*>(&HookedClipCursor), oClipCursor, 1 * 4);
    Detour(win32u, "NtUserSetCursorPos", reinterpret_cast<void*>(&HookedSetCursorPos), oSetCursorPos, 2 * 4);
    Detour(win32u, "NtUserRegisterRawInputDevices", reinterpret_cast<void*>(&HookedRegisterRawInputDevices), oRegisterRawInputDevices, 3 * 4);
    Detour(user32, "BringWindowToTop", reinterpret_cast<void*>(&HookedBringWindowToTop), oBringWindowToTop);
    Detour(user32, "DeferWindowPos", reinterpret_cast<void*>(&HookedDeferWindowPos), oDeferWindowPos);
    Detour(user32, "SetPhysicalCursorPos", reinterpret_cast<void*>(&HookedSetPhysicalCursorPos), oSetPhysicalCursorPos);
    Detour(L"kernelbase.dll", "CreateProcessInternalW", reinterpret_cast<void*>(&HookedCreateProcessInternal), oCreateProcessInternal);
    for (const wchar_t* late : kLateModules) {
        if (const HMODULE module = GetModuleHandleW(late)) CoverModule(late, module, false);
    }
    if (!g_failed) {
        const MH_STATUS enabled = MH_EnableHook(MH_ALL_HOOKS);
        if (enabled != MH_OK) {
            Log("FAILED: the detours could not be switched on: %s", MH_StatusToString(enabled));
            g_failed = true;
        }
    }
    if (g_failed) {
        MH_Uninitialize();
        return false;
    }

    if (running) {
        Log("in a process that was already running, in %llu ms: %ls", GetTickCount64() - began, exe);
        const HANDLE thread = CreateThread(nullptr, 0, &QuietExistingWindows, nullptr, 0, nullptr);
        if (!thread) {
            Log("FAILED: no thread to make the windows it already has non-activating (error %lu)", GetLastError());
            MH_Uninitialize();
            return false;
        }
        CloseHandle(thread);
    } else {
        Log("in before the first instruction, in %llu ms: %ls", GetTickCount64() - began, exe);
    }
    using Register_t = LONG (NTAPI*)(ULONG, void (CALLBACK*)(ULONG, const DllNotification*, void*), void*, void**);
    const auto registerNotification = reinterpret_cast<Register_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification"));
    void* cookie = nullptr;
    if (!registerNotification || registerNotification(0, &OnDllLoaded, nullptr, &cookie) != 0) {
        Log("FAILED: no notice of DLLs loading, so Direct3D and DirectInput could not be covered");
        MH_Uninitialize();
        return false;
    }

    return CreateEventW(nullptr, TRUE, TRUE, rig::ShimEventName(GetCurrentProcessId()).c_str()) != nullptr;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    return Start(self) ? TRUE : FALSE;
}
