#include <cameraunlock/diagnostics/crash_handler.h>
#include <cameraunlock/logging/file_log.h>

#include <atomic>
#include <cstdint>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <Psapi.h>
#pragma comment(lib, "psapi.lib")

namespace cameraunlock::diagnostics {

namespace {

namespace logging = cameraunlock::logging;

// Dedupe in case the OS calls the unhandled filter twice for the same
// record (rare but documented under certain re-raise paths). Without
// this, we'd append two reports for one crash and the second would
// mislead anyone reading the log.
std::atomic<bool> g_alreadyLogged{false};

// Module map captured at install time. Resolving an address from inside the
// filter used to call GetModuleHandleExA/GetModuleBaseNameA once per frame;
// both take the loader lock, so a crash that already holds it (a fault inside
// DllMain, or a LoadLibrary in flight on another thread) deadlocked the filter
// forever. The game froze on its last frame with no dump and had to be killed -
// strictly worse than the crash it replaced.
//
// The snapshot goes stale for modules loaded afterwards; those addresses print
// raw, which is a diagnostic that arrives rather than one that hangs.
constexpr int kMaxModules = 512;
constexpr int kModuleNameLen = 40;

struct ModuleRange {
    std::uintptr_t base;
    std::uintptr_t end;
    char name[kModuleNameLen];
};

ModuleRange g_modules[kMaxModules];
int g_moduleCount = 0;

void SnapshotModules() {
    HMODULE mods[kMaxModules] = {};
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        g_moduleCount = 0;
        return;
    }
    int count = static_cast<int>(needed / sizeof(HMODULE));
    if (count > kMaxModules) count = kMaxModules;

    int stored = 0;
    for (int i = 0; i < count; ++i) {
        MODULEINFO mi = {};
        if (!GetModuleInformation(GetCurrentProcess(), mods[i], &mi, sizeof(mi))) continue;
        ModuleRange& m = g_modules[stored];
        m.base = reinterpret_cast<std::uintptr_t>(mi.lpBaseOfDll);
        m.end = m.base + mi.SizeOfImage;
        m.name[0] = '\0';
        GetModuleBaseNameA(GetCurrentProcess(), mods[i], m.name, kModuleNameLen);
        m.name[kModuleNameLen - 1] = '\0';
        ++stored;
    }
    g_moduleCount = stored;
}

const ModuleRange* FindModule(std::uintptr_t addr) {
    for (int i = 0; i < g_moduleCount; ++i) {
        if (addr >= g_modules[i].base && addr < g_modules[i].end) return &g_modules[i];
    }
    return nullptr;
}

const char* CodeName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_BREAKPOINT:               return "BREAKPOINT";
        case EXCEPTION_DATATYPE_MISALIGNMENT:    return "DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DENORMAL_OPERAND:     return "FLT_DENORMAL_OPERAND";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INEXACT_RESULT:       return "FLT_INEXACT_RESULT";
        case EXCEPTION_FLT_INVALID_OPERATION:    return "FLT_INVALID_OPERATION";
        case EXCEPTION_FLT_OVERFLOW:             return "FLT_OVERFLOW";
        case EXCEPTION_FLT_STACK_CHECK:          return "FLT_STACK_CHECK";
        case EXCEPTION_FLT_UNDERFLOW:            return "FLT_UNDERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW:             return "INT_OVERFLOW";
        case EXCEPTION_INVALID_DISPOSITION:      return "INVALID_DISPOSITION";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
        case EXCEPTION_PRIV_INSTRUCTION:         return "PRIV_INSTRUCTION";
        case EXCEPTION_SINGLE_STEP:              return "SINGLE_STEP";
        case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
        case 0xE06D7363:                         return "CXX_EXCEPTION";  // MSVC throw
        default:                                 return "UNKNOWN";
    }
}

void LogException(EXCEPTION_POINTERS* info) {
    const auto* rec = info->ExceptionRecord;
    const auto addr = reinterpret_cast<std::uintptr_t>(rec->ExceptionAddress);

    logging::EmergencyLine("!! UNHANDLED EXCEPTION");
    logging::EmergencyLine("   code=0x%08lx (%s) flags=0x%lx",
        rec->ExceptionCode, CodeName(rec->ExceptionCode),
        rec->ExceptionFlags);
    logging::EmergencyLine("   address=0x%016llx",
        static_cast<unsigned long long>(addr));

    // Access-violation extra: NumberParameters[0] is access type
    // (0 read, 1 write, 8 DEP), [1] is the inaccessible address.
    if ((rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
      || rec->ExceptionCode == EXCEPTION_IN_PAGE_ERROR)
        && rec->NumberParameters >= 2) {
        const ULONG_PTR kind = rec->ExceptionInformation[0];
        const char* op = (kind == 0) ? "read"
                      : (kind == 1) ? "write"
                      : (kind == 8) ? "DEP"
                      :               "?";
        logging::EmergencyLine("   av: %s @ 0x%016llx", op,
            static_cast<unsigned long long>(rec->ExceptionInformation[1]));
    }

    // Resolve fault address -> module+RVA.
    if (const ModuleRange* faultMod = FindModule(addr)) {
        logging::EmergencyLine("   in %s+0x%llx", faultMod->name,
            static_cast<unsigned long long>(addr - faultMod->base));
    } else {
        logging::EmergencyLine("   in <no module> (raw 0x%016llx)",
            static_cast<unsigned long long>(addr));
    }

    // Stack walk. RtlCaptureStackBackTrace needs no dbghelp - we
    // resolve each frame to module+RVA ourselves so we have something
    // useful even without PDBs. The top few frames will be Win32
    // exception-dispatch plumbing (KiUserExceptionDispatch et al)
    // sitting above the faulted frame on the same thread; the
    // 'address=' line above is the authoritative fault IP.
    logging::EmergencyLine("   stack:");
    // 16 frames rather than 32: this array and everything else on the crash
    // path has to fit in what EXCEPTION_STACK_OVERFLOW leaves us.
    constexpr USHORT kFrames = 16;
    void* frames[kFrames] = {};
    const USHORT n = RtlCaptureStackBackTrace(0, kFrames, frames, nullptr);
    for (USHORT i = 0; i < n; ++i) {
        const auto ip = reinterpret_cast<std::uintptr_t>(frames[i]);
        if (const ModuleRange* frameMod = FindModule(ip)) {
            logging::EmergencyLine("     [%02u] %s+0x%llx", i, frameMod->name,
                static_cast<unsigned long long>(ip - frameMod->base));
        } else {
            logging::EmergencyLine("     [%02u] 0x%016llx <no module>",
                i, static_cast<unsigned long long>(ip));
        }
    }
    logging::EmergencyLine("!! end exception report");
}

// There is exactly one top-level filter slot per process, and returning
// EXCEPTION_CONTINUE_SEARCH from it goes to the OS default - it does NOT chain to the
// filter we displaced. Discarding the previous filter therefore silently disabled the
// host's own crash reporting: an RE Engine title with a dump uploader stops producing
// dumps the moment the mod is installed, and with two cameraunlock plugins in one
// process the second to load blanked the first. Both are the "masks a real fault" case
// the doctrine forbids.
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;

LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* info) {
    if (!g_alreadyLogged.exchange(true)) {
        LogException(info);
    }
    if (g_previousFilter != nullptr) {
        return g_previousFilter(info);
    }
    return EXCEPTION_CONTINUE_SEARCH;  // let WER / OS produce a dump
}

}  // namespace

void InstallCrashHandler() {
    SnapshotModules();
    g_previousFilter = SetUnhandledExceptionFilter(&UnhandledFilter);
    // A second InstallCrashHandler in the same module gets our OWN filter back, and
    // chaining to it would recurse until the stack is gone - taking out the crash
    // report for the crash we were installed to report. (Two separate plugins each
    // linking this static library are fine: distinct UnhandledFilter addresses.)
    if (g_previousFilter == &UnhandledFilter) {
        g_previousFilter = nullptr;
    }
    logging::Line("crash-handler: installed (unhandled-exception filter, %d modules mapped)",
                  g_moduleCount);
}

}  // namespace cameraunlock::diagnostics
