// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Copyright (c) 2026 itsloopyo

#pragma once

// Another mod's hook on a game function: a jump written over the function's
// first bytes that leads out of the game's module. MinHook (which most mods
// use, the CameraUnlock ones included) writes `jmp rel32` to a relay it
// allocates near the function, and the relay jumps on with `jmp [rip+0]` to the
// detour in the mod's DLL. A mod that finds the function in the game's code
// and hooks it as well would put two mods on one camera.

#include <cstdint>
#include <cstring>
#include <optional>

namespace cameraunlock::hooks::planted {

// One jump at `at`, read through `read(addr, out, size) -> bool`: where it
// goes, or nothing when the bytes there are not a jump this knows.
//   E9 rel32            jmp rel32
//   EB rel8             jmp rel8
//   FF 25 disp32        jmp [rip+disp32] on x64, jmp [disp32] on x86 (the target is read from memory)
//   48 B8 imm64 FF E0   mov rax, imm64; jmp rax
template <class Read>
std::optional<std::uintptr_t> JumpAt(const Read& read, std::uintptr_t at) {
    std::uint8_t b[12] = {};
    if (!read(at, b, 2)) return std::nullopt;
    if (b[0] == 0xE9) {
        if (!read(at, b, 5)) return std::nullopt;
        std::int32_t rel;
        std::memcpy(&rel, b + 1, 4);
        return at + 5 + static_cast<std::intptr_t>(rel);
    }
    if (b[0] == 0xEB) return at + 2 + static_cast<std::intptr_t>(static_cast<std::int8_t>(b[1]));
    if (b[0] == 0xFF && b[1] == 0x25) {
        if (!read(at, b, 6)) return std::nullopt;
        std::int32_t disp;
        std::memcpy(&disp, b + 2, 4);
        // x64 reads the target relative to the next instruction, x86 at the absolute address.
        const std::uintptr_t slot = sizeof(std::uintptr_t) == 8 ? at + 6 + static_cast<std::intptr_t>(disp)
                                                                 : static_cast<std::uintptr_t>(static_cast<std::uint32_t>(disp));
        std::uintptr_t target = 0;
        if (!read(slot, &target, sizeof(target))) return std::nullopt;
        return target;
    }
    if (b[0] == 0x48 && b[1] == 0xB8 && sizeof(std::uintptr_t) == 8) {
        if (!read(at, b, 12) || b[10] != 0xFF || b[11] != 0xE0) return std::nullopt;
        std::uint64_t target = 0;
        std::memcpy(&target, b + 2, 8);
        return static_cast<std::uintptr_t>(target);
    }
    return std::nullopt;
}

// Where a hook planted at `function` leads: the code the chain of jumps from
// the function's start ends in, when that is out of the game's module
// [moduleBase, moduleEnd). Jumps that stay in the module are followed on too:
// a game thunk can lead to a function another mod hooked, and MinHook patches a
// function too short for its jump with `jmp -7` to a jump written in the
// padding above it. Nothing when the function does not start with a jump, or
// when the chain ends in the game's own code. A chain is followed for eight
// jumps at most and judged by where it was left.
template <class Read>
std::optional<std::uintptr_t> PlantedHook(const Read& read, std::uintptr_t function, std::uintptr_t moduleBase,
                                          std::uintptr_t moduleEnd) {
    const auto inModule = [&](std::uintptr_t a) { return a >= moduleBase && a < moduleEnd; };
    std::optional<std::uintptr_t> next = JumpAt(read, function);
    if (!next) return std::nullopt;
    std::uintptr_t at = *next;
    for (int hop = 0; hop < 8; ++hop) {
        const std::optional<std::uintptr_t> on = JumpAt(read, at);
        if (!on) return inModule(at) ? std::nullopt : std::optional<std::uintptr_t>(at);
        at = *on;
    }
    return inModule(at) ? std::nullopt : std::optional<std::uintptr_t>(at);
}

}  // namespace cameraunlock::hooks::planted
