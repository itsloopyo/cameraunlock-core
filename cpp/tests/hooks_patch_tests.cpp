// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Copyright (c) 2026 itsloopyo

// Tests for the code patching helpers in cameraunlock/hooks, in an executable of their own.

#include <cameraunlock/hooks/code_patch.h>
#include <cameraunlock/hooks/planted_jump.h>

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

namespace code_patch = cameraunlock::hooks::code_patch;
namespace planted = cameraunlock::hooks::planted;

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
    VirtualFree(page, 0, MEM_RELEASE);
}

}  // namespace

int main() {
    try {
        PlantedHook();
        Redirect();
        SwapCode();
    } catch (const std::exception& e) {
        ++g_failures;
        std::printf("  FAIL: a test threw: %s\n", e.what());
    }
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
