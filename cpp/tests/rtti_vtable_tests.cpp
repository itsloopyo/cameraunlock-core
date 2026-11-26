// Tests for FindVtableFromRTTI (cameraunlock/memory/rtti_vtable.h) against classes
// compiled into this executable.

#include <cameraunlock/memory/rtti_vtable.h>

#include <Windows.h>

#include <iostream>

// At global scope: the lookup builds ".?AV<name>@@", which an anonymous namespace
// would change. Two classes defined together so their vtables sit side by side.
class RttiProbeThreeSlots {
public:
    virtual ~RttiProbeThreeSlots() = default;
    virtual int First() { return 1; }
    virtual int Second(int x) { return x * 3; }
};

class RttiProbeFourSlots {
public:
    virtual ~RttiProbeFourSlots() = default;
    virtual int First() { return 7; }
    virtual int Second(int x) { return x * 5; }
    virtual int Third() { return 9; }
};

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << name << "\n";
    if (!cond) ++g_failures;
}

RttiProbeThreeSlots* volatile g_three = nullptr;
RttiProbeFourSlots* volatile g_four = nullptr;

}  // namespace

int RunRttiVtableTests() {
    std::cout << "RTTI vtable:\n";
    g_three = new RttiProbeThreeSlots();
    g_four = new RttiProbeFourSlots();
    const uintptr_t threeVtable = *reinterpret_cast<uintptr_t*>(g_three);
    const uintptr_t fourVtable = *reinterpret_cast<uintptr_t*>(g_four);

    // The count check below proves the stop only where the word after the third
    // slot is an address inside this module (the next vtable's locator pointer),
    // which a read bounded by the module alone would take as a fourth slot.
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const uintptr_t end = base + nt->OptionalHeader.SizeOfImage;
    const uintptr_t afterThird = reinterpret_cast<const uintptr_t*>(threeVtable)[3];
    Check(afterThird >= base && afterThird < end,
          "the word after the three slots lies inside the module, so a bounds-only read runs on");

    cameraunlock::memory::VtableInfo info{};
    const bool found = cameraunlock::memory::FindVtableFromRTTI(
        GetModuleHandleW(nullptr), "RttiProbeThreeSlots", info, 8);
    Check(found, "a class in this module is found by its name");
    Check(info.vtable_address == threeVtable, "and the vtable found is the one its objects use");
    Check(info.vfunc_count == 3,
          "the read stops at the class's own three slots, not in the vtable laid out after it");

    cameraunlock::memory::VtableInfo four{};
    Check(cameraunlock::memory::FindVtableFromRTTI(
              GetModuleHandleW(nullptr), "RttiProbeFourSlots", four, 8)
              && four.vtable_address == fourVtable && four.vfunc_count == 4,
          "the neighbouring class reads its own four slots");

    delete g_three;
    delete g_four;
    return g_failures;
}
