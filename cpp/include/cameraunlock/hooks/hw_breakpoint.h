// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Copyright (c) 2026 itsloopyo

#pragma once

#ifndef _WIN32
#error "cameraunlock/hooks/hw_breakpoint.h is Windows-only: it sets the process's debug registers."
#endif

#include <Windows.h>

#include <cstdint>

// Runs a handler on whichever thread reaches an instruction, without writing the instruction: an execute breakpoint
// in a debug register and a vectored exception handler. For code that takes no inline hook: a module that watches
// its own code (Far Cry 4's FC64.dll, far-cry-4-headtracking src/exec_breakpoint.cpp), or an instruction inside a
// function with nothing to attach to (No Man's Sky's camera commit, no-mans-sky-headtracking
// src/camera/commit_hook.cpp). x86 and x64.
namespace cameraunlock::hooks::hw_breakpoint {

// Called on the thread that reached the address, with its registers as they were there, before the instruction
// runs; what it changes in `context` is what the thread resumes with. An exception it lets out is caught and logged
// once, and the thread goes on past the breakpoint: an exception may not unwind through the exception dispatcher.
using Handler = void (*)(CONTEXT& context);

// Debug registers 3, 2, 0 and 1, as a debugger hands out 0 first. Slot 3 (register 1) and OnReturn share a
// register, so a process uses one or the other.
constexpr int kSlots = 4;

// Puts slot `slot`'s breakpoint on `address`, or moves it there: at once on every other thread of the process, and
// on the calling thread and each new one within a second (a thread stopped by any slot gets every slot as it is on
// the spot). A thread whose debug registers are cleared is armed again within a second. Nothing is written into the
// code. Throws on a slot out of range, a null address or handler, slot 3 after OnReturn was used, or when the
// vectored exception handler does not register.
void Set(int slot, std::uintptr_t address, Handler handler);

// How many times a thread has reached slot `slot`'s address, wherever the slot was at the time.
std::uint64_t Hits(int slot);

// From inside a handler: runs `handler` once when this thread reaches `at` (the return address of the call the
// handler stopped at), through debug register 1 set in `context`, which is cleared again there. Only one is pending
// per thread; another replaces it. Throws when slot 3 is in use.
void OnReturn(CONTEXT& context, std::uintptr_t at, Handler handler);

// The stack pointer a handler's thread had at the breakpoint.
std::uintptr_t StackPointer(const CONTEXT& context);

// The general register `reg` in `context`, by its encoding number: rax 0 to r15 15 on x64, eax 0 to edi 7 on x86.
// Throws on a number out of range.
std::uintptr_t GeneralRegister(const CONTEXT& context, int reg);

// Whether NtProtectVirtualMemory's first instructions jump into [begin, end), following up to four jumps. A module
// for which this holds watches its own protection, takes no inline hook and is reached by a breakpoint.
bool ProtectionDivertedInto(std::uintptr_t begin, std::uintptr_t end);

}  // namespace cameraunlock::hooks::hw_breakpoint
