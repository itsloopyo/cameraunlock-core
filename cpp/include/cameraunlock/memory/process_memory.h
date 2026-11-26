// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#ifndef _WIN32
#error "cameraunlock/memory/process_memory.h is Windows-only: it reads this process through VirtualQuery and SEH."
#endif

#include <cstdint>
#include <mutex>
#include <vector>

#include <cameraunlock/memory/memory_view.h>

namespace cameraunlock::memory {

// Reads of this process's memory, guarded so a stale pointer reads as a failure
// instead of an access violation. A range is read only when every page of it is
// committed, readable and not a guard page; each thread remembers the readable
// regions it was told of for 100 ms, so a scan that follows pointers all over
// the heap does not ask VirtualQuery about every read.
class ProcessMemory final : public Memory {
public:
    bool Read(std::uintptr_t addr, void* out, std::size_t size) const override;
};

// Whether a module's page protections are the loader's, so they can say which
// of its sections are written to, or a copy's (a scan tool's image read from a
// file into memory that is read-write throughout), which say nothing.
enum class Pages { Loaded, Copied };

// A module laid out in this process, from its own PE headers (of this build's
// architecture): section ranges and, in an x64 build, the unwind table.
// Throws when the headers are not a PE of this architecture, or the module has
// no executable section, no writable section or (x64) no unwind table.
class LoadedModule final : public CodeMap {
public:
    explicit LoadedModule(std::uintptr_t base, Pages pages = Pages::Loaded);

    const ModuleMap& Map() const { return map_; }

    // In an x86 build, which reads no unwind table, these find nothing.
    Range PartAt(std::uintptr_t at) const override;
    std::uintptr_t RootOf(std::uintptr_t at) const override;
    std::vector<Range> Parts(std::uintptr_t root) const override;

private:
    struct Entry {
        std::uint32_t begin, end, unwind;
    };
    const Entry* EntryAt(std::uintptr_t at) const;
    std::uintptr_t RootOfEntry(const Entry* e) const;

    ModuleMap map_;
    std::vector<Entry> entries_;
    mutable std::once_flag rootsOnce_;
    mutable std::vector<std::uintptr_t> roots_;
};

}  // namespace cameraunlock::memory
