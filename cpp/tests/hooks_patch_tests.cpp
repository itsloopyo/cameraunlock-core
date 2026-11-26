// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Copyright (c) 2026 itsloopyo

// Tests for the code patching helpers in cameraunlock/hooks, in an executable of their own: the hook-or-point test
// fills the address space around a page, and the breakpoints arm every thread of the process. `--on-return` runs
// the breakpoint tests that use OnReturn, which takes debug register 1 from slot 3 for the rest of the process.

#include <cameraunlock/hooks/code_patch.h>
#include <cameraunlock/hooks/hook_manager.h>
#include <cameraunlock/hooks/hook_or_point.h>
#include <cameraunlock/hooks/hw_breakpoint.h>
#include <cameraunlock/hooks/planted_jump.h>
#include <cameraunlock/hooks/vtable_slot.h>

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace hooks = cameraunlock::hooks;
namespace code_patch = cameraunlock::hooks::code_patch;
namespace planted = cameraunlock::hooks::planted;
namespace breakpoint = cameraunlock::hooks::hw_breakpoint;

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* expr, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL line %d: %s\n", line, expr);
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

template <class F>
bool Throws(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

volatile int g_sink = 0;

// ---- a hook another mod planted at a game function

#ifdef _WIN64
// A sparse address space for the planted-jump reader: bytes put at addresses,
// everything else unreadable.
struct Sparse {
    std::vector<std::pair<std::uintptr_t, std::vector<std::uint8_t>>> spans;
    void Put(std::uintptr_t at, std::vector<std::uint8_t> bytes) { spans.emplace_back(at, std::move(bytes)); }
    bool operator()(std::uintptr_t at, void* out, std::size_t n) const {
        for (const auto& [base, bytes] : spans)
            if (at >= base && at + n <= base + bytes.size()) {
                std::memcpy(out, bytes.data() + (at - base), n);
                return true;
            }
        return false;
    }
};

std::vector<std::uint8_t> Le(std::uint64_t v, int n) {
    std::vector<std::uint8_t> out;
    for (int i = 0; i < n; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    return out;
}

std::vector<std::uint8_t> Cat(std::vector<std::uint8_t> a, const std::vector<std::uint8_t>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

std::vector<std::uint8_t> JmpRel32(std::uintptr_t from, std::uintptr_t to) {
    return Cat({0xE9}, Le(static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::intptr_t>(to - (from + 5)))), 4));
}

void PlantedHook() {
    std::printf("-- a hook another mod planted at a game function\n");
    constexpr std::uintptr_t kGame = 0x180000000ull, kGameEnd = 0x1A0000000ull;
    constexpr std::uintptr_t kFn = kGame + 0x12d8d10, kRelay = kGame - 0x10000, kDetour = 0x7FF812340000ull;
    // MinHook on x64: jmp rel32 to a relay just below the module, the relay jmp [rip+0] to the detour.
    Sparse mem;
    mem.Put(kFn, JmpRel32(kFn, kRelay));
    mem.Put(kRelay, Cat({0xFF, 0x25, 0, 0, 0, 0}, Le(kDetour, 8)));
    mem.Put(kDetour, {0x48, 0x89, 0x5C, 0x24, 0x08});
    CHECK(planted::PlantedHook(mem, kFn, kGame, kGameEnd) == std::optional<std::uintptr_t>(kDetour));

    // The game's own code: a prologue, and thunks that jump on inside the module.
    Sparse game;
    game.Put(kFn, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57});
    game.Put(kFn + 0x100, JmpRel32(kFn + 0x100, kFn + 0x2000));
    game.Put(kFn + 0x110, {0xEB, 0x10});
    CHECK(!planted::PlantedHook(game, kFn, kGame, kGameEnd));
    CHECK(!planted::PlantedHook(game, kFn + 0x100, kGame, kGameEnd));
    CHECK(!planted::PlantedHook(game, kFn + 0x110, kGame, kGameEnd));

    // mov rax, imm64; jmp rax straight to a DLL, and a jump to memory that does not read.
    Sparse abs;
    abs.Put(kFn, Cat(Cat({0x48, 0xB8}, Le(kDetour, 8)), {0xFF, 0xE0}));
    CHECK(planted::PlantedHook(abs, kFn, kGame, kGameEnd) == std::optional<std::uintptr_t>(kDetour));
    Sparse lost;
    lost.Put(kFn, JmpRel32(kFn, kRelay));
    CHECK(planted::PlantedHook(lost, kFn, kGame, kGameEnd) == std::optional<std::uintptr_t>(kRelay));
    CHECK(!planted::PlantedHook(lost, kFn + 0x40, kGame, kGameEnd));

    // A game thunk (a vtable slot's target) that jumps to a function another mod hooked.
    Sparse thunked = mem;
    thunked.Put(kFn - 0x1000, JmpRel32(kFn - 0x1000, kFn));
    CHECK(planted::PlantedHook(thunked, kFn - 0x1000, kGame, kGameEnd) == std::optional<std::uintptr_t>(kDetour));

    // MinHook on a function too short for its jump: jmp -7 into the padding above, where the jump out sits.
    Sparse above;
    above.Put(kFn - 5, Cat(JmpRel32(kFn - 5, kRelay), {0xEB, 0xF9}));
    above.Put(kRelay, Cat({0xFF, 0x25, 0, 0, 0, 0}, Le(kDetour, 8)));
    above.Put(kDetour, {0x48, 0x89, 0x5C, 0x24, 0x08});
    CHECK(planted::PlantedHook(above, kFn, kGame, kGameEnd) == std::optional<std::uintptr_t>(kDetour));
}
#else
// On x86 only the relative jumps are read: FF 25 is jmp [abs32] there, and 48 B8 is two other instructions.
void PlantedHook() {
    std::printf("-- a hook another mod planted at a game function (x86)\n");
    std::uint8_t code[64] = {};
    const auto base = reinterpret_cast<std::uintptr_t>(code);
    const auto read = [&](std::uintptr_t at, void* out, std::size_t n) {
        if (at < base || at + n > base + sizeof(code)) return false;
        std::memcpy(out, reinterpret_cast<const void*>(at), n);
        return true;
    };
    code[0] = 0xEB;
    code[1] = 0x10;
    CHECK(planted::JumpAt(read, base) == std::optional<std::uintptr_t>(base + 0x12));
    code[0] = 0xFF;
    code[1] = 0x25;
    CHECK(!planted::JumpAt(read, base));
    code[0] = 0xE9;
    const std::int32_t rel = 0x1000;
    std::memcpy(code + 1, &rel, 4);
    CHECK(planted::PlantedHook(read, base, base, base + 0x100) == std::optional<std::uintptr_t>(base + 5 + 0x1000));
}
#endif

// ---- a call redirected and code swapped in one store

__declspec(noinline) int __cdecl RedirectTarget() {
    g_sink = g_sink + 1;
    return 1;
}
__declspec(noinline) int __cdecl RedirectDetour() {
    g_sink = g_sink + 1;
    return 2;
}

void WriteCall(std::uint8_t* at, const void* to) {
    at[0] = 0xE8;
    const auto rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(to) - (reinterpret_cast<std::uintptr_t>(at) + 5));
    std::memcpy(at + 1, &rel, 4);
}

void MakeExecutable(std::uint8_t* page, std::size_t size) {
    DWORD old = 0;
    if (!VirtualProtect(page, size, PAGE_EXECUTE_READ, &old)) throw std::runtime_error("the test's code page could not be made executable");
    FlushInstructionCache(GetCurrentProcess(), page, size);
}

void Redirect() {
    std::printf("-- a call pointed somewhere else in one store\n");
    constexpr std::size_t kPage = 0x1000;
#ifdef _WIN64
    const auto exe = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::uint8_t* page = code_patch::AllocNear(exe, "the test's code");
    CHECK(reinterpret_cast<std::uintptr_t>(page) < exe && exe - reinterpret_cast<std::uintptr_t>(page) < 0x80000000ull);
    // sub rsp, 28h keeps the stack aligned for the call.
    std::uint8_t* entry = page + 0xFF;
    const std::uint8_t prologue[4] = {0x48, 0x83, 0xEC, 0x28};
    std::memcpy(entry, prologue, 4);
    std::uint8_t* site = page + 0x103;
    WriteCall(site, reinterpret_cast<const void*>(&RedirectTarget));
    const std::uint8_t epilogue[5] = {0x48, 0x83, 0xC4, 0x28, 0xC3};
    std::memcpy(site + 5, epilogue, 5);

    std::uint8_t jump[code_patch::kJumpBytes] = {};
    code_patch::JumpAbsolute(jump, reinterpret_cast<const void*>(&RedirectDetour));
    const void* to = nullptr;
    std::memcpy(&to, jump + 6, sizeof(to));
    CHECK(jump[0] == 0xFF && jump[1] == 0x25 && jump[2] == 0 && jump[5] == 0 && to == reinterpret_cast<const void*>(&RedirectDetour));
#else
    auto* page = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, kPage, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!page) throw std::runtime_error("no page for the test's code");
    std::uint8_t* site = page + 0x103;
    std::uint8_t* entry = site;
    WriteCall(site, reinterpret_cast<const void*>(&RedirectTarget));
    site[5] = 0xC3;
#endif
    // A call whose displacement crosses the block a store can write whole.
    std::uint8_t* split = page + 0x20C;
    WriteCall(split, reinterpret_cast<const void*>(&RedirectTarget));
    split[5] = 0xC3;
    // Not a call.
    page[0x300] = 0x90;
    MakeExecutable(page, kPage);

    const auto run = reinterpret_cast<int(__cdecl*)()>(entry);
    CHECK(run() == 1);
    const auto target = reinterpret_cast<std::uintptr_t>(&RedirectTarget);
    void* detour = reinterpret_cast<void*>(&RedirectDetour);
    CHECK(Throws([&] { code_patch::PrepareRedirect(reinterpret_cast<std::uintptr_t>(site), detour, target + 1, "a call elsewhere"); }));
    CHECK(Throws([&] { code_patch::PrepareRedirect(reinterpret_cast<std::uintptr_t>(page + 0x300), detour, target, "no call"); }));
    CHECK(Throws([&] { code_patch::PrepareRedirect(reinterpret_cast<std::uintptr_t>(split), detour, target, "a split call"); }));
    CHECK(run() == 1);

    const code_patch::Redirect r = code_patch::PrepareRedirect(reinterpret_cast<std::uintptr_t>(site), detour, target, "the test call");
    CHECK(run() == 1);
    // Another mod points the call at its own code between the two steps: the redirect is refused and theirs stays.
    std::int32_t game = 0;
    std::memcpy(&game, site + 1, 4);
    const auto theirs = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&RedirectDetour) - (reinterpret_cast<std::uintptr_t>(site) + 5));
    code_patch::SwapCode(site + 1, &game, &theirs, 4, "another mod's redirect");
    CHECK(run() == 2);
    CHECK(Throws([&] { code_patch::ApplyRedirect(r, "the test call"); }));
    CHECK(std::memcmp(site + 1, &theirs, 4) == 0);
    code_patch::SwapCode(site + 1, &theirs, &game, 4, "another mod's redirect taken out");
    CHECK(run() == 1);
    code_patch::ApplyRedirect(r, "the test call");
    CHECK(run() == 2);
    MEMORY_BASIC_INFORMATION info{};
    CHECK(VirtualQuery(site, &info, sizeof info) && info.Protect == PAGE_EXECUTE_READ);
    // Redirected once, the call no longer goes where discovery read.
    CHECK(Throws([&] { code_patch::PrepareRedirect(reinterpret_cast<std::uintptr_t>(site), detour, target, "the test call again"); }));
}

void SwapCode() {
    std::printf("-- code swapped in one store\n");
    constexpr std::size_t kPage = 0x1000;
    auto* page = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, kPage, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!page) throw std::runtime_error("no page for the test's code");
    // mov eax, 1; ret
    const std::uint8_t returnsOne[6] = {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3};
    std::memcpy(page + 0x300, returnsOne, sizeof returnsOne);
    MakeExecutable(page, kPage);
    const auto run = reinterpret_cast<int(__cdecl*)()>(page + 0x300);
    CHECK(run() == 1);

    const std::uint8_t one[4] = {1, 0, 0, 0}, seven[4] = {7, 0, 0, 0}, nine[4] = {9, 0, 0, 0};
    code_patch::SwapCode(page + 0x301, one, seven, 4, "the test's constant");
    CHECK(run() == 7);
    MEMORY_BASIC_INFORMATION info{};
    CHECK(VirtualQuery(page, &info, sizeof info) && info.Protect == PAGE_EXECUTE_READ);
    // It holds 7 now, not 1: nothing is written.
    CHECK(Throws([&] { code_patch::SwapCode(page + 0x301, one, nine, 4, "the test's constant"); }));
    CHECK(run() == 7);
    code_patch::SwapCode(page + 0x301, seven, one, 4, "the test's constant");
    CHECK(run() == 1);
    // Four bytes across the edge of the block a store writes whole, and nothing at all.
    CHECK(Throws([&] { code_patch::SwapCode(page + 0x30E, one, seven, 4, "a split constant"); }));
    CHECK(Throws([&] { code_patch::SwapCode(page + 0x300, one, seven, 0, "nothing"); }));
    CHECK(Throws([&] { code_patch::SwapCode(page + 0x301, nullptr, seven, 4, "the test's constant, unchecked"); }));
    CHECK(run() == 1);
    VirtualFree(page, 0, MEM_RELEASE);
}

// ---- vtable slots

class SlotProbe {
public:
    virtual ~SlotProbe() = default;
    virtual int Add(int x) { return x + 1; }
};
SlotProbe* volatile g_probe = nullptr;
__declspec(noinline) int HookTargetElsewhere() { return 0; }

#ifdef _WIN64
using AddFn = int (*)(void* self, int x);
int DetourAdd(void* self, int x);
#else
using AddFn = int(__fastcall*)(void* self, void* edx, int x);
int __fastcall DetourAdd(void* self, void* edx, int x);
#endif
AddFn g_originalAdd = nullptr;
#ifdef _WIN64
int DetourAdd(void* self, int x) { return g_originalAdd(self, x) + 100; }
#else
int __fastcall DetourAdd(void* self, void* edx, int x) { return g_originalAdd(self, edx, x) + 100; }
#endif

void VtableSlots() {
    std::printf("-- vtable slots swapped and put back\n");
    g_probe = new SlotProbe();
    auto** vtable = *reinterpret_cast<void***>(g_probe);
    void** slot = vtable + 1;
    void* const game = *slot;
    MEMORY_BASIC_INFORMATION before{};
    CHECK(VirtualQuery(slot, &before, sizeof before) != 0);
    CHECK(g_probe->Add(1) == 2);

    hooks::SlotSwaps swaps;
    g_originalAdd = reinterpret_cast<AddFn>(swaps.Swap(slot, reinterpret_cast<void*>(&DetourAdd), "the probe's Add"));
    CHECK(reinterpret_cast<void*>(g_originalAdd) == game);
    CHECK(swaps.Count() == 1);
    CHECK(g_probe->Add(1) == 102);
    MEMORY_BASIC_INFORMATION after{};
    CHECK(VirtualQuery(slot, &after, sizeof after) && after.Protect == before.Protect);
    swaps.RestoreAll();
    CHECK(swaps.Count() == 0);
    CHECK(*slot == game);
    CHECK(g_probe->Add(1) == 2);

    // Swapped twice, put back to the first: the latest swap is undone first.
    swaps.Swap(slot, reinterpret_cast<void*>(&DetourAdd), "the probe's Add");
    swaps.Swap(slot, reinterpret_cast<void*>(&DetourAdd), "the probe's Add again");
    swaps.RestoreAll();
    CHECK(*slot == game);

    // Another mod swaps the slot after us: putting ours back leaves theirs alone, and says so.
    swaps.Swap(slot, reinterpret_cast<void*>(&DetourAdd), "the probe's Add");
    void* const theirs = reinterpret_cast<void*>(&HookTargetElsewhere);
    hooks::SwapSlot(slot, theirs, "another mod's Add");
    CHECK(Throws([&] { swaps.RestoreAll(); }));
    CHECK(*slot == theirs && swaps.Count() == 0);
    hooks::SwapSlot(slot, game, "the probe's Add, put back by hand");

    // A slot on an executable page keeps execute while it is written.
    auto* page = static_cast<void**>(VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READ));
    if (!page) throw std::runtime_error("no page for the test's slot");
    CHECK(hooks::SwapSlot(page + 2, game, "a slot beside code") == nullptr);
    CHECK(page[2] == game);
    MEMORY_BASIC_INFORMATION code{};
    CHECK(VirtualQuery(page, &code, sizeof code) && code.Protect == PAGE_EXECUTE_READ);
    CHECK(Throws([&] { hooks::SwapSlot(reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(page) + 1), game, "a slot off its alignment"); }));
    VirtualFree(page, 0, MEM_RELEASE);
    delete g_probe;
}

// ---- MinHook, or the vtable entries where it has no memory

__declspec(noinline) int __cdecl HookTarget(int x) {
    g_sink = g_sink + 1;
    return x + 1;
}
using IntFn = int(__cdecl*)(int);
IntFn g_originalTarget = nullptr;
int __cdecl HookDetour(int x) { return g_originalTarget(x) + 100; }
int(__cdecl* volatile g_callTarget)(int) = &HookTarget;

void HookOrPoint() {
    std::printf("-- hooked by MinHook, or through the vtables where it has no memory\n");
    auto& hm = hooks::HookManager::Instance();
    if (!hm.IsInitialized() && hm.Initialize() != hooks::HookStatus::Ok) throw std::runtime_error("MinHook did not initialise");
    hooks::SlotSwaps pointed;
    void* const target = reinterpret_cast<void*>(&HookTarget);
    CHECK(hooks::HookOrPointVtables(target, {}, reinterpret_cast<void*>(&HookDetour), reinterpret_cast<void**>(&g_originalTarget), pointed,
                                    "the test target") == hooks::HookedBy::MinHook);
    CHECK(g_callTarget(1) == 102);
    CHECK(pointed.Count() == 0);
    // Hooked already: MinHook's refusal is not a want of memory, so nothing is pointed.
    CHECK(Throws([&] {
        void* unused = nullptr;
        hooks::HookOrPointVtables(target, {}, reinterpret_cast<void*>(&HookDetour), &unused, pointed, "the test target again");
    }));
    CHECK(hm.RemoveHook(target) == hooks::HookStatus::Ok);
    CHECK(g_callTarget(1) == 2);

#ifdef _WIN64
    // A function on a page far from everything, with every free granule within MinHook's reach of it reserved.
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const std::uintptr_t gran = si.dwAllocationGranularity;
    std::uint8_t* code = nullptr;
    for (std::uintptr_t at = 0x100000000000ull; !code && at < 0x700000000000ull; at += 0x10000000000ull)
        code = static_cast<std::uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(at), gran, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!code) throw std::runtime_error("no far page for the test's function");
    // mov eax, ecx; add eax, 1; nop x3; ret
    const std::uint8_t addOne[9] = {0x89, 0xC8, 0x83, 0xC0, 0x01, 0x90, 0x90, 0x90, 0xC3};
    std::memcpy(code, addOne, sizeof addOne);
    MakeExecutable(code, gran);
    std::vector<void*> fences;
    const auto origin = reinterpret_cast<std::uintptr_t>(code);
    // Past MinHook's reach either way: MAX_MEMORY_RANGE 0x40000000 in vendor/minhook/src/buffer.c.
    constexpr std::uintptr_t kAround = 0x48000000;
    for (std::uintptr_t at = origin - kAround; at < origin + kAround;) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<void*>(at), &mbi, sizeof mbi)) break;
        const std::uintptr_t regionEnd = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (mbi.State == MEM_FREE) {
            const std::uintptr_t start = (at + gran - 1) & ~(gran - 1);
            const std::uintptr_t end = (regionEnd < origin + kAround ? regionEnd : origin + kAround) & ~(gran - 1);
            if (end > start)
                if (void* fence = VirtualAlloc(reinterpret_cast<void*>(start), end - start, MEM_RESERVE, PAGE_NOACCESS)) fences.push_back(fence);
        }
        at = regionEnd;
    }
    void* table[2] = {code, code};
    void* const distant = code;
    IntFn original = nullptr;
    g_originalTarget = nullptr;
    CHECK(hooks::HookOrPointVtables(distant, {&table[0], &table[1]}, reinterpret_cast<void*>(&HookDetour), reinterpret_cast<void**>(&original),
                                    pointed, "the far function") == hooks::HookedBy::Vtables);
    g_originalTarget = original;
    CHECK(original == reinterpret_cast<IntFn>(distant));
    CHECK(pointed.Count() == 2);
    CHECK(reinterpret_cast<IntFn>(table[0])(1) == 102 && reinterpret_cast<IntFn>(table[1])(5) == 106);
    CHECK(reinterpret_cast<IntFn>(distant)(1) == 2);
    pointed.RestoreAll();
    CHECK(table[0] == distant && table[1] == distant);
    // No vtable to fall back on, and an entry that holds something else.
    CHECK(Throws([&] {
        void* unused = nullptr;
        hooks::HookOrPointVtables(distant, {}, reinterpret_cast<void*>(&HookDetour), &unused, pointed, "the far function");
    }));
    void* other[1] = {reinterpret_cast<void*>(&HookTarget)};
    CHECK(Throws([&] {
        void* unused = nullptr;
        hooks::HookOrPointVtables(distant, {&other[0]}, reinterpret_cast<void*>(&HookDetour), &unused, pointed, "the far function");
    }));
    CHECK(other[0] == reinterpret_cast<void*>(&HookTarget) && pointed.Count() == 0);
    for (void* fence : fences) VirtualFree(fence, 0, MEM_RELEASE);
    VirtualFree(code, 0, MEM_RELEASE);
#endif
}

// ---- debug-register breakpoints, with the code left as it is

int g_stopped = 0;
int g_inner = 0;
std::uintptr_t g_returnTo = 0;
__declspec(noinline) int Stopped1(int a, int b) {
    g_sink = g_sink + 1;
    return a * 10 + b;
}
__declspec(noinline) int Stopped2(int a, int b) {
    g_sink = g_sink + 1;
    return a * 100 + b;
}
__declspec(noinline) int Stopped3(int a, int b) {
    g_sink = g_sink + 1;
    return a * 1000 + b;
}
__declspec(noinline) int Stopped4(int a, int b) {
    g_sink = g_sink + 1;
    return a * 10000 + b;
}
int (*volatile g_callStopped1)(int, int) = &Stopped1;
void OnStopped(CONTEXT& context) {
    ++g_stopped;
    // At the entry the stack pointer holds the caller's return address.
    g_returnTo = *reinterpret_cast<const std::uintptr_t*>(breakpoint::StackPointer(context));
}
// Stops again inside itself, at its own breakpoint and at another slot's, as
// a frame hook's render handler does when it calls the view setter.
void OnStoppedReentering(CONTEXT&) {
    static thread_local int depth = 0;
    ++g_inner;
    if (depth > 0) return;
    ++depth;
    g_sink = Stopped3(0, 0) + Stopped1(0, 0);
    --depth;
}
void OnStoppedThrowing(CONTEXT&) { throw std::runtime_error("a handler that throws"); }
// The arming pass reaches the calling thread from its own thread, once a second.
void WaitForArming() { std::this_thread::sleep_for(std::chrono::milliseconds(2500)); }

bool InThisExe(std::uintptr_t address) {
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    return address > base && address < base + nt->OptionalHeader.SizeOfImage;
}

// What a breakpoint is put on: it reads what its first argument points at.
__declspec(noinline) int __cdecl ReadFirst(const int* p) { return *static_cast<const volatile int*>(p); }
int(__cdecl* volatile g_read)(const int*) = &ReadFirst;
int g_swapped = 42;
void HandTheSwapped(CONTEXT& context) {
#ifdef _WIN64
    context.Rcx = reinterpret_cast<DWORD64>(&g_swapped);
#else
    *reinterpret_cast<const int**>(breakpoint::StackPointer(context) + 4) = &g_swapped;
#endif
}

void Breakpoints() {
    std::printf("-- debug-register breakpoints\n");
    unsigned char before[16];
    std::memcpy(before, reinterpret_cast<const void*>(&Stopped1), sizeof(before));
    breakpoint::Set(0, reinterpret_cast<std::uintptr_t>(&Stopped1), &OnStopped);
    WaitForArming();
    CHECK(Stopped1(3, 4) == 34);
    CHECK(Stopped1(5, 6) == 56);
    CHECK(g_stopped == 2);
    CHECK(breakpoint::Hits(0) == 2);
    CHECK(InThisExe(g_returnTo));
    CHECK(std::memcmp(before, reinterpret_cast<const void*>(&Stopped1), sizeof(before)) == 0);

    // Moved twice before this thread is armed again: it still stops where the
    // slot was two moves ago, which is passed over, and is armed on the spot.
    breakpoint::Set(0, reinterpret_cast<std::uintptr_t>(&Stopped2), &OnStopped);
    breakpoint::Set(0, reinterpret_cast<std::uintptr_t>(&Stopped3), &OnStopped);
    CHECK(Stopped1(1, 2) == 12);
    CHECK(g_stopped == 2);
    CHECK(Stopped2(1, 2) == 102);
    CHECK(Stopped3(1, 2) == 1002);
    CHECK(g_stopped == 3);

    // Debug registers 2 and 0, a handler stopping again inside itself, and a
    // thread started after the breakpoints went in.
    breakpoint::Set(0, reinterpret_cast<std::uintptr_t>(&Stopped1), &OnStopped);
    breakpoint::Set(1, reinterpret_cast<std::uintptr_t>(&Stopped2), &OnStopped);
    breakpoint::Set(2, reinterpret_cast<std::uintptr_t>(&Stopped3), &OnStoppedReentering);
    WaitForArming();
    g_stopped = 0;
    CHECK(Stopped2(2, 3) == 203);
    CHECK(g_stopped == 1);
    CHECK(Stopped3(2, 3) == 2003);
    CHECK(g_inner == 2);
    CHECK(g_stopped == 2);
    int later = 0;
    std::thread([&later] {
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        later = Stopped1(4, 5) + Stopped2(4, 5);
    }).join();
    CHECK(later == 45 + 405);
    CHECK(g_stopped == 4);

    CHECK(Throws([] { breakpoint::Set(breakpoint::kSlots, reinterpret_cast<std::uintptr_t>(&Stopped1), &OnStopped); }));
    CHECK(Throws([] { breakpoint::Set(-1, reinterpret_cast<std::uintptr_t>(&Stopped1), &OnStopped); }));
    CHECK(Throws([] { breakpoint::Set(1, 0, &OnStopped); }));
    CHECK(Throws([] { breakpoint::Set(1, reinterpret_cast<std::uintptr_t>(&Stopped2), nullptr); }));
    CHECK(Throws([] { breakpoint::Hits(breakpoint::kSlots); }));
}

// The handler changes what the function runs with, on a thread running before the breakpoint was set, on one
// started after, and on one whose debug registers were cleared after it was armed.
void BreakpointsChangeTheThread() {
    std::printf("-- a breakpoint's handler changes what the code runs with\n");
    const int mine = 7;
    const std::uint64_t hits = breakpoint::Hits(0);
    HANDLE running = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE go = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    int seen = 0;
    std::thread early([&] {
        SetEvent(running);
        WaitForSingleObject(go, INFINITE);
        seen = g_read(&mine);
    });
    WaitForSingleObject(running, INFINITE);
    // Set arms every other thread before it returns.
    breakpoint::Set(0, reinterpret_cast<std::uintptr_t>(&ReadFirst), &HandTheSwapped);
    SetEvent(go);
    early.join();
    CHECK(seen == 42 && breakpoint::Hits(0) == hits + 1);

    ResetEvent(running);
    ResetEvent(go);
    int again = 0;
    std::thread cleared([&] {
        SetEvent(running);
        WaitForSingleObject(go, INFINITE);
        again = g_read(&mine);
    });
    WaitForSingleObject(running, INFINITE);
    WaitForArming();
    CONTEXT wiped{};
    wiped.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    CHECK(GetThreadContext(cleared.native_handle(), &wiped) && wiped.Dr3 == reinterpret_cast<std::uintptr_t>(&ReadFirst));
    wiped.Dr0 = wiped.Dr1 = wiped.Dr2 = wiped.Dr3 = wiped.Dr7 = 0;
    CHECK(SetThreadContext(cleared.native_handle(), &wiped));
    WaitForArming();
    SetEvent(go);
    cleared.join();
    CloseHandle(go);
    CloseHandle(running);
    CHECK(again == 42 && breakpoint::Hits(0) == hits + 2);
}

// Slot 3 is debug register 1: four breakpoints at once.
void FourthSlot() {
    std::printf("-- four breakpoints at once\n");
    breakpoint::Set(0, reinterpret_cast<std::uintptr_t>(&Stopped1), &OnStopped);
    breakpoint::Set(1, reinterpret_cast<std::uintptr_t>(&Stopped2), &OnStopped);
    breakpoint::Set(2, reinterpret_cast<std::uintptr_t>(&Stopped3), &OnStopped);
    breakpoint::Set(3, reinterpret_cast<std::uintptr_t>(&Stopped4), &OnStopped);
    WaitForArming();
    g_stopped = 0;
    const std::uint64_t fourth = breakpoint::Hits(3);
    CHECK(Stopped1(1, 1) + Stopped2(1, 1) + Stopped3(1, 1) + Stopped4(1, 1) == 11 + 101 + 1001 + 10001);
    CHECK(g_stopped == 4 && breakpoint::Hits(3) == fourth + 1);
    CONTEXT context{};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    CHECK(Throws([&] { breakpoint::OnReturn(context, reinterpret_cast<std::uintptr_t>(&Stopped1), &OnStopped); }));

    // A handler that throws: the thread goes on past the breakpoint.
    breakpoint::Set(3, reinterpret_cast<std::uintptr_t>(&Stopped4), &OnStoppedThrowing);
    CHECK(Stopped4(2, 2) == 20002);
}

// ---- OnReturn, in a process of its own

int g_returned = 0;
void ReturnSeen(CONTEXT& context) {
    ++g_returned;
#ifdef _WIN64
    context.Rax = 99;
#else
    context.Eax = 99;
#endif
}
void StopAndWaitForReturn(CONTEXT& context) {
    breakpoint::OnReturn(context, *reinterpret_cast<const std::uintptr_t*>(breakpoint::StackPointer(context)), &ReturnSeen);
}

void OnReturn() {
    std::printf("-- a handler runs another when its call returns\n");
    breakpoint::Set(0, reinterpret_cast<std::uintptr_t>(&Stopped1), &StopAndWaitForReturn);
    WaitForArming();
    CHECK(g_callStopped1(1, 2) == 99);
    CHECK(g_returned == 1);
    // Once: the return breakpoint was cleared where it fired.
    breakpoint::Set(0, reinterpret_cast<std::uintptr_t>(&Stopped2), &OnStopped);
    g_returned = 0;
    CHECK(g_callStopped1(1, 2) == 12);
    CHECK(g_returned == 0);
    CONTEXT wiped{};
    wiped.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    CHECK(GetThreadContext(GetCurrentThread(), &wiped) && (wiped.Dr7 & 0xC) == 0);
    CHECK(Throws([] { breakpoint::Set(3, reinterpret_cast<std::uintptr_t>(&Stopped4), &OnStopped); }));
    CONTEXT context{};
    // A context without the debug registers in it, then one with them but no address.
    CHECK(Throws([&] { breakpoint::OnReturn(context, reinterpret_cast<std::uintptr_t>(&Stopped1), &ReturnSeen); }));
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    CHECK(Throws([&] { breakpoint::OnReturn(context, 0, &ReturnSeen); }));
}

void Registers() {
    std::printf("-- general registers and the stack pointer from a context\n");
    CONTEXT c{};
#ifdef _WIN64
    c.Rax = 1;
    c.Rsp = 0x1000;
    c.Rdi = 7;
    c.R15 = 15;
    CHECK(breakpoint::GeneralRegister(c, 0) == 1 && breakpoint::GeneralRegister(c, 4) == 0x1000 && breakpoint::GeneralRegister(c, 7) == 7 &&
          breakpoint::GeneralRegister(c, 15) == 15);
    CHECK(breakpoint::StackPointer(c) == 0x1000);
    CHECK(Throws([&] { breakpoint::GeneralRegister(c, 16); }));
#else
    c.Eax = 1;
    c.Esp = 0x1000;
    c.Edi = 7;
    CHECK(breakpoint::GeneralRegister(c, 0) == 1 && breakpoint::GeneralRegister(c, 4) == 0x1000 && breakpoint::GeneralRegister(c, 7) == 7);
    CHECK(breakpoint::StackPointer(c) == 0x1000);
    CHECK(Throws([&] { breakpoint::GeneralRegister(c, 8); }));
#endif
    CHECK(Throws([&] { breakpoint::GeneralRegister(c, -1); }));
}

void ProtectionDiverted() {
    std::printf("-- whether NtProtectVirtualMemory jumps into a module\n");
    const auto protect = reinterpret_cast<std::uintptr_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtProtectVirtualMemory"));
    const auto read = [](std::uintptr_t at, void* out, std::size_t n) {
        std::memcpy(out, reinterpret_cast<const void*>(at), n);
        return true;
    };
    CHECK(!breakpoint::ProtectionDivertedInto(0, 0));
    CHECK(breakpoint::ProtectionDivertedInto(0, UINTPTR_MAX) == planted::JumpAt(read, protect).has_value());
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--on-return") {
            OnReturn();
        } else {
            PlantedHook();
            Redirect();
            SwapCode();
            VtableSlots();
            HookOrPoint();
            Registers();
            ProtectionDiverted();
            Breakpoints();
            BreakpointsChangeTheThread();
            FourthSlot();
        }
    } catch (const std::exception& e) {
        ++g_failures;
        std::printf("  FAIL: a test threw: %s\n", e.what());
    }
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
