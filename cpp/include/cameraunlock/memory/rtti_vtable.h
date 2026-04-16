#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

namespace cameraunlock::memory {

// Maximum vtable entries to read
constexpr int kMaxVfuncEntries = 32;

struct VtableInfo {
    uintptr_t vtable_address;              // address of vfunc[0]
    uintptr_t vfuncs[kMaxVfuncEntries];    // absolute function addresses
    int vfunc_count;                       // valid entries (stopped at non-code address)
    uintptr_t col_address;                 // CompleteObjectLocator address
};

// Find vtable from RTTI class name.
// module: game module handle (e.g., GetModuleHandleA(nullptr))
// class_name: undecorated name (e.g., "CCustomCamera") — will be searched as substring
// info: populated on success
// max_vfuncs: how many vtable entries to read (capped at kMaxVfuncEntries)
// Returns true if vtable found.
bool FindVtableFromRTTI(void* module, std::string_view class_name,
                        VtableInfo& info, int max_vfuncs = 8);

// Same but takes an already-found TypeDescriptor pointer
// (from FindRTTIDescriptor) to skip redundant scanning.
bool FindVtableFromTypeDescriptor(void* module, void* type_descriptor,
                                  VtableInfo& info, int max_vfuncs = 8);

} // namespace cameraunlock::memory
