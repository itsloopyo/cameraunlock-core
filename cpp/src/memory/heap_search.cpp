// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/memory/heap_search.h>

#include <Windows.h>
#include <psapi.h>

#include <algorithm>
#include <array>

namespace cameraunlock::memory {
namespace {

constexpr std::uintptr_t kPage = 0x1000;

// The next aligned word in [a, end) equal to `value`, or `end`; a page
// decommitted or guarded under the search ends it there. No C++ objects in
// this frame: __try cannot share a function with unwinding.
std::uintptr_t NextMatch(std::uintptr_t a, std::uintptr_t end, std::uintptr_t value) {
    __try {
        for (; a < end; a += sizeof(std::uintptr_t))
            if (*reinterpret_cast<const volatile std::uintptr_t*>(a) == value) return a;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == STATUS_GUARD_PAGE_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                                                                                 : EXCEPTION_CONTINUE_SEARCH) {
    }
    return end;
}

}  // namespace

// A region with PAGE_GUARD does not read as PAGE_READWRITE, so no guard page
// is touched unless the game sets one between the query and the read.
HeapSearch FindInPrivateMemory(std::uintptr_t value, const std::function<void(std::uintptr_t)>& seen) {
    HeapSearch look;
    ULONG_PTR stackLow = 0, stackHigh = 0;
    GetCurrentThreadStackLimits(&stackLow, &stackHigh);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    std::array<PSAPI_WORKING_SET_EX_INFORMATION, 256> pages;
    MEMORY_BASIC_INFORMATION m;
    for (auto a = reinterpret_cast<std::uintptr_t>(si.lpMinimumApplicationAddress);
         a < reinterpret_cast<std::uintptr_t>(si.lpMaximumApplicationAddress) && VirtualQuery(reinterpret_cast<void*>(a), &m, sizeof(m));
         a = reinterpret_cast<std::uintptr_t>(m.BaseAddress) + m.RegionSize) {
        if (m.State != MEM_COMMIT || m.Type != MEM_PRIVATE || m.Protect != PAGE_READWRITE) continue;
        const auto begin = reinterpret_cast<std::uintptr_t>(m.BaseAddress);
        if (begin < stackHigh && begin + m.RegionSize > stackLow) continue;
        const std::size_t total = m.RegionSize / kPage;
        for (std::size_t done = 0; done < total; done += pages.size()) {
            const std::size_t n = (std::min)(pages.size(), total - done);
            for (std::size_t i = 0; i < n; ++i) pages[i].VirtualAddress = reinterpret_cast<void*>(begin + (done + i) * kPage);
            if (!QueryWorkingSetEx(GetCurrentProcess(), pages.data(), static_cast<DWORD>(n * sizeof(pages[0])))) {
                ++look.unread;
                look.lastError = GetLastError();
                continue;
            }
            for (std::size_t i = 0; i < n; ++i) {
                if (!pages[i].VirtualAttributes.Valid) continue;
                ++look.pagesRead;
                const std::uintptr_t page = begin + (done + i) * kPage;
                for (std::uintptr_t at = NextMatch(page, page + kPage, value); at < page + kPage;
                     at = NextMatch(at + sizeof(std::uintptr_t), page + kPage, value)) {
                    ++look.matches;
                    seen(at);
                }
            }
        }
    }
    return look;
}

}  // namespace cameraunlock::memory
