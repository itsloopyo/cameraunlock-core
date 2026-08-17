#pragma once

namespace cameraunlock::diagnostics {

// Installs an unhandled-exception filter that writes a crash report (exception
// code, fault address resolved to module+RVA, access-violation details, and a
// module+RVA stack walk) to the process log via
// cameraunlock::logging::EmergencyLine, then returns EXCEPTION_CONTINUE_SEARCH
// so the game's / OS's normal crash flow still runs (WER dump, host's filter).
//
// Deliberately NOT a vectored exception handler: mods routinely probe
// potentially-unmapped memory inside __try/__except (SEH-guarded reads), and a
// vectored handler would log every one of those expected first-chance AVs as a
// false-positive crash, drowning out real signal. The trade-off is that crashes
// the engine catches in its own top-level filter never reach us; startup
// crashes (the common reporter case) happen before the engine installs its
// filter, so this filter wins there.
//
// Call once, as early as possible after logging::Open. Safe to call before any
// hook is installed.
//
// The loaded-module map is snapshotted here, not looked up from the filter:
// GetModuleHandleEx / GetModuleBaseName take the loader lock, and a crash that
// already holds it would hang the game instead of reporting. Addresses in
// modules loaded after this call print raw rather than as module+RVA, so call
// it late enough that the engine's own modules are in.
void InstallCrashHandler();

}  // namespace cameraunlock::diagnostics
