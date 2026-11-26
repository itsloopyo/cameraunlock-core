#pragma once

#ifndef _WIN32
#error "cameraunlock/memory/safe_memory.h is Windows-only: it is built on SEH."
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <Windows.h>

// SEH-guarded reads and writes of engine memory that can be freed underneath
// the mod - a struct released on a level transition, a pointer chain walked on
// the render thread a few hundred times a second. They return false instead of
// faulting, so the caller can drop its cached pointer and re-resolve.
//
// These guard OUR OWN dereferences. They must never wrap a call into game code:
// an access violation while dereferencing a node the engine handed us is ours
// to absorb, but one raised inside a game function is the game's, and
// swallowing it would leave the game part-way through whatever it was doing,
// holding whatever locks it took, with the real fault erased from the crash
// dump.
//
// The filter is what keeps these from being blanket swallows: only
// EXCEPTION_ACCESS_VIOLATION and STATUS_GUARD_PAGE_VIOLATION are handled. A
// breakpoint, a C++ exception travelling through, a stack overflow - all keep
// unwinding to whoever owns them.
//
// The fault counter is passed in by the call site rather than kept here, so a
// hook faulting every frame cannot bury a quieter one in a shared total.
//
// Every function is a free function with no unwinding objects in scope, which
// is what makes __try legal (MSVC C2712 forbids it alongside C++ object
// destruction). That is also why the guarded types must be trivially copyable.
namespace cameraunlock::memory {

/// For `__except (AccessViolationFilter(GetExceptionCode()))`. Handles an
/// access violation only. A guard page hit keeps unwinding, since putting the
/// guard back needs the faulting address: use MemoryFaultFilter for that.
inline int AccessViolationFilter(DWORD code) {
    return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                              : EXCEPTION_CONTINUE_SEARCH;
}

/// For `__except (MemoryFaultFilter(GetExceptionInformation()))`. Handles an
/// access violation, and a guard page hit with the guard put back first.
///
/// A guard page turns up under an address a pointer walk lands on when it is
/// another thread's stack (a new thread's stack where a freed block was).
/// Touching it clears the guard and raises STATUS_GUARD_PAGE_VIOLATION here
/// rather than in the owning thread, so the guard is put back before the read
/// is failed: without it that thread's stack could not grow, and left
/// unhandled the exception takes the game down.
inline int MemoryFaultFilter(const EXCEPTION_POINTERS* e) {
    const DWORD code = e->ExceptionRecord->ExceptionCode;
    if (code == STATUS_GUARD_PAGE_VIOLATION) {
        // A guard that cannot be put back is left to crash where it was hit,
        // not later in the owning thread's stack growth.
        if (e->ExceptionRecord->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
        const auto* at = reinterpret_cast<const void*>(e->ExceptionRecord->ExceptionInformation[1]);
        MEMORY_BASIC_INFORMATION mbi;
        DWORD was = 0;
        if (!VirtualQuery(at, &mbi, sizeof mbi) ||
            !VirtualProtect(const_cast<void*>(at), 1, mbi.Protect | PAGE_GUARD, &was))
            return EXCEPTION_CONTINUE_SEARCH;
        return EXCEPTION_EXECUTE_HANDLER;
    }
    return AccessViolationFilter(code);
}

/// memcpy under MemoryFaultFilter, with no staging: a fault part way through
/// leaves `to` holding the bytes copied before it. False on a fault.
inline bool GuardedMemcpy(void* to, const void* from, std::size_t n) {
    __try {
        std::memcpy(to, from, n);
        return true;
    } __except (MemoryFaultFilter(GetExceptionInformation())) {
        return false;
    }
}

/// Copies a trivially-copyable value out of `addr`. False on access violation,
/// with `out` left untouched.
///
/// The copy lands in a local first because a struct wider than the distance to
/// the end of a committed page faults PART WAY THROUGH: a 64-byte read starting
/// 32 bytes before a decommitted page copied 32 bytes and then raised, so
/// assigning straight into `out` returned false having already overwritten half
/// of it. The caller's usual response to false is to keep the value it had, and
/// half of it was gone.
template <typename T>
bool SafeRead(std::uintptr_t addr, T& out) {
    static_assert(std::is_trivially_copyable<T>::value,
                  "SafeRead target must be trivially copyable");
    alignas(T) unsigned char staging[sizeof(T)];
    if (!GuardedMemcpy(staging, reinterpret_cast<const void*>(addr), sizeof(T))) return false;
    std::memcpy(&out, staging, sizeof(T));
    return true;
}

/// Writes a trivially-copyable value to `addr`. False on access violation.
///
/// False does NOT mean nothing was written. A naturally-aligned scalar of at
/// most a pointer's width is all-or-nothing - the store either retires or
/// faults - but an aggregate that straddles the end of a committed page is
/// copied until it reaches the unmapped one, and a 64-byte struct starting 32
/// bytes short of that boundary leaves 32 bytes of the engine's memory already
/// overwritten. There is no fix for that here: the fault is what tells us the
/// range is bad, and by then the prefix is written. A caller that must not
/// leave a half-updated engine struct behind has to write field-sized values
/// itself and treat a mid-sequence false as "partially applied".
template <typename T>
bool SafeWrite(std::uintptr_t addr, const T& in) {
    static_assert(std::is_trivially_copyable<T>::value,
                  "SafeWrite target must be trivially copyable");
    __try {
        *reinterpret_cast<T*>(addr) = in;
        return true;
    } __except (MemoryFaultFilter(GetExceptionInformation())) {
        return false;
    }
}

/// Single byte. Named separately because a byte-at-a-time walk is the one place
/// the width matters: a uint16 read of the last character of a string touches
/// one byte past it, which faults when the string ends on the final byte of a
/// committed page and silently returns the name one character short.
inline bool SafeReadU8(std::uintptr_t addr, std::uint8_t& out) {
    return SafeRead<std::uint8_t>(addr, out);
}

/// Counting forms. The read/write itself is delegated, so the __try stays in
/// one place and these can hold ordinary C++ objects.
template <typename T>
bool SafeRead(std::uintptr_t addr, T& out, std::atomic<std::uint64_t>& faults) {
    if (SafeRead<T>(addr, out)) return true;
    faults.fetch_add(1, std::memory_order_relaxed);
    return false;
}

template <typename T>
bool SafeWrite(std::uintptr_t addr, const T& in, std::atomic<std::uint64_t>& faults) {
    if (SafeWrite<T>(addr, in)) return true;
    faults.fetch_add(1, std::memory_order_relaxed);
    return false;
}

}  // namespace cameraunlock::memory
