// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Copyright (c) 2026 itsloopyo

#pragma once

#ifndef _WIN32
#error "cameraunlock/hooks/hook_or_point.h is Windows-only: it hooks code in the running process."
#endif

#include <string>
#include <vector>

#include <cameraunlock/hooks/vtable_slot.h>

namespace cameraunlock::hooks {

enum class HookedBy { MinHook, Vtables };

// Hooks `function` with `detour` through HookManager, which must be initialised, and turns the hook on; `original`
// gets the way to call the game's own. Where MinHook finds no memory near the function for its trampoline
// (Monster Hunter: World reserves the space around its image before a mod is in, seen 2026-11-11 by the Headstart MT
// Framework backend), each vtable entry in `entries`, every one of which must hold `function`, is pointed at `detour`
// instead, recorded in `pointed`, and `original` gets `function` itself: only calls made through those entries are
// detoured, and `pointed.RestoreAll()` puts them back. Throws naming `what` when the hook fails any other way, when
// it cannot be turned on, when no entry is given, or when an entry does not hold `function`.
HookedBy HookOrPointVtables(void* function, const std::vector<void**>& entries, void* detour, void** original, SlotSwaps& pointed,
                            const std::string& what);

}  // namespace cameraunlock::hooks
