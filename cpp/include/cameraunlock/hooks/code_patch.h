// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Copyright (c) 2026 itsloopyo

#pragma once

#ifndef _WIN32
#error "cameraunlock/hooks/code_patch.h is Windows-only: it patches code in the running process."
#endif

#include <cstddef>
#include <cstdint>

// Patching a game's code by hand where MinHook cannot. A patch is prepared
// first, which can fail without touching the game, then swapped in with one
// locked store, since the game's other threads may be running the code. `what`
// names the code in errors.
namespace cameraunlock::hooks::code_patch {

#ifdef _WIN64
// One allocation granule, committed read-write, within 2 GB below `site`. The caller writes its code and then
// makes the pages it used executable and read-only. x64 only: on x86 a rel32 reaches the whole address space.
std::uint8_t* AllocNear(std::uintptr_t site, const char* what);

// jmp [rip + 0] and the 8-byte address it reads: 14 bytes at `at`.
constexpr std::size_t kJumpBytes = 14;
void JumpAbsolute(std::uint8_t* at, const void* to);
#endif

// A rel32 call about to be pointed somewhere else: `was` is the displacement PrepareRedirect read, `rel` the one it
// is to hold.
struct Redirect {
    std::uint8_t* call = nullptr;
    std::int32_t rel = 0;
    std::int32_t was = 0;
};

// The call at `site`, which must call `expected`, pointed at `to`: on x64 through a jump AllocNear places within
// the call's reach, on x86 straight at it. Throws when the call is not that one or its displacement cannot be
// rewritten in one store (it crosses an aligned 16-byte block on x64, an aligned 8-byte word on x86). On x86 that
// refuses three call sites in eight; nothing here stops the game's threads to write the rest.
Redirect PrepareRedirect(std::uintptr_t site, void* to, std::uintptr_t expected, const char* what);

// Writes the prepared displacement in one compare-exchange of the block around it, from the displacement
// PrepareRedirect read. Throws when the call no longer holds that one (another mod changed it in between), when
// the block around it changed during the store, or when its protection could not be changed or put back.
void ApplyRedirect(const Redirect& r, const char* what);

// The `size` bytes at `at`, which must hold `expected`, replaced by `replacement` in one compare-exchange, the
// code's protection put back after and the instruction cache flushed. The bytes must lie in one aligned 8-byte
// word, or on x64 in one aligned 16-byte block. Throws when they do not, when `expected` is null, when they do not
// hold `expected`, or when
// the protection could not be changed or put back.
void SwapCode(void* at, const void* expected, const void* replacement, std::size_t size, const char* what);

}  // namespace cameraunlock::hooks::code_patch
