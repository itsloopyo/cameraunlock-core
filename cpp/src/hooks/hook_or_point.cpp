// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Copyright (c) 2026 itsloopyo

#include <cameraunlock/hooks/hook_or_point.h>

#include <cameraunlock/hooks/hook_manager.h>

#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace cameraunlock::hooks {
namespace {

std::string Hex(const void* at) {
    char text[2 + 2 * sizeof(void*) + 1];
    std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(at)));
    return text;
}

}  // namespace

HookedBy HookOrPointVtables(void* function, const std::vector<void**>& entries, void* detour, void** original, SlotSwaps& pointed,
                            const std::string& what) {
    auto& hm = HookManager::Instance();
    const HookStatus created = hm.CreateHook(function, detour, original);
    if (created == HookStatus::Ok) {
        if (const HookStatus s = hm.EnableHook(function); s != HookStatus::Ok)
            throw std::runtime_error("cannot enable the hook on " + what + " at " + Hex(function) + ": " + HookStatusToString(s));
        return HookedBy::MinHook;
    }
    if (created != HookStatus::ErrorMemoryAlloc)
        throw std::runtime_error("cannot hook " + what + " at " + Hex(function) + ": " + HookStatusToString(created));
    if (entries.empty()) throw std::runtime_error("cannot hook " + what + " at " + Hex(function) + ": no memory near it and no vtable holds it");
    for (void** const entry : entries)
        if (*entry != function)
            throw std::runtime_error("cannot point the vtable entry at " + Hex(entry) + " for " + what + " at its detour: it holds " + Hex(*entry) +
                                     ", not " + Hex(function));
    *original = function;
    for (void** const entry : entries) pointed.Swap(entry, detour, what + " (vtable entry at " + Hex(entry) + ")");
    return HookedBy::Vtables;
}

}  // namespace cameraunlock::hooks
