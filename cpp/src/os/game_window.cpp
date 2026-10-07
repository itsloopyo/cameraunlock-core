#include <cameraunlock/os/game_window.h>

#ifdef _WIN32

#include <atomic>
#include <cstdarg>
#include <cstdio>

namespace cameraunlock::os {

namespace {

struct EnumState {
    DWORD pid = 0;
    HWND hwnd = nullptr;
};

BOOL CALLBACK PickGameWindow(HWND hwnd, LPARAM lParam) {
    auto* state = reinterpret_cast<EnumState*>(lParam);

    DWORD wndPid = 0;
    GetWindowThreadProcessId(hwnd, &wndPid);
    if (wndPid != state->pid) return TRUE;

    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;

    RECT r{};
    if (IsIconic(hwnd)) {
        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        if (!GetWindowPlacement(hwnd, &placement)) return TRUE;
        r = placement.rcNormalPosition;
    } else if (!GetWindowRect(hwnd, &r)) {
        return TRUE;
    }
    if ((r.right - r.left) < 200 || (r.bottom - r.top) < 200) return TRUE;

    state->hwnd = hwnd;
    return FALSE;
}

void Emit(WindowLogFn log, WindowLogLevel level, const char* fmt, ...) {
    if (log == nullptr) return;
    char message[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    log(level, message);
}

}  // namespace

HWND FindGameWindow() {
    EnumState state;
    state.pid = GetCurrentProcessId();
    EnumWindows(PickGameWindow, reinterpret_cast<LPARAM>(&state));
    return state.hwnd;
}

void CenterGameWindowOnce(WindowLogFn log) {
    static std::atomic<bool> s_centered{false};
    if (s_centered.exchange(true, std::memory_order_acq_rel)) return;

    HWND hwnd = FindGameWindow();
    if (!hwnd) {
        Emit(log, WindowLogLevel::Warning, "window: no candidate top-level window found");
        return;
    }

    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    bool requested = SetForegroundWindow(hwnd) != FALSE;
    if (!requested) {
        Emit(log, WindowLogLevel::Warning, "window: Windows refused foreground activation; requesting Alt unlock");
        // Windows releases its foreground lock on Alt. Do not release a key the
        // player is holding or combine the pulse with an existing modifier chord.
        const bool modifierHeld = ((GetAsyncKeyState(VK_MENU) | GetAsyncKeyState(VK_CONTROL)
            | GetAsyncKeyState(VK_SHIFT) | GetAsyncKeyState(VK_LWIN)
            | GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0;
        if (modifierHeld) {
            Emit(log, WindowLogLevel::Warning, "window: foreground unlock skipped while a modifier is held");
        } else {
            INPUT inputs[2]{};
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = VK_MENU;
            inputs[1] = inputs[0];
            inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
            const UINT sent = SendInput(2, inputs, sizeof(INPUT));
            if (sent != 2) {
                Emit(log, WindowLogLevel::Warning, "window: Alt unlock inserted %u/2 events (error %lu)", sent, GetLastError());
                if (sent == 1 && SendInput(1, &inputs[1], sizeof(INPUT)) != 1) {
                    Emit(log, WindowLogLevel::Warning, "window: Alt release failed: %lu", GetLastError());
                }
            } else {
                requested = SetForegroundWindow(hwnd) != FALSE;
            }
        }
    }
    // Cross-thread activation is asynchronous; let the window process it before
    // checking the foreground handle, without waiting on a hung game indefinitely.
    DWORD_PTR response = 0;
    if (requested && !SendMessageTimeoutW(hwnd, WM_NULL, 0, 0,
                                         SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &response)) {
        Emit(log, WindowLogLevel::Warning, "window: activation acknowledgement timed out or failed: %lu", GetLastError());
    }
    const bool foreground = GetForegroundWindow() == hwnd;
    Emit(log, foreground ? WindowLogLevel::Info : WindowLogLevel::Warning,
         "window: foreground verified=%d request accepted=%d", foreground, requested);

    // HWND_TOP alone can leave the window behind the foreground application.
    // Briefly use the topmost band, then restore the original topmost state.
    const bool wasTopmost = (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    constexpr UINT raiseFlags = SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    if (!SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, raiseFlags)) {
        Emit(log, WindowLogLevel::Warning, "window: raising window failed: %lu", GetLastError());
        return;
    }
    if (!wasTopmost && !SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, raiseFlags)) {
        Emit(log, WindowLogLevel::Warning, "window: restoring non-topmost state failed: %lu", GetLastError());
        return;
    }
    Emit(log, WindowLogLevel::Info, "window: raised window; original topmost state=%d", wasTopmost);

    if ((GetWindowLongW(hwnd, GWL_STYLE) & WS_CAPTION) == 0) {
        Emit(log, WindowLogLevel::Info, "window: borderless/fullscreen window, leaving position unchanged");
        return;
    }

    RECT win{};
    if (!GetWindowRect(hwnd, &win)) {
        Emit(log, WindowLogLevel::Warning, "window: GetWindowRect failed");
        return;
    }
    int winW = win.right - win.left;
    int winH = win.bottom - win.top;

    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(mon, &info)) {
        Emit(log, WindowLogLevel::Warning, "window: GetMonitorInfoW failed");
        return;
    }
    const RECT& work = info.rcWork;
    int workW = work.right - work.left;
    int workH = work.bottom - work.top;

    if (winW >= workW || winH >= workH) {
        Emit(log, WindowLogLevel::Info,
             "window: window %dx%d fills work area %dx%d, leaving in place",
             winW, winH, workW, workH);
        return;
    }

    int newX = CenteredOrigin(work.left, workW, winW);
    int newY = CenteredOrigin(work.top, workH, winH);
    if (!SetWindowPos(hwnd, HWND_TOP, newX, newY, 0, 0,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        Emit(log, WindowLogLevel::Warning, "window: SetWindowPos failed: %lu", GetLastError());
        return;
    }
    Emit(log, WindowLogLevel::Info,
         "window: centered %dx%d window at (%d, %d) on work area %dx%d",
         winW, winH, newX, newY, workW, workH);
}

bool CenterWindowInWorkArea(HWND hwnd, WindowLogFn log) {
    if (!IsWindow(hwnd)) {
        Emit(log, WindowLogLevel::Warning, "window: %p is not a window", static_cast<void*>(hwnd));
        return false;
    }
    // A minimised window reports a small rect far off screen in place of its own.
    if (IsIconic(hwnd)) {
        Emit(log, WindowLogLevel::Info, "window: minimised, leaving position unchanged");
        return false;
    }
    if (IsZoomed(hwnd)) {
        Emit(log, WindowLogLevel::Info, "window: maximised, leaving position unchanged");
        return false;
    }
    if ((GetWindowLongW(hwnd, GWL_STYLE) & WS_CAPTION) == 0) {
        Emit(log, WindowLogLevel::Info, "window: borderless/fullscreen window, leaving position unchanged");
        return false;
    }

    RECT win{};
    if (!GetWindowRect(hwnd, &win)) {
        Emit(log, WindowLogLevel::Warning, "window: GetWindowRect failed: %lu", GetLastError());
        return false;
    }
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info)) {
        Emit(log, WindowLogLevel::Warning, "window: GetMonitorInfoW failed");
        return false;
    }
    const RECT& work = info.rcWork;
    const int winW = win.right - win.left;
    const int winH = win.bottom - win.top;
    const int workW = work.right - work.left;
    const int workH = work.bottom - work.top;

    // Nowhere on the work area shows all of it, and centring a window taller
    // than the work area puts its title bar above the top, out of reach.
    if (winW > workW || winH > workH) {
        Emit(log, WindowLogLevel::Info,
             "window: %dx%d at (%d, %d) does not fit work area %dx%d at (%d, %d), leaving position unchanged",
             winW, winH, static_cast<int>(win.left), static_cast<int>(win.top),
             workW, workH, static_cast<int>(work.left), static_cast<int>(work.top));
        return false;
    }

    const int newX = CenteredOrigin(work.left, workW, winW);
    const int newY = CenteredOrigin(work.top, workH, winH);
    if (newX == win.left && newY == win.top) return true;

    if (!SetWindowPos(hwnd, nullptr, newX, newY, 0, 0,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        Emit(log, WindowLogLevel::Warning, "window: SetWindowPos failed: %lu", GetLastError());
        return false;
    }
    Emit(log, WindowLogLevel::Info,
         "window: centered %dx%d at (%d, %d) on work area %dx%d at (%d, %d), was at (%d, %d)",
         winW, winH, newX, newY, workW, workH,
         static_cast<int>(work.left), static_cast<int>(work.top),
         static_cast<int>(win.left), static_cast<int>(win.top));
    return true;
}

}  // namespace cameraunlock::os

#endif  // _WIN32
