// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Copyright (c) 2026 itsloopyo

#include <cameraunlock/hooks/hw_breakpoint.h>

#include <tlhelp32.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>

#include <cameraunlock/hooks/planted_jump.h>
#include <cameraunlock/logging/file_log.h>

namespace cameraunlock::hooks::hw_breakpoint {
namespace {

namespace log = ::cameraunlock::logging;

#ifdef _WIN64
using Reg = DWORD64;
std::uintptr_t Ip(const CONTEXT& c) { return static_cast<std::uintptr_t>(c.Rip); }
#else
using Reg = DWORD;
std::uintptr_t Ip(const CONTEXT& c) { return static_cast<std::uintptr_t>(c.Eip); }
#endif

struct Slot {
    int reg;
    std::atomic<std::uintptr_t> address{0};
    // Where the slot was before its last move: a thread can still stop there
    // until the arming pass reaches it.
    std::atomic<std::uintptr_t> former{0};
    std::atomic<Handler> handler{nullptr};
    std::atomic<bool> used{false};
    std::atomic<std::uint64_t> hits{0};
};
Slot g_slots[kSlots] = {{3}, {2}, {0}, {1}};
constexpr DWORD kResumeFlag = 0x10000;
// A mod's DLL may take no thread attach calls, so threads the game starts
// later are found by looking again this often.
constexpr auto kArmEvery = std::chrono::seconds(1);

// OnReturn's register, which slot 3 also uses: the first to take it keeps it.
constexpr int kReturnRegister = 1;
constexpr int kReturnSlot = 3;
enum Register1Owner { kNobody, kSlot3, kOnReturn };
std::atomic<int> g_register1{kNobody};
bool TakeRegister1(int owner) {
    int expected = kNobody;
    return g_register1.compare_exchange_strong(expected, owner) || expected == owner;
}
thread_local std::uintptr_t t_returnAt = 0;
thread_local Handler t_returnHandler = nullptr;
// Where register 1 still stops this thread after its return breakpoint fired
// in a record that could not clear it.
thread_local std::uintptr_t t_staleReturnAt = 0;

std::mutex g_lock;
std::once_flag g_started;
std::set<DWORD> g_refused;  // under g_lock
// A handler let an exception out, which may not unwind through the
// exception dispatcher; the arming thread says so.
std::atomic<bool> g_handlerThrew{false};
// The first such exception's message, written once by the handler that set
// g_handlerClaimed, read once g_handlerThrew is set.
std::atomic<bool> g_handlerClaimed{false};
char g_handlerError[256];

// The enable pair, then the condition and length fields, zero for an execute breakpoint.
Reg ControlBits(int reg) { return (Reg(3) << (reg * 2)) | (Reg(0xF) << (16 + reg * 4)); }
Reg LocalEnable(int reg) { return Reg(1) << (reg * 2); }
Reg& Register(CONTEXT& context, int reg) {
    switch (reg) {
        case 0: return context.Dr0;
        case 1: return context.Dr1;
        case 2: return context.Dr2;
        default: return context.Dr3;
    }
}

void Write(CONTEXT& context, int reg, std::uintptr_t address) {
    Register(context, reg) = static_cast<Reg>(address);
    context.Dr7 = (context.Dr7 & ~ControlBits(reg)) | (address ? LocalEnable(reg) : 0);
}

// Every slot set so far written into `context`; a register no slot has used
// is left to whoever else uses it.
void WriteSlots(CONTEXT& context) {
    for (const Slot& slot : g_slots)
        if (slot.used.load(std::memory_order_acquire)) Write(context, slot.reg, slot.address.load(std::memory_order_acquire));
}

bool HoldsSlots(CONTEXT context) {
    const CONTEXT held = context;
    WriteSlots(context);
    return std::memcmp(&held.Dr0, &context.Dr0, sizeof(Reg) * 4) == 0 && held.Dr7 == context.Dr7;
}

// The status bits of the registers a slot has used; Dr6 may also show a
// register nothing enabled whose address the instruction happens to match.
Reg OurStatusBits() {
    Reg bits = 0;
    for (const Slot& slot : g_slots)
        if (slot.used.load(std::memory_order_acquire)) bits |= Reg(1) << slot.reg;
    return bits;
}
constexpr Reg kSingleStepStatus = 0x4000;
constexpr DWORD kTrapFlag = 0x100;

void Run(Handler handler, CONTEXT& context) {
    const auto record = [](const char* what) {
        if (g_handlerClaimed.exchange(true, std::memory_order_acq_rel)) return;
        std::snprintf(g_handlerError, sizeof g_handlerError, "%s", what);
        g_handlerThrew.store(true, std::memory_order_release);
    };
    try {
        handler(context);
    } catch (const std::exception& e) {
        record(e.what());
    } catch (...) {
        record("an exception that is not a std::exception");
    }
}

LONG CALLBACK OnException(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT& context = *info->ContextRecord;
    const std::uintptr_t ip = Ip(context);
    // Dr6 names the register that stopped the thread when the record carries
    // the debug registers; a thread whose registers lag a move stops at an
    // address no slot holds any more, and is ours all the same.
    const bool statusKnown = (context.ContextFlags & CONTEXT_DEBUG_REGISTERS) == CONTEXT_DEBUG_REGISTERS;
    const bool trapFlag = (context.EFlags & kTrapFlag) != 0;
    // Dr6 keeps its bits until someone clears them, so the trap flag
    // says whether the single step is this one.
    const bool steppedToo = statusKnown && (context.Dr6 & kSingleStepStatus) != 0 && trapFlag;
    // Register 1 stopped the thread: Dr6 says so where it is known; without it,
    // a step the game's trap flag took to the same address is the game's.
    const bool register1 = statusKnown ? (context.Dr6 & (Reg(1) << kReturnRegister)) != 0 : !trapFlag;
    bool returned = false;
    if (register1 && t_returnAt != 0 && ip == t_returnAt) {
        const Handler handler = t_returnHandler;
        t_returnAt = 0;
        t_returnHandler = nullptr;
        // A record without the debug registers cannot clear register 1, which
        // stops the thread here again until a record that has them does.
        if (statusKnown) Write(context, kReturnRegister, 0);
        else t_staleReturnAt = ip;
        Run(handler, context);
        returned = true;
    } else if (register1 && t_staleReturnAt != 0 && ip == t_staleReturnAt) {
        if (statusKnown) {
            Write(context, kReturnRegister, 0);
            t_staleReturnAt = 0;
        }
        returned = true;
    }
    if (returned && statusKnown) context.Dr6 &= ~(Reg(1) << kReturnRegister);
    bool ours = statusKnown && (context.Dr6 & OurStatusBits()) != 0;
    Slot* hit = nullptr;
    for (Slot& slot : g_slots) {
        if (!slot.used.load(std::memory_order_acquire)) continue;
        if (ip == slot.address.load(std::memory_order_acquire)) hit = &slot;
        if (!statusKnown && (hit == &slot || ip == slot.former.load(std::memory_order_acquire))) ours = true;
    }
    if (!ours && !returned) return EXCEPTION_CONTINUE_SEARCH;
    // A slot on the same instruction as a return breakpoint fires too.
    if (ours && hit) {
        hit->hits.fetch_add(1, std::memory_order_relaxed);
        Run(hit->handler.load(std::memory_order_acquire), context);
    }
    // Without the resume flag the instruction stops again before it runs.
    context.EFlags |= kResumeFlag;
    // A record without the debug registers would write back registers it never
    // read, over whatever else uses them; the arming pass reaches this thread.
    if (statusKnown) {
        context.Dr6 &= ~OurStatusBits();
        // The thread resumes with the debug registers this record holds, which
        // predate a move made meanwhile; the arming pass catches a move made
        // after this.
        WriteSlots(context);
        // The game's own single step landed here as well, and is its to handle.
        if (steppedToo) return EXCEPTION_CONTINUE_SEARCH;
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

// Every thread of the process but this one whose debug registers do not hold
// every slot as it now is. The registers are read from each thread as it is,
// not remembered from an earlier pass: a thread armed while still in
// LdrInitializeThunk has them cleared again before its own code runs, a new
// thread can take the id of one that exited, and a thread inside OnException
// when a slot moves puts back the ones its record held. A thread is suspended
// only to write them, between GetThreadContext and SetThreadContext, which
// take no lock it could hold.
void ArmThreads() {
    const DWORD self = GetCurrentThreadId();
    const DWORD process = GetCurrentProcessId();
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) throw std::runtime_error("no thread snapshot (" + std::to_string(GetLastError()) + ")");
    std::lock_guard<std::mutex> hold(g_lock);
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self) continue;
        // A thread that has exited since the snapshot does not open, and needs no register.
        const HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);
        if (!thread) continue;
        CONTEXT context{};
        context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        DWORD refused = 0;
        if (GetThreadContext(thread, &context) && !HoldsSlots(context) && SuspendThread(thread) != static_cast<DWORD>(-1)) {
            context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(thread, &context)) {
                WriteSlots(context);
                if (!SetThreadContext(thread, &context)) refused = GetLastError();
            }
            ResumeThread(thread);
        }
        // Logged once the thread runs again, as it may hold the log's lock.
        if (refused && g_refused.insert(entry.th32ThreadID).second)
            log::Line("breakpoint: thread %lu refused its debug registers (%lu); it reaches the hooked code unhooked, tried again each second", entry.th32ThreadID, refused);
        CloseHandle(thread);
    }
    CloseHandle(snapshot);
}

}  // namespace

void Set(int slot, std::uintptr_t address, Handler handler) {
    if (slot < 0 || slot >= kSlots) throw std::invalid_argument("breakpoint: no slot " + std::to_string(slot));
    if (address == 0 || !handler) throw std::invalid_argument("breakpoint: a breakpoint needs an address and a handler");
    Slot& s = g_slots[slot];
    {
        std::lock_guard<std::mutex> hold(g_lock);
        if (slot == kReturnSlot && !TakeRegister1(kSlot3))
            throw std::logic_error("breakpoint: slot 3 shares debug register 1 with OnReturn, which this process uses");
        s.former.store(s.address.load(std::memory_order_acquire), std::memory_order_release);
        // The handler before the address: a thread may reach the address as
        // soon as it is published.
        s.handler.store(handler, std::memory_order_release);
        s.address.store(address, std::memory_order_release);
        s.used.store(true, std::memory_order_release);
    }
    try {
        std::call_once(g_started, [] {
            if (!AddVectoredExceptionHandler(1, OnException))
                throw std::runtime_error("breakpoint: the vectored exception handler did not register (" + std::to_string(GetLastError()) + ")");
            std::thread([] {
                std::string lastError;
                bool loggedThrow = false;
                for (;;) {
                    std::this_thread::sleep_for(kArmEvery);
                    if (!loggedThrow && g_handlerThrew.load(std::memory_order_acquire)) {
                        loggedThrow = true;
                        log::Line("ERROR: a breakpoint's handler let an exception out, the game went on past it (logged once): %s", g_handlerError);
                    }
                    // A snapshot that fails now is taken again next pass; the
                    // threads armed so far stay armed.
                    try {
                        ArmThreads();
                    } catch (const std::exception& e) {
                        if (e.what() != lastError) {
                            lastError = e.what();
                            log::Line("breakpoint: arming the game's threads failed this pass, tried again in a second: %s", e.what());
                        }
                    }
                }
            }).detach();
        });
    } catch (...) {
        std::lock_guard<std::mutex> hold(g_lock);
        s.used.store(false, std::memory_order_release);
        s.address.store(0, std::memory_order_release);
        if (slot == kReturnSlot) g_register1.store(kNobody);
        throw;
    }
    ArmThreads();
}

std::uint64_t Hits(int slot) {
    if (slot < 0 || slot >= kSlots) throw std::invalid_argument("breakpoint: no slot " + std::to_string(slot));
    return g_slots[slot].hits.load(std::memory_order_relaxed);
}

void OnReturn(CONTEXT& context, std::uintptr_t at, Handler handler) {
    if (at == 0 || !handler) throw std::invalid_argument("breakpoint: a return breakpoint needs an address and a handler");
    // Written back without them, the record would put registers it never read over every other slot.
    if ((context.ContextFlags & CONTEXT_DEBUG_REGISTERS) != CONTEXT_DEBUG_REGISTERS)
        throw std::invalid_argument("breakpoint: OnReturn needs a context that carries the debug registers");
    if (!TakeRegister1(kOnReturn)) throw std::logic_error("breakpoint: OnReturn shares debug register 1 with slot 3, which this process uses");
    // A return breakpoint still pending (its call never came back the usual
    // way) is replaced.
    t_returnAt = at;
    t_returnHandler = handler;
    t_staleReturnAt = 0;
    Write(context, kReturnRegister, at);
}

std::uintptr_t StackPointer(const CONTEXT& context) {
#ifdef _WIN64
    return static_cast<std::uintptr_t>(context.Rsp);
#else
    return static_cast<std::uintptr_t>(context.Esp);
#endif
}

std::uintptr_t GeneralRegister(const CONTEXT& c, int reg) {
#ifdef _WIN64
    const DWORD64 regs[] = {c.Rax, c.Rcx, c.Rdx, c.Rbx, c.Rsp, c.Rbp, c.Rsi, c.Rdi, c.R8, c.R9, c.R10, c.R11, c.R12, c.R13, c.R14, c.R15};
#else
    const DWORD regs[] = {c.Eax, c.Ecx, c.Edx, c.Ebx, c.Esp, c.Ebp, c.Esi, c.Edi};
#endif
    constexpr int kCount = static_cast<int>(sizeof(regs) / sizeof(regs[0]));
    if (reg < 0 || reg >= kCount) throw std::invalid_argument("breakpoint: no general register " + std::to_string(reg));
    return static_cast<std::uintptr_t>(regs[reg]);
}

bool ProtectionDivertedInto(std::uintptr_t begin, std::uintptr_t end) {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) throw std::runtime_error("ntdll.dll is not loaded");
    const FARPROC protect = GetProcAddress(ntdll, "NtProtectVirtualMemory");
    if (!protect) throw std::runtime_error("ntdll.dll exports no NtProtectVirtualMemory");
    const auto read = [](std::uintptr_t from, void* out, std::size_t size) {
        std::memcpy(out, reinterpret_cast<const void*>(from), size);
        return true;
    };
    std::uintptr_t at = reinterpret_cast<std::uintptr_t>(protect);
    // A detour can reach its body through a trampoline or two of its own.
    for (int hop = 0; hop < 4; ++hop) {
        const std::optional<std::uintptr_t> next = planted::JumpAt(read, at);
        if (!next) return false;
        at = *next;
        if (at >= begin && at < end) return true;
    }
    return false;
}

}  // namespace cameraunlock::hooks::hw_breakpoint
