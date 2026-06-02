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
void InstallCrashHandler();

}  // namespace cameraunlock::diagnostics
