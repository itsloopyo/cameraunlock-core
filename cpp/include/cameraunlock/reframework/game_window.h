#pragma once

namespace cameraunlock::reframework {

// Restore, raise, request foreground activation and center the game's window,
// once per process; subsequent calls are no-ops. Borderless windows and windows
// that already fill the work area keep their position. Diagnostics go
// through the reframework log callback (log_callback.h).
//
// The routine itself now lives in cameraunlock/os/game_window.h, in the
// always-on target, so a non-REFramework mod can use it too. This stays as the
// REFramework-flavoured entry point: RE mods call it by this name.
void CenterGameWindowOnce();

} // namespace cameraunlock::reframework
