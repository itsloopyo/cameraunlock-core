#pragma once

#ifdef _WIN32
#include <Windows.h>
#endif

namespace cameraunlock::os {

/// Where a window `windowLength` long starts, on one axis, to sit in the middle
/// of a work area `workLength` long that starts at `workStart`. Call it once
/// for x and once for y. `workStart` is not zero on a secondary monitor and is
/// negative on one left of or above the primary.
///
/// The half is rounded toward zero. An odd gap leaves the spare pixel after
/// the window, and a window longer than the area starts before `workStart`: a
/// caller that must not move such a window checks the lengths first, as
/// CenterWindowInWorkArea does.
constexpr int CenteredOrigin(int workStart, int workLength, int windowLength) noexcept {
    return workStart + (workLength - windowLength) / 2;
}

#ifdef _WIN32

/// Severity of a game-window diagnostic. The numbering matches
/// cameraunlock::reframework::LogLevel so a REFramework mod's forwarder is a
/// straight mapping, but this enum is the one an ordinary mod binds against -
/// nothing here depends on REFramework being present.
enum class WindowLogLevel { Info = 0, Warning = 1 };

/// Sink for those diagnostics. The message arrives already formatted, so a
/// forwarder never has to relay a va_list. Null means no diagnostics.
using WindowLogFn = void (*)(WindowLogLevel level, const char* message);

/// The game's main top-level window: owned by this process, visible, not an
/// owned pop-up, and at least 200x200. Null when no candidate exists - which is
/// the normal answer during early startup, before the engine has created it.
///
/// The size floor is what separates the real window from the splash, tooltip
/// and message-only windows a game creates alongside it, all of which are
/// visible and unowned too.
HWND FindGameWindow();

/// Restores and raises FindGameWindow()'s result, requests foreground activation,
/// and centres it on its monitor's work area, once per process. Later calls are
/// no-ops. Borderless windows and windows filling the work area keep their position.
/// If activation is refused, sends one Alt press/release when no modifier is held
/// and requests activation again. Logs whether the game actually became foreground.
///
/// The work area, not the monitor bounds: centring against the full monitor
/// puts the title bar behind the taskbar on a top-docked one, and the window
/// cannot then be dragged back.
void CenterGameWindowOnce(WindowLogFn log);

/// Centres `hwnd` in the work area of the monitor it is on, and can be called
/// after every placement: when the game creates its window, changes its
/// windowed resolution or comes back from fullscreen. It keeps no state, moves
/// the window and nothing else (no resize, no activation, no z-order change),
/// and does not look for the window: the caller passes the one it means.
///
/// True when the window is at the centred origin on return, whether this call
/// moved it or it was there already. False when it was left where it was:
/// `hwnd` is not a window, it is minimised, maximised or has no caption
/// (borderless and exclusive fullscreen), it is wider or taller than the work
/// area, or a Win32 call failed.
///
/// A move logs the window's size, the work area and both origins, so a report
/// from another display can be checked by arithmetic. A window left alone logs
/// why. A window already centred logs nothing, since a game may place its
/// window many times.
///
/// The window and the work area are read in the calling thread's coordinates
/// and the origin is written back in the same, so nothing here converts for
/// DPI.
bool CenterWindowInWorkArea(HWND hwnd, WindowLogFn log);

#endif  // _WIN32

}  // namespace cameraunlock::os
