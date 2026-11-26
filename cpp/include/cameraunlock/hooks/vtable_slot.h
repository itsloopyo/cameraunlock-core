// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Copyright (c) 2026 itsloopyo

#pragma once

#ifndef _WIN32
#error "cameraunlock/hooks/vtable_slot.h is Windows-only: it writes the running process's vtables."
#endif

#include <cstddef>
#include <string>
#include <vector>

namespace cameraunlock::hooks {

// Points the pointer-sized slot at `slot` (a vtable entry) at `value` in one atomic exchange and answers what it held.
// The slot's page is opened for the write and given its protection back after. A page that was executable stays so
// while it is written, as a vtable can share a page with code another thread is running. Throws naming `what` when
// the slot is not pointer-aligned or its protection could not be changed or put back.
void* SwapSlot(void** slot, void* value, const std::string& what);

// Slots swapped through it, each with what it held, so they can all be put back.
class SlotSwaps {
public:
    // SwapSlot, remembered.
    void* Swap(void** slot, void* value, const std::string& what);

    // Puts every slot back as it was, the latest swap first, and forgets them all. Every slot is tried; throws after
    // that, naming each one that could not be written.
    void RestoreAll();

    std::size_t Count() const { return entries_.size(); }

private:
    struct Entry {
        void** slot;
        void* held;
        std::string what;
    };
    std::vector<Entry> entries_;
};

}  // namespace cameraunlock::hooks
