// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/memory/process_memory.h>
#include <cameraunlock/memory/safe_memory.h>

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace cameraunlock::memory {
namespace {

#if defined(_WIN64)
constexpr std::uint8_t kUnwindChained = 0x4;
#endif

// VirtualQuery walks the page tables of the whole region it reports, so in a
// game with gigabytes of heap each call costs tens of microseconds, and a scan
// that follows pointers all over the heap asks it about a new region almost
// every read: Bodycam's discovery took 65 s in process against 3 s through
// ReadProcessMemory from outside (Headstart's Unreal backend). Each thread
// keeps the readable regions it has been told of for kRegionsFor and then asks
// afresh, so a region freed or re-protected is seen within that time. An
// unreadable answer is not kept: a reserved range is committed a page at a
// time as objects are made in it.
constexpr auto kRegionsFor = std::chrono::milliseconds(100);

struct Region {
    std::uintptr_t begin, end;
};

// A guard page is never read on purpose: touching one clears the guard, which
// breaks the owning thread's stack growth.
bool Readable(std::uintptr_t from, std::uintptr_t to) {
    thread_local std::vector<Region> regions;  // sorted, none overlapping
    thread_local std::chrono::steady_clock::time_point since{};
    const auto now = std::chrono::steady_clock::now();
    if (now - since > kRegionsFor) {
        regions.clear();
        since = now;
    }
    for (std::uintptr_t at = from; at < to;) {
        const auto it = std::upper_bound(regions.begin(), regions.end(), at,
                                         [](std::uintptr_t a, const Region& r) { return a < r.begin; });
        if (it != regions.begin() && at < std::prev(it)->end) {
            at = std::prev(it)->end;
            continue;
        }
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(reinterpret_cast<const void*>(at), &mbi, sizeof mbi)) return false;
        constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                    PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(mbi.Protect & kReadable))
            return false;
        const Region fresh{reinterpret_cast<std::uintptr_t>(mbi.BaseAddress),
                           reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize};
        // A kept region that overlaps this answer is older than it. Ends are
        // sorted like begins, so the overlap is one run.
        const auto first = std::upper_bound(regions.begin(), regions.end(), fresh.begin,
                                            [](std::uintptr_t a, const Region& r) { return a < r.end; });
        const auto last = std::lower_bound(first, regions.end(), fresh.end,
                                           [](const Region& r, std::uintptr_t a) { return r.begin < a; });
        regions.insert(regions.erase(first, last), fresh);
        at = fresh.end;
    }
    return true;
}

// The parts of a range whose committed pages can be written go to `writable`,
// the rest to `readonly`, a run of pages alike at a time.
void SplitByPages(const Range& range, std::vector<Range>& writable, std::vector<Range>& readonly) {
    constexpr DWORD kWritable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    for (std::uintptr_t at = range.begin; at < range.end;) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(reinterpret_cast<const void*>(at), &mbi, sizeof mbi))
            throw std::runtime_error("VirtualQuery failed inside the module, error " + std::to_string(GetLastError()));
        const std::uintptr_t end = (std::min<std::uintptr_t>)(range.end, reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize);
        const bool canWrite = mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) && (mbi.Protect & kWritable);
        std::vector<Range>& to = canWrite ? writable : readonly;
        if (!to.empty() && to.back().end == at) to.back().end = end;
        else to.push_back({at, end});
        at = end;
    }
}

}  // namespace

bool ProcessMemory::Read(std::uintptr_t addr, void* out, std::size_t size) const {
    if (addr < 0x10000 || addr + size < addr) return false;
    if (!Readable(addr, addr + size)) return false;
    return GuardedMemcpy(out, reinterpret_cast<const void*>(addr), size);
}

LoadedModule::LoadedModule(std::uintptr_t base, Pages pages) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) throw std::runtime_error("module has no DOS header");
    // IMAGE_NT_HEADERS and its magic are the ones of the architecture this is built for.
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC)
        throw std::runtime_error(sizeof(void*) == 8 ? "module is not a 64-bit PE image" : "module is not a 32-bit PE image");

    map_.base = base;
    map_.end = base + nt->OptionalHeader.SizeOfImage;
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        const std::uint32_t size = (std::max)(section->Misc.VirtualSize, section->SizeOfRawData);
        if (!size) continue;
        const Range range{base + section->VirtualAddress, base + section->VirtualAddress + size};
        if (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) map_.code.push_back(range);
        else if (section->Characteristics & IMAGE_SCN_MEM_WRITE) map_.writable.push_back(range);
        else if (section->Characteristics & IMAGE_SCN_MEM_READ) map_.readonly.push_back(range);
    }
    if (map_.code.empty()) throw std::runtime_error("module has no executable section");
    if (map_.writable.empty() && pages == Pages::Loaded) {
        // A protected exe can clear the write flag on every section header once it
        // is loaded (Far Cry Primal's FCPrimal.exe: .sdata and .tls read 0xc0000040
        // on disk, 0x40000040 loaded). The loader's pages still say which parts
        // are written to. A copy's pages say nothing, so a copy keeps the error.
        std::vector<Range> readonly;
        for (const Range& r : map_.readonly) SplitByPages(r, map_.writable, readonly);
        map_.readonly = std::move(readonly);
    }
    if (map_.writable.empty())
        throw std::runtime_error(pages == Pages::Loaded
                                     ? "module has no writable section, by its headers or its pages"
                                     : "module has no writable section by its headers (a protected image's are only told apart in the running game)");

#if defined(_WIN64)
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!dir.VirtualAddress || dir.Size < sizeof(RUNTIME_FUNCTION))
        throw std::runtime_error("module has no unwind table");
    const auto* fn = reinterpret_cast<const RUNTIME_FUNCTION*>(base + dir.VirtualAddress);
    const std::size_t count = dir.Size / sizeof(RUNTIME_FUNCTION);
    entries_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) entries_.push_back({fn[i].BeginAddress, fn[i].EndAddress, fn[i].UnwindData});
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) { return a.begin < b.begin; });
#endif
}

const LoadedModule::Entry* LoadedModule::EntryAt(std::uintptr_t at) const {
    if (at < map_.base || at >= map_.end) return nullptr;
    const auto rva = static_cast<std::uint32_t>(at - map_.base);
    auto it = std::upper_bound(entries_.begin(), entries_.end(), rva,
                               [](std::uint32_t r, const Entry& e) { return r < e.begin; });
    if (it == entries_.begin()) return nullptr;
    --it;
    return rva < it->end ? &*it : nullptr;
}

Range LoadedModule::PartAt(std::uintptr_t at) const {
    const Entry* e = EntryAt(at);
    return e ? Range{map_.base + e->begin, map_.base + e->end} : Range{};
}

std::uintptr_t LoadedModule::RootOfEntry(const Entry* e) const {
#if !defined(_WIN64)
    (void)e;
    return 0;
#else
    // A chained entry's UNWIND_INFO ends with the RUNTIME_FUNCTION of its parent.
    for (int depth = 0; e && depth < 32; ++depth) {
        const auto* info = reinterpret_cast<const std::uint8_t*>(map_.base + (e->unwind & ~1u));
        if (!((info[0] >> 3) & kUnwindChained)) return map_.base + e->begin;
        const std::uint8_t codes = info[2];
        const auto* parent = reinterpret_cast<const RUNTIME_FUNCTION*>(info + 4 + ((codes + 1u) & ~1u) * 2);
        e = EntryAt(map_.base + parent->BeginAddress);
    }
    return 0;
#endif
}

std::uintptr_t LoadedModule::RootOf(std::uintptr_t at) const { return RootOfEntry(EntryAt(at)); }

std::vector<Range> LoadedModule::Parts(std::uintptr_t root) const {
    std::call_once(rootsOnce_, [this] {
        roots_.reserve(entries_.size());
        for (const Entry& e : entries_) roots_.push_back(RootOfEntry(&e));
    });
    std::vector<Range> out;
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (roots_[i] == root) out.push_back({map_.base + entries_[i].begin, map_.base + entries_[i].end});
    return out;
}

}  // namespace cameraunlock::memory
