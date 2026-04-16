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

bool FindVtableFromTypeDescriptor(void* module, void* type_descriptor,
                                  VtableInfo& info, int max_vfuncs) {
    if (!module || !type_descriptor) return false;
    if (max_vfuncs > kMaxVfuncEntries) max_vfuncs = kMaxVfuncEntries;

    uintptr_t base = 0;
    size_t modSize = 0;
    if (!GetModuleRange(module, base, modSize)) return false;

    uintptr_t tdAddr = reinterpret_cast<uintptr_t>(type_descriptor);
    uint32_t tdRva = static_cast<uint32_t>(tdAddr - base);

    // Scan the module for a COL whose pTypeDescriptor == tdRva
    const uint8_t* start = reinterpret_cast<const uint8_t*>(base);
    const RTTICompleteObjectLocator* foundCol = nullptr;

    for (size_t i = 0; i + sizeof(RTTICompleteObjectLocator) <= modSize; i += 4) {
        auto* col = reinterpret_cast<const RTTICompleteObjectLocator*>(start + i);

        if (col->signature != 1) continue;  // x64 signature
        if (col->pTypeDescriptor != tdRva) continue;

        // Validate self-reference
        uint32_t colRva = static_cast<uint32_t>(i);
        if (col->pSelf != colRva) continue;

        foundCol = col;
        break;
    }

    if (!foundCol) return false;

    uintptr_t colAddr = reinterpret_cast<uintptr_t>(foundCol);
    info.col_address = colAddr;

    // Now find the vtable: scan for a pointer to this COL.
    // vtable[-1] == &COL, so vtable == (&COL_pointer) + 8
    for (size_t i = 0; i + sizeof(uintptr_t) <= modSize; i += 8) {
        uintptr_t val = *reinterpret_cast<const uintptr_t*>(start + i);
        if (val != colAddr) continue;

        // This is vtable[-1]. vtable[0] starts at the next slot.
        uintptr_t vtable = reinterpret_cast<uintptr_t>(start + i + sizeof(uintptr_t));
        info.vtable_address = vtable;

        // Read vfunc entries, stopping at non-code addresses
        uintptr_t codeStart = base;
        uintptr_t codeEnd = base + modSize;
        info.vfunc_count = 0;

        for (int v = 0; v < max_vfuncs; v++) {
            uintptr_t funcAddr = *reinterpret_cast<const uintptr_t*>(vtable + v * sizeof(uintptr_t));
            // Valid code address: within the module
            if (funcAddr < codeStart || funcAddr >= codeEnd) break;
            info.vfuncs[v] = funcAddr;
            info.vfunc_count = v + 1;
        }

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
