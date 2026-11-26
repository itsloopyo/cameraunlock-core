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
    return g_failures;
#else
    std::cout << "  (skipped: SEH-guarded access is Windows-only)\n";
    return 0;
#endif
}
