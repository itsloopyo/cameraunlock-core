#include <cameraunlock/memory/rtti_vtable.h>
#include <cameraunlock/memory/pattern_scanner.h>

#ifdef _WIN32
#include <Windows.h>
#include <Psapi.h>
#endif

#include <cstring>
#include <string>

namespace cameraunlock::memory {

// MSVC x64 RTTI structures
#pragma pack(push, 4)
struct RTTICompleteObjectLocator {
    uint32_t signature;        // 1 for x64
    uint32_t offset;           // offset of this vtable in complete class
    uint32_t cdOffset;         // constructor displacement
    uint32_t pTypeDescriptor;  // RVA of TypeDescriptor
    uint32_t pClassDescriptor; // RVA of ClassHierarchyDescriptor
    uint32_t pSelf;            // RVA of this COL (self-reference for validation)
};
#pragma pack(pop)

namespace {

// The three scan bodies below are separate functions so each can carry its own
// SEH frame (MSVC rejects __try in a function that needs C++ object unwinding).
// NextReadableRange keeps them off PAGE_NOACCESS sections, but a protection
// change can still race the scan, and losing that race must not close the game.
const RTTICompleteObjectLocator* ScanRunForCol(uintptr_t runBase, size_t runSize,
                                               uintptr_t modBase, uint32_t tdRva) {
#ifdef _WIN32
    __try {
#endif
        for (size_t off = 0; off + sizeof(RTTICompleteObjectLocator) <= runSize; off += 4) {
            const uintptr_t addr = runBase + off;
            auto* col = reinterpret_cast<const RTTICompleteObjectLocator*>(addr);

            if (col->signature != 1) continue;  // x64 signature
            // Primary vtable only. Multiple-inheritance classes have one COL per
            // base sub-object; the secondary ones (offset != 0) point at vtables
            // whose vfunc layout belongs to that base, not the class being looked
            // up, so hooking through them lands on the wrong functions.
            if (col->offset != 0) continue;
            if (col->pTypeDescriptor != tdRva) continue;
            if (col->pSelf != static_cast<uint32_t>(addr - modBase)) continue;

            return col;
        }
#ifdef _WIN32
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
#endif
    return nullptr;
}

// vtable[-1] == &COL, so the vtable starts one slot past the pointer we find.
uintptr_t ScanRunForColPointer(uintptr_t runBase, size_t runSize, uintptr_t colAddr) {
#ifdef _WIN32
    __try {
#endif
        for (size_t off = 0; off + sizeof(uintptr_t) <= runSize; off += 8) {
            if (*reinterpret_cast<const uintptr_t*>(runBase + off) == colAddr) {
                return runBase + off;
            }
        }
#ifdef _WIN32
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
#endif
    return 0;
}

int ReadVfuncs(uintptr_t vtable, uintptr_t codeStart, uintptr_t codeEnd,
               uintptr_t* out, int max_vfuncs) {
    int count = 0;
#ifdef _WIN32
    __try {
#endif
        for (int v = 0; v < max_vfuncs; v++) {
            // The COL scan permits a match at the very end of the image, which puts
            // vtable at base+modSize - so the first read is already past the mapping.
            // The "is it a code address" test below only runs after the dereference.
            if (vtable + (v + 1) * sizeof(uintptr_t) > codeEnd) break;
            uintptr_t funcAddr = *reinterpret_cast<const uintptr_t*>(vtable + v * sizeof(uintptr_t));
            // Valid code address: within the module
            if (funcAddr < codeStart || funcAddr >= codeEnd) break;
            out[v] = funcAddr;
            count = v + 1;
        }
#ifdef _WIN32
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
#endif
    return count;
}

}  // namespace

bool FindVtableFromTypeDescriptor(void* module, void* type_descriptor,
                                  VtableInfo& info, int max_vfuncs) {
    if (!module || !type_descriptor) return false;
    if (max_vfuncs > kMaxVfuncEntries) max_vfuncs = kMaxVfuncEntries;

    uintptr_t base = 0;
    size_t modSize = 0;
    if (!GetModuleRange(module, base, modSize)) return false;

    uintptr_t tdAddr = reinterpret_cast<uintptr_t>(type_descriptor);
    uint32_t tdRva = static_cast<uint32_t>(tdAddr - base);

    const uintptr_t modEnd = base + modSize;

    // Scan the module for a COL whose pTypeDescriptor == tdRva
    const RTTICompleteObjectLocator* foundCol = nullptr;
    uintptr_t cursor = base;
    uintptr_t runBase = 0;
    size_t runSize = 0;
    while (!foundCol && NextReadableRange(cursor, modEnd, runBase, runSize)) {
        foundCol = ScanRunForCol(runBase, runSize, base, tdRva);
    }

    if (!foundCol) return false;

    uintptr_t colAddr = reinterpret_cast<uintptr_t>(foundCol);
    info.col_address = colAddr;

    // Now find the vtable: scan for a pointer to this COL.
    cursor = base;
    while (NextReadableRange(cursor, modEnd, runBase, runSize)) {
        uintptr_t colPtr = ScanRunForColPointer(runBase, runSize, colAddr);
        if (!colPtr) continue;

        // This is vtable[-1]. vtable[0] starts at the next slot.
        uintptr_t vtable = colPtr + sizeof(uintptr_t);
        info.vtable_address = vtable;

        // Read vfunc entries, stopping at non-code addresses
        info.vfunc_count = ReadVfuncs(vtable, base, modEnd, info.vfuncs, max_vfuncs);
        return info.vfunc_count > 0;
    }

    return false;
}

bool FindVtableFromRTTI(void* module, std::string_view class_name,
                        VtableInfo& info, int max_vfuncs) {
    // Build mangled RTTI name: ".?AV<class_name>@@"
    std::string mangled = ".?AV";
    mangled += class_name;
    mangled += "@@";

    void* td = FindRTTIDescriptor(module, mangled);
    if (!td) return false;

    return FindVtableFromTypeDescriptor(module, td, info, max_vfuncs);
}

} // namespace cameraunlock::memory
