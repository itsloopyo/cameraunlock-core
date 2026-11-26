// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Copyright (c) 2026 itsloopyo

#include <cameraunlock/hooks/code_patch.h>

#include <Windows.h>
#include <intrin.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>

namespace cameraunlock::hooks::code_patch {
namespace {

// The widest store the CPU makes whole: cmpxchg16b on x64, cmpxchg8b on x86.
#ifdef _WIN64
constexpr std::uintptr_t kBlock = 16;
#else
constexpr std::uintptr_t kBlock = 8;
#endif

struct Stored {
    bool swapped;
    bool protectedAgain;
};

// The `size` bytes at `at` replaced in one compare-exchange of the aligned block around them, against the block as it
// is read here with `expected` spliced in.
Stored StoreInBlock(std::uintptr_t at, const void* expected, const void* replacement, std::size_t size, const std::string& unwritable) {
    const std::uintptr_t start = at & ~(kBlock - 1);
    void* block = reinterpret_cast<void*>(start);
    // Two patches on one page would each put back the protection the other opened.
    static std::mutex patching;
    std::lock_guard<std::mutex> hold(patching);
    DWORD old = 0;
    if (!VirtualProtect(block, kBlock, PAGE_EXECUTE_READWRITE, &old)) throw std::runtime_error(unwritable);
    alignas(16) std::uint8_t before[kBlock];
    std::memcpy(before, block, kBlock);
    std::memcpy(before + (at - start), expected, size);
    alignas(16) std::uint8_t after[kBlock];
    std::memcpy(after, before, kBlock);
    std::memcpy(after + (at - start), replacement, size);
#ifdef _WIN64
    LONG64 low = 0, high = 0;
    std::memcpy(&low, after, 8);
    std::memcpy(&high, after + 8, 8);
    const bool swapped = _InterlockedCompareExchange128(static_cast<LONG64*>(block), high, low, reinterpret_cast<LONG64*>(before)) != 0;
#else
    LONG64 from = 0, to = 0;
    std::memcpy(&from, before, 8);
    std::memcpy(&to, after, 8);
    const bool swapped = _InterlockedCompareExchange64(static_cast<volatile LONG64*>(block), to, from) == from;
#endif
    DWORD restored = 0;
    const bool protectedAgain = VirtualProtect(block, kBlock, old, &restored) != 0;
    return {swapped, protectedAgain};
}

bool InOneBlock(std::uintptr_t at, std::size_t size) { return size <= kBlock && (at & (kBlock - 1)) + size <= kBlock; }

std::string Hex(std::uintptr_t value) {
    char text[2 + 2 * sizeof(value) + 1];
    std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(value));
    return text;
}

}  // namespace

#ifdef _WIN64
namespace {

// How far below a site the detours are placed, short of a rel32's 2 GB.
constexpr std::uintptr_t kReach = 0x7FF00000;

}  // namespace

std::uint8_t* AllocNear(std::uintptr_t site, const char* what) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const std::uintptr_t gran = si.dwAllocationGranularity;
    const std::uintptr_t low = site > kReach ? site - kReach : gran;
    for (std::uintptr_t at = (site - gran) & ~(gran - 1); at > low; at -= gran) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(reinterpret_cast<void*>(at), &mbi, sizeof(mbi))) break;
        if (mbi.State != MEM_FREE) {
            at = reinterpret_cast<std::uintptr_t>(mbi.AllocationBase);
            if (at < gran) break;
            continue;
        }
        if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), gran, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))
            return static_cast<std::uint8_t*>(p);
    }
    throw std::runtime_error(std::string("no free memory within 2 GB below the exe for a detour from ") + what);
}

void JumpAbsolute(std::uint8_t* at, const void* to) {
    at[0] = 0xFF;  // jmp [rip + 0]
    at[1] = 0x25;
    std::memset(at + 2, 0, 4);
    std::memcpy(at + 6, &to, sizeof(to));
}
#endif

Redirect PrepareRedirect(std::uintptr_t site, void* to, std::uintptr_t expected, const char* what) {
    const std::string name(what);
    auto* call = reinterpret_cast<std::uint8_t*>(site);
    std::int32_t rel;
    std::memcpy(&rel, call + 1, 4);
    if (call[0] != 0xE8 || site + 5 + static_cast<std::intptr_t>(rel) != expected)
        throw std::runtime_error("the call to " + name + " is not the one discovery read");
#ifdef _WIN64
    if (!InOneBlock(site + 1, 4))
        throw std::runtime_error("the displacement of the call to " + name + " crosses a 16-byte boundary, so it cannot be rewritten in one store");
    std::uint8_t* stub = AllocNear(site, what);
    JumpAbsolute(stub, to);
    DWORD old = 0;
    if (!VirtualProtect(stub, kJumpBytes, PAGE_EXECUTE_READ, &old)) throw std::runtime_error(name + "'s detour could not be made executable");
    FlushInstructionCache(GetCurrentProcess(), stub, kJumpBytes);
    const std::int64_t delta = reinterpret_cast<std::intptr_t>(stub) - static_cast<std::intptr_t>(site + 5);
    if (delta < INT32_MIN || delta > INT32_MAX) throw std::runtime_error(name + "'s detour is out of a call's reach");
    return {call, static_cast<std::int32_t>(delta), rel};
#else
    if (!InOneBlock(site + 1, 4))
        throw std::runtime_error("the displacement of the call to " + name + " crosses an 8-byte boundary, so it cannot be rewritten in one store");
    // A rel32 wraps around a 32-bit address space, so every target is in reach.
    return {call, static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(to) - (site + 5)), rel};
#endif
}

void ApplyRedirect(const Redirect& r, const char* what) {
    const std::string name(what);
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(r.call + 1);
    if (!InOneBlock(at, 4)) throw std::invalid_argument("the call to " + name + " was not prepared by PrepareRedirect");
    const Stored s = StoreInBlock(at, &r.was, &r.rel, 4, "the call to " + name + " could not be made writable");
    if (!s.swapped)
        throw std::runtime_error("the call to " + name + " no longer holds the displacement discovery read, or changed while it was redirected");
    if (!s.protectedAgain) throw std::runtime_error("the call to " + name + " could not be made read-only again");
    FlushInstructionCache(GetCurrentProcess(), r.call, 5);
}

void SwapCode(void* at, const void* expected, const void* replacement, std::size_t size, const char* what) {
    const std::string name(what);
    const auto address = reinterpret_cast<std::uintptr_t>(at);
    if (!expected || !replacement) throw std::invalid_argument(name + ": SwapCode needs the bytes expected there and their replacement");
    if (size == 0 || !InOneBlock(address, size))
        throw std::invalid_argument(name + ": " + std::to_string(size) + " bytes at " + Hex(address) + " do not lie in one aligned " +
                                    std::to_string(kBlock) + "-byte block, so they cannot be written in one store");
    const Stored s = StoreInBlock(address, expected, replacement, size, name + " could not be made writable");
    if (!s.swapped) throw std::runtime_error(name + " does not hold the code it was expected to, or changed while it was patched");
    if (!s.protectedAgain) throw std::runtime_error(name + " could not be given its protection back");
    FlushInstructionCache(GetCurrentProcess(), at, size);
}

}  // namespace cameraunlock::hooks::code_patch
