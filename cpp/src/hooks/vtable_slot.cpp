// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Copyright (c) 2026 itsloopyo

#include <cameraunlock/hooks/vtable_slot.h>

#include <Windows.h>

#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>

namespace cameraunlock::hooks {
namespace {

std::runtime_error Failed(const std::string& what, const char* step, DWORD error) {
    return std::runtime_error("the vtable slot for " + what + " could not be " + step + ": error " + std::to_string(error));
}

// SwapSlot, with what the slot held put in `held` as soon as it is exchanged, so a caller still knows it when the
// protection then cannot be put back.
void Exchange(void** slot, void* value, const std::string& what, bool& exchanged, void*& held) {
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
    held = InterlockedExchangePointer(slot, value);
    exchanged = true;
    DWORD unused = 0;
    if (!VirtualProtect(slot, sizeof(void*), old, &unused)) throw Failed(what, "given its protection back", GetLastError());
}

}  // namespace

void* SwapSlot(void** slot, void* value, const std::string& what) {
    bool exchanged = false;
    void* held = nullptr;
    Exchange(slot, value, what, exchanged, held);
    return held;
}

void* SlotSwaps::Swap(void** slot, void* value, const std::string& what) {
    bool exchanged = false;
    void* held = nullptr;
    try {
        Exchange(slot, value, what, exchanged, held);
    } catch (...) {
        if (exchanged) entries_.push_back({slot, held, what});
        throw;
    }
    entries_.push_back({slot, held, what});
    return held;
}

void SlotSwaps::RestoreAll() {
    std::string failed;
    for (auto e = entries_.rbegin(); e != entries_.rend(); ++e) {
        try {
            SwapSlot(e->slot, e->held, e->what);
        } catch (const std::exception& error) {
            failed += failed.empty() ? "" : "; ";
            failed += error.what();
        }
    }
    entries_.clear();
    if (!failed.empty()) throw std::runtime_error("not every vtable slot was put back: " + failed);
}

}  // namespace cameraunlock::hooks
