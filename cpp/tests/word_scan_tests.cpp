// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Tests for the word scans over a module's ranges (cameraunlock/memory/word_scan.h).

#include <cameraunlock/memory/word_scan.h>

#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

using namespace cameraunlock::memory;

int g_failures = 0;

void Check(bool cond, const char* name) {
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << name << "\n";
    if (!cond) ++g_failures;
}

constexpr std::uint64_t kMark = 0x1234;

// A reader that refuses one page in the middle of the range, with a mark
// either side of it.
class Holed final : public Memory {
public:
    bool Read(std::uintptr_t addr, void* out, std::size_t size) const override {
        for (std::uintptr_t a = addr; a < addr + size; ++a)
            if (a >= 0x21000 && a < 0x22000) return false;
        std::memset(out, 0, size);
        if (addr <= 0x20008 && addr + size >= 0x20010) std::memcpy(static_cast<char*>(out) + (0x20008 - addr), &kMark, 8);
        if (addr <= 0x22008 && addr + size >= 0x22010) std::memcpy(static_cast<char*>(out) + (0x22008 - addr), &kMark, 8);
        return true;
    }
};

void QwordScanSkipsUnreadablePage() {
    const Holed mem;
    int marks = 0;
    std::uintptr_t lastAt = 0;
    ForEachQword(mem, {Range{0x20000, 0x23000}}, [&](std::uintptr_t at, std::uint64_t v) {
        if (v == kMark) ++marks;
        lastAt = at;
    });
    Check(marks == 2, "ForEachQword finds the marks either side of an unreadable page");
    Check(lastAt == 0x22FF8, "ForEachQword reads on to the range's last qword");
}

void PointerScanSkipsUnreadablePage() {
    const Holed mem;
    int marks = 0;
    std::uintptr_t lastAt = 0;
    ForEachPointer(mem, {Range{0x20000, 0x23000}}, [&](std::uintptr_t at, std::uintptr_t v) {
        if (v == kMark) ++marks;
        lastAt = at;
    });
    Check(marks == 2, "ForEachPointer finds the marks either side of an unreadable page");
    Check(lastAt == 0x23000 - sizeof(std::uintptr_t), "ForEachPointer reads on to the range's last word");
}

}  // namespace

int RunWordScanTests() {
    g_failures = 0;
    std::cout << "Word scan:\n";
    QwordScanSkipsUnreadablePage();
    PointerScanSkipsUnreadablePage();
    return g_failures;
}
