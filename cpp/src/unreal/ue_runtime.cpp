#include <cameraunlock/unreal/ue_runtime.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace cameraunlock::unreal {

namespace {

std::uintptr_t g_moduleBase = 0;
std::uintptr_t g_moduleEnd  = 0;
UObjectGlobalsLayout g_layout = {};

}  // namespace

void SetRuntime(std::uintptr_t moduleBase, std::uintptr_t moduleEnd,
                const UObjectGlobalsLayout& layout) {
    g_moduleBase = moduleBase;
    g_moduleEnd  = moduleEnd;
    g_layout     = layout;
}
std::uintptr_t ModuleBase() { return g_moduleBase; }
std::uintptr_t ModuleEnd()  { return g_moduleEnd; }
const UObjectGlobalsLayout& Layout() { return g_layout; }

bool SafeReadPtr(std::uintptr_t addr, std::uintptr_t& out) {
    __try {
        out = *reinterpret_cast<const std::uintptr_t*>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeReadU32(std::uintptr_t addr, std::uint32_t& out) {
    __try { out = *reinterpret_cast<const std::uint32_t*>(addr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SafeReadU16(std::uintptr_t addr, std::uint16_t& out) {
    __try { out = *reinterpret_cast<const std::uint16_t*>(addr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SafeReadFQuat(std::uintptr_t addr, FQuat4d& out) {
    __try {
        out = *reinterpret_cast<const FQuat4d*>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeReadFVector(std::uintptr_t addr, FVector& out) {
    __try {
        out.X = *reinterpret_cast<const double*>(addr + 0);
        out.Y = *reinterpret_cast<const double*>(addr + 8);
        out.Z = *reinterpret_cast<const double*>(addr + 16);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeWriteFQuat(std::uintptr_t addr, const FQuat4d& q) {
    __try {
        *reinterpret_cast<FQuat4d*>(addr) = q;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeWriteFVector(std::uintptr_t addr, const FVector& v) {
    __try {
        *reinterpret_cast<double*>(addr + 0)  = v.X;
        *reinterpret_cast<double*>(addr + 8)  = v.Y;
        *reinterpret_cast<double*>(addr + 16) = v.Z;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool LooksLikePointer(std::uintptr_t v) {
    // Heap or .data in the user-mode 64-bit range, 8-byte aligned, not the
    // small immediate range that often appears in flag fields.
    if (v < 0x10000) return false;
    if (v > 0x7fffffffffffULL) return false;
    if ((v & 0x7) != 0) return false;
    return true;
}

// FNamePool layout (UE5): Blocks[] at pool + kFNamePoolBlocks, block stride 2,
// entry header uint16 (Len = header>>6, bIsWide = bit0), chars at +2.
std::string ResolveFName(std::uint32_t id) {
    if (g_moduleBase == 0) return std::string();
    const std::uintptr_t blocks =
        g_moduleBase + g_layout.kFNamePool + g_layout.kFNamePoolBlocks;
    std::uintptr_t blockPtr = 0;
    if (!SafeReadPtr(blocks + (static_cast<std::uintptr_t>(id >> 16) * 8), blockPtr))
        return std::string();
    if (!blockPtr) return std::string();
    const std::uintptr_t entry = blockPtr + (static_cast<std::uintptr_t>(id & 0xffff) * 2);
    std::uint16_t header = 0;
    if (!SafeReadU16(entry, header)) return std::string();
    const bool isWide = (header & 1) != 0;
    const int len = header >> 6;
    if (len <= 0 || len > 1024) return std::string();
    std::string out;
    out.reserve(len);
    if (!isWide) {
        for (int i = 0; i < len; ++i) {
            std::uint16_t b = 0;
            if (!SafeReadU16(entry + 2 + i, b)) break;  // overlapping read ok
            out.push_back(static_cast<char>(b & 0xff));
        }
    } else {
        for (int i = 0; i < len; ++i) {
            std::uint16_t w = 0;
            if (!SafeReadU16(entry + 2 + i * 2, w)) break;
            out.push_back(static_cast<char>(w & 0x7f));
        }
    }
    return out;
}

std::string ObjectName(std::uintptr_t obj) {
    std::uint32_t id = 0;
    if (!SafeReadU32(obj + g_layout.kNamePrivate, id))
        return std::string();
    return ResolveFName(id);
}

std::string ClassName(std::uintptr_t obj) {
    std::uintptr_t cls = 0;
    if (!SafeReadPtr(obj + g_layout.kClassPrivate, cls) || !cls)
        return std::string();
    return ObjectName(cls);
}

std::string OuterName(std::uintptr_t obj) {
    std::uintptr_t outer = 0;
    if (!SafeReadPtr(obj + g_layout.kOuterPrivate, outer) || !outer)
        return std::string();
    return ObjectName(outer);
}

std::uintptr_t FindLiveObject(const char* wantClass, const char* wantName,
                              const char* wantOuter) {
    std::uintptr_t found = 0;
    ForEachUObject([&](std::uintptr_t obj) -> bool {
        if (ObjectName(obj) != wantName) return false;
        if (wantClass && ClassName(obj) != wantClass) return false;
        if (wantOuter && OuterName(obj) != wantOuter) return false;
        found = obj;
        return true;
    });
    return found;
}

}  // namespace cameraunlock::unreal
