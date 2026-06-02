#pragma once

namespace cameraunlock::reframework {

// Center the game's main top-level window on its monitor's work area, once
// per process; subsequent calls are no-ops. Windows that already fill the
// work area (fullscreen / borderless) are left in place. Diagnostics go
// through the reframework log callback (log_callback.h).
void CenterGameWindowOnce();

} // namespace cameraunlock::reframework
