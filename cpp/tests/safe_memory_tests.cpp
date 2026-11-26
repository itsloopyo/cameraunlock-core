// Tests for the SEH-guarded engine-memory access.
//
// The point of these is not that a good address reads back - it is that a bad
// one comes back false instead of taking the player's session with it, and that
// the guard is not a blanket swallow. Only EXCEPTION_ACCESS_VIOLATION is
// handled; a breakpoint, a stack overflow or a C++ exception travelling through
// has to keep unwinding to whoever owns it, and AccessViolationFilter is what
// says so.

#include <iostream>

#ifdef _WIN32

#include <cameraunlock/memory/safe_memory.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

struct Pose {
    float x;
    float y;
    float z;
    std::uint32_t tag;
};

void TestFilter() {
    using cameraunlock::memory::AccessViolationFilter;
    std::cout << "AccessViolationFilter:\n";

    Check(AccessViolationFilter(EXCEPTION_ACCESS_VIOLATION) == EXCEPTION_EXECUTE_HANDLER,
          "an access violation is handled");
    Check(AccessViolationFilter(EXCEPTION_BREAKPOINT) == EXCEPTION_CONTINUE_SEARCH,
          "a breakpoint keeps unwinding - this is not a blanket swallow");
    Check(AccessViolationFilter(EXCEPTION_STACK_OVERFLOW) == EXCEPTION_CONTINUE_SEARCH,
          "a stack overflow keeps unwinding");
    Check(AccessViolationFilter(EXCEPTION_INT_DIVIDE_BY_ZERO) == EXCEPTION_CONTINUE_SEARCH,
          "a divide by zero keeps unwinding");
}

void TestReadWrite() {
    using cameraunlock::memory::SafeRead;
    using cameraunlock::memory::SafeReadU8;
    using cameraunlock::memory::SafeWrite;
    std::cout << "SafeRead / SafeWrite:\n";

    Pose live{1.5f, -2.5f, 0.25f, 0xABCDEF01u};
    const auto liveAddr = reinterpret_cast<std::uintptr_t>(&live);

    Pose out{};
    Check(SafeRead(liveAddr, out) && out.x == 1.5f && out.y == -2.5f && out.z == 0.25f &&
              out.tag == 0xABCDEF01u,
          "a live struct reads back whole");

    float single = 0.0f;
    Check(SafeRead(liveAddr + offsetof(Pose, z), single) && single == 0.25f,
          "a field at an offset reads back");

    Check(SafeWrite(liveAddr + offsetof(Pose, y), 9.0f) && live.y == 9.0f,
          "a live field writes back");

    // The one the 8-bit accessor exists for: a byte-at-a-time walk, where a
    // wider read would touch memory past the object.
    std::uint8_t byte = 0;
    Check(SafeReadU8(liveAddr + 3, byte) &&
              byte == reinterpret_cast<const std::uint8_t*>(&live)[3],
          "SafeReadU8 reads exactly one byte");
}

void TestFaultsAreAbsorbed() {
    using cameraunlock::memory::SafeRead;
    using cameraunlock::memory::SafeReadU8;
    using cameraunlock::memory::SafeWrite;
    std::cout << "faults:\n";

    // The first page is never mapped in a Win32 process, so this is a
    // guaranteed access violation rather than a hopeful one.
    const std::uintptr_t dead = 0x10;

    Pose out{7.0f, 7.0f, 7.0f, 7u};
    Check(!SafeRead(dead, out), "reading unmapped memory returns false instead of faulting");
    Check(out.x == 7.0f && out.tag == 7u, "a failed read leaves the destination untouched");

    std::uint8_t byte = 0x5A;
    Check(!SafeReadU8(dead, byte), "SafeReadU8 absorbs the same fault");
    Check(byte == 0x5A, "a failed byte read leaves the destination untouched");

    Check(!SafeWrite(dead, 1.0f), "writing unmapped memory returns false instead of faulting");

    // Deliberately no "write into read-only code" case: on the one build where
    // it did NOT fault it would patch a breakpoint into a live function, so the
    // test would corrupt the process it is meant to be checking.
}

// The struct that faults PART WAY THROUGH. A 64-byte read starting 32 bytes
// before a decommitted page copies 32 bytes and then raises, so a SafeRead that
// assigned straight into the caller's object returned false having already
// overwritten half of it - and false is exactly when the caller keeps what it
// had.
void TestAPartialFaultLeavesTheDestinationAlone() {
    using cameraunlock::memory::SafeRead;
    std::cout << "page-straddling fault:\n";

    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const SIZE_T pageSize = info.dwPageSize;

    auto* base = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, pageSize * 2, MEM_RESERVE, PAGE_NOACCESS));
    Check(base != nullptr, "two pages reserved");
    if (base == nullptr) return;

    // Only the first page is committed, so a read that runs off its end faults
    // at a known offset rather than a hopeful one.
    VirtualAlloc(base, pageSize, MEM_COMMIT, PAGE_READWRITE);
    memset(base, 0xAA, pageSize);

    struct Wide { unsigned char bytes[64]; };
    const auto straddle = reinterpret_cast<std::uintptr_t>(base + pageSize - 32);

    Wide out;
    memset(&out, 0x11, sizeof(out));
    const bool ok = SafeRead(straddle, out);

    int modified = 0;
    for (unsigned char b : out.bytes) {
        if (b != 0x11) ++modified;
    }
    Check(!ok, "a read that runs into an unmapped page returns false");
    Check(modified == 0, "and leaves every byte of the destination untouched");

    VirtualFree(base, 0, MEM_RELEASE);
}

void TestFaultCounter() {
    using cameraunlock::memory::SafeRead;
    using cameraunlock::memory::SafeWrite;
    std::cout << "fault counter:\n";

    std::atomic<std::uint64_t> faults{0};
    float value = 0.0f;
    float live = 3.0f;

    Check(SafeRead(reinterpret_cast<std::uintptr_t>(&live), value, faults) &&
              faults.load() == 0,
          "a successful read does not touch the counter");

    Check(!SafeRead(std::uintptr_t{0x10}, value, faults) && faults.load() == 1,
          "a failed read increments the caller's own counter");

    Check(!SafeWrite(std::uintptr_t{0x10}, 1.0f, faults) && faults.load() == 2,
          "a failed write increments the same counter");
}

EXCEPTION_POINTERS* Fault(EXCEPTION_RECORD& record, EXCEPTION_POINTERS& pointers, DWORD code) {
    record = EXCEPTION_RECORD{};
    record.ExceptionCode = code;
    pointers = EXCEPTION_POINTERS{&record, nullptr};
    return &pointers;
}

void TestMemoryFaultFilter() {
    using cameraunlock::memory::MemoryFaultFilter;
    std::cout << "MemoryFaultFilter:\n";
    EXCEPTION_RECORD record;
    EXCEPTION_POINTERS pointers;
    Check(MemoryFaultFilter(Fault(record, pointers, EXCEPTION_ACCESS_VIOLATION)) == EXCEPTION_EXECUTE_HANDLER,
          "an access violation is handled");
    Check(MemoryFaultFilter(Fault(record, pointers, EXCEPTION_BREAKPOINT)) == EXCEPTION_CONTINUE_SEARCH,
          "a breakpoint keeps unwinding");
    Check(MemoryFaultFilter(Fault(record, pointers, EXCEPTION_STACK_OVERFLOW)) == EXCEPTION_CONTINUE_SEARCH,
          "a stack overflow keeps unwinding");
    Check(MemoryFaultFilter(Fault(record, pointers, EXCEPTION_INT_DIVIDE_BY_ZERO)) == EXCEPTION_CONTINUE_SEARCH,
          "a divide by zero keeps unwinding");
}

bool HasGuard(const void* at) {
    MEMORY_BASIC_INFORMATION mbi{};
    return VirtualQuery(at, &mbi, sizeof mbi) && (mbi.Protect & PAGE_GUARD);
}

// What another thread's stack looks like to a pointer walk: a committed page
// with the guard on. Touching it clears the guard and raises
// STATUS_GUARD_PAGE_VIOLATION, which AccessViolationFilter let through.
void TestAGuardPageIsReadAsAFaultAndKeepsItsGuard() {
    using cameraunlock::memory::RangeReadable;
    using cameraunlock::memory::SafeCopy;
    using cameraunlock::memory::SafeRead;
    using cameraunlock::memory::SafeWrite;
    std::cout << "guard page:\n";

    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    auto* page = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, info.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE | PAGE_GUARD));
    Check(page != nullptr, "a guard page allocated");
    if (page == nullptr) return;
    const auto at = reinterpret_cast<std::uintptr_t>(page);

    std::uint32_t value = 0x77;
    Check(!SafeRead(at, value) && value == 0x77, "SafeRead of a guard page returns false and leaves the value");
    Check(HasGuard(page), "and the guard is back on the page");

    unsigned char bytes[16] = {};
    Check(!SafeCopy(bytes, at, sizeof bytes), "SafeCopy of a guard page returns false");
    Check(HasGuard(page), "and the guard is back on the page");

    Check(!SafeWrite(at, std::uint32_t{1}), "SafeWrite to a guard page returns false");
    Check(HasGuard(page), "and the guard is back on the page");

    Check(!RangeReadable(at, 4), "RangeReadable calls a guard page unreadable");
    Check(HasGuard(page), "without touching the guard");

    VirtualFree(page, 0, MEM_RELEASE);
}

void TestCopyAndWriteBytes() {
    using cameraunlock::memory::SafeCopy;
    using cameraunlock::memory::SafeWriteBytes;
    std::cout << "SafeCopy / SafeWriteBytes:\n";

    unsigned char live[600];
    for (std::size_t i = 0; i < sizeof live; ++i) live[i] = static_cast<unsigned char>(i * 7);
    const auto liveAddr = reinterpret_cast<std::uintptr_t>(live);

    unsigned char small[32] = {};
    Check(SafeCopy(small, liveAddr + 10, sizeof small) && std::memcmp(small, live + 10, sizeof small) == 0,
          "a small live range copies back");
    unsigned char large[600] = {};
    Check(SafeCopy(large, liveAddr, sizeof large) && std::memcmp(large, live, sizeof large) == 0,
          "a range wider than the stack staging copies back");

    unsigned char kept[8];
    std::memset(kept, 0x33, sizeof kept);
    Check(!SafeCopy(kept, std::uintptr_t{0x10}, sizeof kept), "a copy from unmapped memory returns false");
    bool untouched = true;
    for (unsigned char b : kept) untouched = untouched && b == 0x33;
    Check(untouched, "and leaves the destination untouched");

    const unsigned char patch[4] = {1, 2, 3, 4};
    Check(SafeWriteBytes(liveAddr + 100, patch, sizeof patch) && std::memcmp(live + 100, patch, sizeof patch) == 0,
          "SafeWriteBytes writes a live range");
    Check(!SafeWriteBytes(std::uintptr_t{0x10}, patch, sizeof patch), "and returns false on unmapped memory");
}

// Both stagings: on the stack up to 256 bytes, on the heap past it.
void TestACopyStraddlingAPageLeavesTheDestinationAlone() {
    using cameraunlock::memory::SafeCopy;
    std::cout << "page-straddling copy:\n";

    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const SIZE_T pageSize = info.dwPageSize;
    auto* base = static_cast<unsigned char*>(VirtualAlloc(nullptr, pageSize * 2, MEM_RESERVE, PAGE_NOACCESS));
    Check(base != nullptr, "two pages reserved");
    if (base == nullptr) return;
    VirtualAlloc(base, pageSize, MEM_COMMIT, PAGE_READWRITE);
    memset(base, 0xAA, pageSize);

    for (std::size_t n : {std::size_t{64}, std::size_t{1024}}) {
        std::vector<unsigned char> out(n, 0x11);
        const bool ok = SafeCopy(out.data(), reinterpret_cast<std::uintptr_t>(base + pageSize - 32), n);
        bool untouched = true;
        for (unsigned char b : out) untouched = untouched && b == 0x11;
        Check(!ok && untouched, n == 64 ? "a 64-byte copy that runs into an unmapped page leaves the destination alone"
                                        : "a 1024-byte copy that runs into an unmapped page leaves the destination alone");
    }
    VirtualFree(base, 0, MEM_RELEASE);
}

void TestRanges() {
    using cameraunlock::memory::RangeReadable;
    using cameraunlock::memory::RangeWritable;
    std::cout << "RangeReadable / RangeWritable:\n";

    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const SIZE_T pageSize = info.dwPageSize;
    auto* base = static_cast<unsigned char*>(VirtualAlloc(nullptr, pageSize * 3, MEM_RESERVE, PAGE_NOACCESS));
    Check(base != nullptr, "three pages reserved");
    if (base == nullptr) return;
    DWORD was = 0;
    VirtualAlloc(base, pageSize * 2, MEM_COMMIT, PAGE_READWRITE);
    VirtualProtect(base + pageSize, pageSize, PAGE_READONLY, &was);
    const auto at = reinterpret_cast<std::uintptr_t>(base);

    Check(RangeReadable(at, pageSize * 2), "two committed pages of different protection read as one range");
    Check(RangeWritable(at, pageSize), "a read-write page is writable");
    Check(!RangeWritable(at, pageSize + 1), "a range running onto a read-only page is not writable");
    Check(!RangeReadable(at + pageSize * 2 - 4, 8), "a range running onto a reserved page is not readable");
    Check(!RangeReadable(at + pageSize * 2, 4), "a reserved page is not readable");
    Check(!RangeReadable(0, 4), "a null address is not readable");
    Check(!RangeReadable(at, 0), "an empty range is not readable");
    Check(!RangeReadable(~std::uintptr_t{0} - 2, 8), "a range that wraps is not readable");

    VirtualFree(base, 0, MEM_RELEASE);
}

}  // namespace

#endif  // _WIN32

int RunSafeMemoryTests() {
    std::cout << "\n=== Safe Memory Tests ===\n";
#ifdef _WIN32
    TestFilter();
    TestReadWrite();
    TestFaultsAreAbsorbed();
    TestAPartialFaultLeavesTheDestinationAlone();
    TestFaultCounter();
    TestMemoryFaultFilter();
    TestAGuardPageIsReadAsAFaultAndKeepsItsGuard();
    TestCopyAndWriteBytes();
    TestACopyStraddlingAPageLeavesTheDestinationAlone();
    TestRanges();
    return g_failures;
#else
    std::cout << "  (skipped: SEH-guarded access is Windows-only)\n";
    return 0;
#endif
}
