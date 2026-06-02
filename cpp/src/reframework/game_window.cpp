#include <cameraunlock/reframework/game_window.h>
#include <cameraunlock/reframework/log_callback.h>

#include <windows.h>

#include <atomic>

namespace cameraunlock::reframework {

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

} // namespace

void CenterGameWindowOnce() {
    static std::atomic<bool> s_centered{false};
    if (s_centered.exchange(true, std::memory_order_acq_rel)) return;

    EnumState state;
    state.pid = GetCurrentProcessId();
    EnumWindows(PickGameWindow, reinterpret_cast<LPARAM>(&state));
    if (!state.hwnd) {
        Log(LogLevel::Warning, "window: no candidate top-level window found");
        return;
    }

    RECT win{};
    if (!GetWindowRect(state.hwnd, &win)) {
        Log(LogLevel::Warning, "window: GetWindowRect failed");
        return;
    }
    int winW = win.right - win.left;
    int winH = win.bottom - win.top;

    HMONITOR mon = MonitorFromWindow(state.hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(mon, &info)) {
        Log(LogLevel::Warning, "window: GetMonitorInfoW failed");
        return;
    }
    const RECT& work = info.rcWork;
    int workW = work.right - work.left;
    int workH = work.bottom - work.top;

    if (winW >= workW || winH >= workH) {
        Log(LogLevel::Info,
            "window: window %dx%d fills work area %dx%d, leaving in place",
            winW, winH, workW, workH);
        return;
    }

    int newX = work.left + (workW - winW) / 2;
    int newY = work.top + (workH - winH) / 2;
    if (!SetWindowPos(state.hwnd, HWND_TOP, newX, newY, 0, 0,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        Log(LogLevel::Warning, "window: SetWindowPos failed: %lu", GetLastError());
        return;
    }
    Log(LogLevel::Info,
        "window: centered %dx%d window at (%d, %d) on work area %dx%d",
        winW, winH, newX, newY, workW, workH);
}

} // namespace cameraunlock::reframework
