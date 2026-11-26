// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Copyright (c) 2026 itsloopyo

#include <cameraunlock/hooks/vtable_slot.h>

#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>

namespace cameraunlock::hooks {
namespace {

std::runtime_error Failed(const std::string& what, const char* step, DWORD error) {
    return std::runtime_error("the vtable slot for " + what + " could not be " + step + ": error " + std::to_string(error));
}

std::string Hex(const void* at) {
    char text[2 + 2 * sizeof(void*) + 1];
    std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(at)));
    return text;
}

// SwapSlot, or with `expected` a compare-exchange from it, with what the slot held put in `held` as soon as it is
// written, so a caller still knows it when the protection then cannot be put back. `exchanged` says whether the slot
// now holds `value`.
void Exchange(void** slot, void* value, void* const* expected, const std::string& what, bool& exchanged, void*& held) {
    if (reinterpret_cast<std::uintptr_t>(slot) % sizeof(void*) != 0)
        throw std::invalid_argument("the vtable slot for " + what + " is not pointer-aligned, so it cannot be written in one store");
    // Two swaps on one page would each put back the protection the other opened.
    static std::mutex swapping;
    std::lock_guard<std::mutex> hold(swapping);
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(slot, &info, sizeof info)) throw Failed(what, "queried", GetLastError());
    const DWORD writable =
        info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), writable, &old)) throw Failed(what, "made writable", GetLastError());
    if (expected) {
        held = InterlockedCompareExchangePointer(slot, value, *expected);
        exchanged = held == *expected;
    } else {
        held = InterlockedExchangePointer(slot, value);
        exchanged = true;
    }
    DWORD unused = 0;
    if (!VirtualProtect(slot, sizeof(void*), old, &unused)) throw Failed(what, "given its protection back", GetLastError());
}

}  // namespace

void* SwapSlot(void** slot, void* value, const std::string& what) {
    bool exchanged = false;
    void* held = nullptr;
    Exchange(slot, value, nullptr, what, exchanged, held);
    return held;
}

void* SlotSwaps::Swap(void** slot, void* value, const std::string& what) {
    bool exchanged = false;
    void* held = nullptr;
    try {
        Exchange(slot, value, nullptr, what, exchanged, held);
    } catch (...) {
        if (exchanged) entries_.push_back({slot, held, value, what});
        throw;
    }
    entries_.push_back({slot, held, value, what});
    return held;
}

void SlotSwaps::RestoreAll() {
    std::string failed;
    for (auto e = entries_.rbegin(); e != entries_.rend(); ++e) {
        std::string why;
        try {
            bool exchanged = false;
            void* now = nullptr;
            Exchange(e->slot, e->held, &e->wrote, e->what, exchanged, now);
            if (!exchanged)
                why = "the vtable slot for " + e->what + " holds " + Hex(now) + ", not the " + Hex(e->wrote) +
                      " written there, so it was left alone";
        } catch (const std::exception& error) {
            why = error.what();
        }
        if (!why.empty()) failed += (failed.empty() ? "" : "; ") + why;
    }
    entries_.clear();
    if (!failed.empty()) throw std::runtime_error("not every vtable slot was put back: " + failed);
}

}  // namespace cameraunlock::hooks
