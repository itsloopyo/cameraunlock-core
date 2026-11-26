#include <cameraunlock/memory/pattern_scanner.h>

#include <vector>
#include <cctype>
#include <cstring>

#ifdef _WIN32
#include <Psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

namespace cameraunlock::memory {

namespace {

// Parse hex string pattern into bytes and mask
// Pattern format: "48 8B 05 ?? ?? ?? ??" where ?? is wildcard
// Using char for mask because std::vector<bool> is a special case that doesn't have .data()
bool ParsePattern(std::string_view pattern, std::vector<uint8_t>& bytes, std::vector<char>& mask) {
    bytes.clear();
    mask.clear();

    size_t i = 0;
    while (i < pattern.size()) {
        // Skip whitespace
        while (i < pattern.size() && std::isspace(static_cast<unsigned char>(pattern[i]))) {
            ++i;
        }
        if (i >= pattern.size()) break;

        // Check for wildcard
        if (pattern[i] == '?') {
            bytes.push_back(0);
            mask.push_back('?');  // wildcard
            ++i;
            // Skip second ? if present (for ??)
            if (i < pattern.size() && pattern[i] == '?') {
                ++i;
            }
        } else {
            // Parse hex byte
            if (i + 1 >= pattern.size()) return false;

            char hex[3] = { pattern[i], pattern[i + 1], 0 };
            char* end = nullptr;
            long value = std::strtol(hex, &end, 16);
            if (end != hex + 2) return false;

            bytes.push_back(static_cast<uint8_t>(value));
            mask.push_back('x');  // match
            i += 2;
        }
    }

    return !bytes.empty();
}

// Match pattern at a specific address
bool MatchPattern(const uint8_t* data, const uint8_t* pattern, const char* mask, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (mask[i] == 'x' && data[i] != pattern[i]) {
            return false;
        }
    }
    return true;
}

#ifdef _WIN32

bool IsReadableProtect(DWORD protect) {
    if (protect & PAGE_GUARD) return false;
    switch (protect & 0xFFu) {
        case PAGE_READONLY:
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return true;
        default:
            return false;
    }
}

#endif // _WIN32

// The scan bodies live in their own functions: MSVC rejects __try in a function
// that needs C++ object unwinding, and a protection change racing the scan can
// still fault a page that VirtualQuery reported as readable.
void* ScanRunMask(const uint8_t* start, size_t lastIndex,
                  const uint8_t* pattern, const char* mask, size_t length) {
#ifdef _WIN32
    __try {
#endif
        for (size_t i = 0; i <= lastIndex; ++i) {
            if (MatchPattern(start + i, pattern, mask, length)) {
                return const_cast<uint8_t*>(start + i);
            }
        }
#ifdef _WIN32
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
#endif
    return nullptr;
}

const uint8_t* ScanRunBytes(const uint8_t* start, size_t lastIndex,
                            const uint8_t* needle, size_t length) {
#ifdef _WIN32
    __try {
#endif
        for (size_t i = 0; i <= lastIndex; ++i) {
            if (std::memcmp(start + i, needle, length) == 0) {
                return start + i;
            }
        }
#ifdef _WIN32
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
#endif
    return nullptr;
}

} // anonymous namespace

bool NextReadableRange(uintptr_t& cursor, uintptr_t end, uintptr_t& runBase, size_t& runSize) {
#ifdef _WIN32
    uintptr_t runStart = 0;
    uintptr_t runEnd = 0;

    while (cursor < end) {
        MEMORY_BASIC_INFORMATION mbi = {};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi)) != sizeof(mbi)) break;

        const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (regionEnd <= cursor) break;

        const uintptr_t chunkStart = cursor;
        const uintptr_t chunkEnd = regionEnd < end ? regionEnd : end;
        cursor = regionEnd;

        // Chunks are contiguous by construction, so an adjacent readable region
        // just extends the run: a pattern straddling a section boundary still
        // matches.
        if (mbi.State == MEM_COMMIT && IsReadableProtect(mbi.Protect)) {
            if (runEnd == 0) runStart = chunkStart;
            runEnd = chunkEnd;
        } else if (runEnd != 0) {
            break;
        }
    }

    if (runEnd == 0) return false;
    runBase = runStart;
    runSize = runEnd - runStart;
    return true;
#else
    if (cursor >= end) return false;
    runBase = cursor;
    runSize = end - cursor;
    cursor = end;
    return true;
#endif
}

bool GetModuleRange(void* module, uintptr_t& base, size_t& size) {
    if (!module) return false;

#ifdef _WIN32
    MODULEINFO modInfo = {};
    if (!GetModuleInformation(GetCurrentProcess(), static_cast<HMODULE>(module), &modInfo, sizeof(modInfo))) {
        return false;
    }
    base = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
    size = modInfo.SizeOfImage;
    return true;
#else
    // Non-Windows platforms would need different implementation
    (void)base;
    (void)size;
    return false;
#endif
}

void* ScanPatternInRange(uintptr_t base, size_t size, std::string_view pattern) {
    std::vector<uint8_t> patternBytes;
    std::vector<char> patternMask;

    if (!ParsePattern(pattern, patternBytes, patternMask)) {
        return nullptr;
    }

    return ScanPatternMaskInRange(base, size, patternBytes.data(), patternMask.data(), patternBytes.size());
}

void* ScanPatternMaskInRange(uintptr_t base, size_t size, const uint8_t* pattern, const char* mask, size_t length) {
    if (length == 0 || length > size) {
        return nullptr;
    }

    uintptr_t cursor = base;
    const uintptr_t end = base + size;
    uintptr_t runBase = 0;
    size_t runSize = 0;
    while (NextReadableRange(cursor, end, runBase, runSize)) {
        if (runSize < length) continue;
        void* hit = ScanRunMask(reinterpret_cast<const uint8_t*>(runBase), runSize - length,
                                pattern, mask, length);
        if (hit) return hit;
    }
    return nullptr;
}

void* ScanPattern(void* module, std::string_view pattern) {
    uintptr_t base = 0;
    size_t size = 0;

    if (!GetModuleRange(module, base, size)) {
        return nullptr;
    }

    return ScanPatternInRange(base, size, pattern);
}

void* ScanPatternMask(void* module, const uint8_t* pattern, const char* mask, size_t length) {
    uintptr_t base = 0;
    size_t size = 0;

    if (!GetModuleRange(module, base, size)) {
        return nullptr;
    }

    return ScanPatternMaskInRange(base, size, pattern, mask, length);
}

void* ResolveRIPRelative(void* instruction, int offset_position, int instruction_length) {
    if (!instruction) return nullptr;

    auto* inst = static_cast<uint8_t*>(instruction);
    int32_t displacement = 0;
    std::memcpy(&displacement, inst + offset_position, sizeof(int32_t));

    // RIP-relative addressing: target = instruction_end + displacement
    return inst + instruction_length + displacement;
}

void* FindRTTIDescriptor(void* module, std::string_view class_name) {
    uintptr_t base = 0;
    size_t size = 0;

    if (!GetModuleRange(module, base, size)) {
        return nullptr;
    }

    // Empty / over-long names cannot match. Guarding here also stops the
    // unsigned `size - class_name.size()` below from underflowing into a
    // huge value and walking past the end of the module image.
    if (class_name.empty() || class_name.size() > size) {
        return nullptr;
    }

    // Search for the class name string in the module
    // RTTI type descriptor starts with vtable pointer followed by spare data, then name
    // The structure layout is:
    // - vtable pointer (8 bytes on x64)
    // - spare data pointer (8 bytes on x64)
    // - name string (variable length, null terminated)
    const size_t type_info_offset = sizeof(void*) * 2;  // 16 bytes on x64
    const auto* needle = reinterpret_cast<const uint8_t*>(class_name.data());

    uintptr_t cursor = base;
    const uintptr_t end = base + size;
    uintptr_t runBase = 0;
    size_t runSize = 0;
    while (NextReadableRange(cursor, end, runBase, runSize)) {
        if (runSize < class_name.size()) continue;
        const uint8_t* hit = ScanRunBytes(reinterpret_cast<const uint8_t*>(runBase),
                                          runSize - class_name.size(), needle, class_name.size());
        if (!hit) continue;
        // A hit inside the PE header cannot be a type_info name; stepping back
        // from it would leave the module.
        if (reinterpret_cast<uintptr_t>(hit) < base + type_info_offset) continue;
        return const_cast<uint8_t*>(hit - type_info_offset);
    }
    return nullptr;
}

} // namespace cameraunlock::memory
