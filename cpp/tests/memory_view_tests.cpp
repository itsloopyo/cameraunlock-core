// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Tests for the Memory interface (cameraunlock/memory/memory_view.h) over a
// heap a test builds, and for ProcessMemory and LoadedModule
// (cameraunlock/memory/process_memory.h) over this process.

#include <cameraunlock/memory/memory_view.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <cameraunlock/memory/process_memory.h>

#include <Windows.h>
#endif

namespace {

using namespace cameraunlock::memory;

int g_failures = 0;

void Check(bool cond, const char* name) {
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << name << "\n";
    if (!cond) ++g_failures;
}

// Blocks of bytes at made-up addresses; anything outside them does not read.
class HeapMemory final : public Memory {
public:
    std::vector<std::uint8_t>& Block(std::uintptr_t at, std::size_t size) {
        auto& b = blocks_[at];
        b.assign(size, 0);
        return b;
    }
    bool Read(std::uintptr_t addr, void* out, std::size_t size) const override {
        for (const auto& [at, bytes] : blocks_) {
            if (addr >= at && addr - at <= bytes.size() && size <= bytes.size() - (addr - at)) {
                std::memcpy(out, bytes.data() + (addr - at), size);
                return true;
            }
        }
        return false;
    }

private:
    std::map<std::uintptr_t, std::vector<std::uint8_t>> blocks_;
};

void MemoryReadsThroughOneFunction() {
    std::cout << "Memory over a test heap:\n";
    HeapMemory mem;
    auto& b = mem.Block(0x100000, 0x40);
    const std::uint64_t value = 0x1122334455667788ull;
    std::memcpy(b.data() + 8, &value, 8);
    std::uint64_t got = 0;
    Check(mem.Get(0x100008, got) && got == value, "Get reads a value");
    Check(mem.Or0<std::uint64_t>(0x100008) == value, "Or0 reads a value");
    Check(mem.Or0<std::uint64_t>(0x200000) == 0, "Or0 is zero where nothing reads");
    Check(mem.Or0<std::uint64_t>(0x10003C) == 0, "Or0 is zero for a value running off a block");
    Check(mem.Ptr(0x100008) == static_cast<std::uintptr_t>(value), "Ptr reads a pointer-sized value");

    // A string ending in the last bytes of a block: the 16-byte read fails and
    // it is finished byte by byte.
    auto& s = mem.Block(0x300000, 0x13);
    std::memcpy(s.data(), "abcdefghijklmnopqr", 0x13);
    Check(mem.Ascii(0x300000) == "abcdefghijklmnopqr", "Ascii finishes a string at the end of a block");
    Check(mem.Ascii(0x300000, 4).empty(), "Ascii is empty when the string runs past max");
    s[3] = 0x01;
    Check(mem.Ascii(0x300000).empty(), "Ascii is empty for a non-printable byte");
    Check(mem.Ascii(0x400000).empty(), "Ascii is empty where nothing reads");
    auto& t = mem.Block(0x500000, 3);
    std::memcpy(t.data(), "abc", 3);  // the block ends before a terminator
    Check(mem.Ascii(0x500000).empty(), "Ascii is empty for a string the block cuts off");

    Check(mem.BytesEqual(0x500000, "abc", 3), "BytesEqual compares without a terminator");
    Check(!mem.BytesEqual(0x500000, "abd", 3), "BytesEqual tells bytes apart");
    Check(!mem.BytesEqual(0x500000, std::string(65, 'a').c_str(), 65), "BytesEqual refuses more than 64 bytes");
}

void PointersAndRanges() {
    std::cout << "PlausiblePtr, Range and ModuleMap:\n";
    Check(PlausiblePtr(0x7FF612340000ull), "an image address is plausible");
    Check(!PlausiblePtr(0x8000) && !PlausiblePtr(0x7FFFFFFFFFFFull) && !PlausiblePtr(0x10004),
          "the null page, the top of user space and an unaligned value are not");
    const Range r{0x1000, 0x2000};
    Check(r.Contains(0x1000) && r.Contains(0x1FFF) && !r.Contains(0x2000) && !r.Contains(0xFFF), "Range holds [begin, end)");
    Check(r.Contains(0x1FF8, 8) && !r.Contains(0x1FF9, 8), "Range holds a run only when all of it fits");
    ModuleMap map;
    map.base = 0x1000;
    map.end = 0x9000;
    map.code = {{0x2000, 0x3000}};
    Check(map.InModule(0x1000) && !map.InModule(0x9000), "InModule is [base, end)");
    Check(map.InCode(0x2800) && !map.InCode(0x3000), "InCode is inside a code range");
}

void FindsBytesAcrossChunks() {
    std::cout << "FindBytes and FindCString:\n";
    HeapMemory mem;
    auto& b = mem.Block(0x1000000, 0x20000);
    // Straddling the 64 KiB chunk boundary.
    std::memcpy(b.data() + 0xFFFE, "\0\0needleX", 9);
    b[0xFF] = 'Z';
    std::memcpy(b.data() + 0x100, "\0needle\0", 8);
    std::memcpy(b.data() + 0x200, "\0longneedle\0", 12);
    const std::vector<Range> ranges{{0x1000000, 0x1020000}};
    const auto hits = FindBytes(mem, ranges, std::string_view("\0needle", 7));
    Check(hits.size() == 2 && hits[0] == 0x1000100 && hits[1] == 0x100FFFF,
          "a needle with a leading NUL is found, across a chunk boundary too");
    const auto two = FindBytes(mem, ranges, std::string_view("\0\0needle", 8));
    Check(two.size() == 1 && two[0] == 0x100FFFE, "two leading NULs are both checked");
    Check(FindBytes(mem, ranges, "").empty(), "an empty needle finds nothing");
    const auto strings = FindCString(mem, ranges, "needle");
    Check(strings.size() == 1 && strings[0] == 0x1000101, "FindCString skips a longer string ending in the text");
    // A range whose tail does not read: the readable part is still searched.
    std::memcpy(mem.Block(0x3000000, 0x2000).data() + 0x1800, "\0needle\0", 8);
    Check(FindCString(mem, {{0x3000000, 0x3010000}}, "needle").size() == 1, "the readable pages of a range are searched");
}

#ifdef _WIN32

__declspec(noinline) int ARootFunction(int x) {
    volatile int y = x;
    std::vector<int> v(static_cast<std::size_t>(y) + 1);  // not a leaf: it has an unwind entry
    return static_cast<int>(v.size());
}

void ProcessMemoryReadsThisProcess() {
    std::cout << "ProcessMemory:\n";
    const ProcessMemory mem;
    const std::uint32_t local = 0xCAFEF00D;
    std::uint32_t got = 0;
    Check(mem.Get(reinterpret_cast<std::uintptr_t>(&local), got) && got == local, "a local reads back");
    Check(!mem.Get(0x1000, got), "the null page does not read");
    Check(!mem.Read(~std::uintptr_t{0} - 2, &got, 4), "a range wrapping the address space does not read");

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const std::size_t page = si.dwPageSize;
    auto* none = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, page, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS));
    Check(!mem.Get(reinterpret_cast<std::uintptr_t>(none), got), "a no-access page does not read");
    VirtualFree(none, 0, MEM_RELEASE);

    auto* guard = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, page, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE | PAGE_GUARD));
    Check(!mem.Get(reinterpret_cast<std::uintptr_t>(guard), got), "a guard page does not read");
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery(guard, &mbi, sizeof mbi);
    Check((mbi.Protect & PAGE_GUARD) != 0, "the guard page keeps its guard");
    VirtualFree(guard, 0, MEM_RELEASE);

    // A region the thread has cached as readable, decommitted under it: the
    // copy faults and the read fails.
    auto* two = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 2 * page, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    two[page] = 7;
    std::uint8_t byte = 0;
    Check(mem.Get(reinterpret_cast<std::uintptr_t>(two + page), byte) && byte == 7, "a fresh page reads");
    Check(mem.Get(reinterpret_cast<std::uintptr_t>(two), byte), "its neighbour reads, caching the region");
    VirtualFree(two + page, page, MEM_DECOMMIT);
    Check(!mem.Get(reinterpret_cast<std::uintptr_t>(two + page), byte), "a page decommitted under the cache does not read");
    VirtualFree(two, 0, MEM_RELEASE);
}

void LoadedModuleReadsThisExe() {
    std::cout << "LoadedModule:\n";
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const LoadedModule module(base);
    const ModuleMap& map = module.Map();
    Check(map.base == base && map.end > base, "the map spans the image");
    Check(!map.code.empty() && !map.writable.empty() && !map.readonly.empty(), "code, writable and read-only ranges are found");
    const auto fn = reinterpret_cast<std::uintptr_t>(&ARootFunction);
    Check(map.InCode(fn), "a function of this exe is in its code");
#if defined(_WIN64)
    Check(module.RootOf(fn) == fn && module.RootOf(fn + 1) == fn, "RootOf finds the function's start from inside it");
    Check(module.PartAt(fn).begin == fn, "PartAt is the unwind entry starting there");
    const auto parts = module.Parts(fn);
    Check(!parts.empty() && parts.front().begin == fn, "Parts lists the function's entries");
    Check(module.RootOf(base) == 0, "the headers are in no function");
#else
    Check(module.RootOf(fn) == 0 && module.Parts(fn).empty(), "an x86 build reads no unwind table");
#endif
    std::vector<std::uint8_t> junk(0x1000, 0);
    bool threw = false;
    try {
        LoadedModule bad(reinterpret_cast<std::uintptr_t>(junk.data()));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    Check(threw, "a buffer that is no PE image throws");
    Check(LoadedModule(base, Pages::Copied).Map().writable.size() == map.writable.size(),
          "a module whose headers name its writable sections reads the same as a copy");
}

#endif

}  // namespace

int RunMemoryViewTests() {
    g_failures = 0;
    MemoryReadsThroughOneFunction();
    PointersAndRanges();
    FindsBytesAcrossChunks();
#ifdef _WIN32
    ProcessMemoryReadsThisProcess();
    LoadedModuleReadsThisExe();
#endif
    return g_failures;
}
