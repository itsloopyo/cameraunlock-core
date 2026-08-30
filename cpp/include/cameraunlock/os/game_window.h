#pragma once

#ifdef _WIN32
#include <Windows.h>
#endif

namespace cameraunlock::os {

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

/// Centres FindGameWindow()'s result on its monitor's work area, once per
/// process; later calls are no-ops. Windows that already fill the work area
/// (fullscreen or borderless) are left alone.
///
/// The work area, not the monitor bounds: centring against the full monitor
/// puts the title bar behind the taskbar on a top-docked one, and the window
/// cannot then be dragged back.
void CenterGameWindowOnce(WindowLogFn log);

#endif  // _WIN32

}  // namespace cameraunlock::os
