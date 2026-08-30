#pragma once

namespace cameraunlock::reframework {

// Center the game's main top-level window on its monitor's work area, once
// per process; subsequent calls are no-ops. Windows that already fill the
// work area (fullscreen / borderless) are left in place. Diagnostics go
// through the reframework log callback (log_callback.h).
//
// The routine itself now lives in cameraunlock/os/game_window.h, in the
// always-on target, so a non-REFramework mod can use it too. This stays as the
// REFramework-flavoured entry point: RE mods call it by this name.
void CenterGameWindowOnce();

} // namespace cameraunlock::reframework
