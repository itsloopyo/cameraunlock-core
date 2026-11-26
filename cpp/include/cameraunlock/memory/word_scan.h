// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstdint>
#include <vector>

#include <cameraunlock/memory/memory_view.h>

namespace cameraunlock::memory {

// Every 8-byte-aligned qword in `ranges`, read a page run at a time so a scan of
// a module's writable data costs a handful of reads rather than millions.
// Unreadable stretches are skipped.
template <class Fn>
void ForEachQword(const Memory& mem, const std::vector<Range>& ranges, Fn&& fn) {
    constexpr std::size_t kChunk = 0x10000;
    std::vector<std::uint64_t> buffer(kChunk / 8);
    for (const Range& range : ranges) {
        std::uintptr_t at = (range.begin + 7) & ~std::uintptr_t{7};
        while (at + 8 <= range.end) {
            const std::size_t want = (range.end - at) < kChunk ? (range.end - at) & ~std::size_t{7} : kChunk;
            if (mem.Read(at, buffer.data(), want)) {
                for (std::size_t i = 0; i < want / 8; ++i) fn(at + i * 8, buffer[i]);
                at += want;
                continue;
            }
            // Something in the chunk does not read: walk it a page at a time and
            // skip only the pages that fail.
            const std::uintptr_t stop = at + want;
            while (at < stop) {
                const std::uintptr_t pageEnd = (at + 0x1000) & ~std::uintptr_t{0xFFF};
                const std::size_t n = ((pageEnd < stop ? pageEnd : stop) - at) & ~std::size_t{7};
                if (n && mem.Read(at, buffer.data(), n))
                    for (std::size_t i = 0; i < n / 8; ++i) fn(at + i * 8, buffer[i]);
                at = pageEnd < stop ? pageEnd : stop;
            }
        }
    }
}

// Every pointer-aligned pointer-sized word in `ranges`: ForEachQword in a
// 64-bit build, each aligned dword in a 32-bit one.
template <class Fn>
void ForEachPointer(const Memory& mem, const std::vector<Range>& ranges, Fn&& fn) {
    if constexpr (sizeof(std::uintptr_t) == 8) {
        ForEachQword(mem, ranges, [&](std::uintptr_t at, std::uint64_t v) { fn(at, static_cast<std::uintptr_t>(v)); });
    } else {
        constexpr std::size_t kChunk = 0x10000;
        std::vector<std::uint32_t> buffer(kChunk / 4);
        for (const Range& range : ranges) {
            std::uintptr_t at = (range.begin + 3) & ~std::uintptr_t{3};
            while (at + 4 <= range.end) {
                const std::uintptr_t pageEnd = (at + 0x1000) & ~std::uintptr_t{0xFFF};
                const std::uintptr_t stop = range.end - at < kChunk ? range.end : at + kChunk;
                std::size_t n = (stop - at) & ~std::size_t{3};
                if (!mem.Read(at, buffer.data(), n)) {
                    // Walk the failing stretch a page at a time and skip only the pages that do not read.
                    n = ((pageEnd < stop ? pageEnd : stop) - at) & ~std::size_t{3};
                    if (!n || !mem.Read(at, buffer.data(), n)) {
                        at = pageEnd < stop ? pageEnd : stop;
                        continue;
                    }
                }
                for (std::size_t i = 0; i < n / 4; ++i) fn(at + i * 4, static_cast<std::uintptr_t>(buffer[i]));
                at += n;
            }
        }
    }
}

}  // namespace cameraunlock::memory
