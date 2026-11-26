#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace cameraunlock::memory {

// Get module base address and size
// Returns false if module is null or info cannot be retrieved
bool GetModuleRange(void* module, uintptr_t& base, size_t& size);

// Advance `cursor` to the next run of committed, readable memory inside
// [cursor, end) and report it in runBase/runSize. Returns false once the range
// is exhausted. Adjacent readable regions are merged into one run.
//
// Anything scanning a module image must iterate through this rather than
// walking MODULEINFO::SizeOfImage directly: packed titles (Denuvo, VMProtect)
// map sections PAGE_NOACCESS until first execution, and a raw dereference there
// closes the game to desktop. Callers must still guard the reads themselves -
// a protection change can race the scan.
bool NextReadableRange(uintptr_t& cursor, uintptr_t end, uintptr_t& runBase, size_t& runSize);

// Scan for byte pattern in module
// Pattern uses ?? for wildcards, e.g., "48 8B 05 ?? ?? ?? ??"
// Returns nullptr if pattern not found
void* ScanPattern(void* module, std::string_view pattern);

// Scan for byte pattern with explicit mask
// Pattern is raw bytes, mask uses 'x' for match and '?' for wildcard
// Length is the size of the pattern
// Returns nullptr if pattern not found
void* ScanPatternMask(void* module, const uint8_t* pattern, const char* mask, size_t length);

// Scan for byte pattern in a specific memory range
// Returns nullptr if pattern not found
void* ScanPatternInRange(uintptr_t base, size_t size, std::string_view pattern);

// Scan for byte pattern with explicit mask in a specific memory range
void* ScanPatternMaskInRange(uintptr_t base, size_t size, const uint8_t* pattern, const char* mask, size_t length);

// Resolve RIP-relative address from an instruction
// instruction: pointer to the start of the instruction containing the RIP-relative offset
// offset_position: byte offset within instruction where the 32-bit displacement starts
// instruction_length: total length of the instruction (displacement is relative to instruction end)
// Returns the resolved absolute address
void* ResolveRIPRelative(void* instruction, int offset_position, int instruction_length);

// Scan for RTTI class name and return pointer to complete object locator
// Useful for finding class instances via their type info
// class_name should be the mangled name, e.g., ".?AVGuiCrosshairData@@"
void* FindRTTIDescriptor(void* module, std::string_view class_name);

} // namespace cameraunlock::memory
