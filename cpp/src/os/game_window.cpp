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
    if (!GetWindowRect(hwnd, &r)) return TRUE;
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

    int newX = work.left + (workW - winW) / 2;
    int newY = work.top + (workH - winH) / 2;
    if (!SetWindowPos(hwnd, HWND_TOP, newX, newY, 0, 0,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        Emit(log, WindowLogLevel::Warning, "window: SetWindowPos failed: %lu", GetLastError());
        return;
    }
    Emit(log, WindowLogLevel::Info,
         "window: centered %dx%d window at (%d, %d) on work area %dx%d",
         winW, winH, newX, newY, workW, workH);
}

}  // namespace cameraunlock::os

#endif  // _WIN32
