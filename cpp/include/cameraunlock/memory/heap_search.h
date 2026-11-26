// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#ifndef _WIN32
#error "cameraunlock/memory/heap_search.h is Windows-only: it walks this process with VirtualQuery and QueryWorkingSetEx."
#endif

#include <cstddef>
#include <cstdint>
#include <functional>

// Looking in this process's own memory for the objects no global leads to, by
// a value they hold (most often their class's vtable).
namespace cameraunlock::memory {

struct HeapSearch {
    std::size_t matches = 0;
    std::size_t pagesRead = 0;
    // Runs of pages whose residency could not be asked, so were not looked in,
    // and the last error QueryWorkingSetEx gave for one.
    std::size_t unread = 0;
    unsigned long lastError = 0;
};

// Calls `seen` with every pointer-aligned address in committed, private,
// PAGE_READWRITE memory that holds `value` (a pointer-sized word). Only pages
// already resident are read, so the search brings none of the game's memory
// into RAM; the calling thread's stack is skipped. A page freed or guarded
// under the search ends that page's search, and a guard set that late is
// used up by the read, as the first touch of any guard page is. It reads the whole heap: call it from a
// thread of the mod's own, never from the game's.
HeapSearch FindInPrivateMemory(std::uintptr_t value, const std::function<void(std::uintptr_t)>& seen);

}  // namespace cameraunlock::memory
