#pragma once

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>

#include <cameraunlock/unreal/ue_math.h>

// Runtime access to a live UE5 process: fault-guarded memory reads/writes,
// the loaded module's address range, and FName/UObject reflection that walks
// GUObjectArray to resolve class and object names.
//
// RVAs and struct offsets vary per game build, so the consumer supplies a
// UObjectGlobalsLayout (typically from its per-build offset profile) via
// SetRuntime() before any reflection call. Until then, every reflection
// function returns empty/false.
namespace cameraunlock::unreal {

// Where the UObject/FName globals live in the game module and how their
// structs are laid out. Field meanings:
//   kObjObjects        FUObjectArray.ObjObjects (FChunkedFixedUObjectArray) RVA
//   kObjObjects_Num    NumElements offset within ObjObjects
//   kFUObjectItemSize  sizeof(FUObjectItem)
//   kChunkNumElems     objects per chunk
//   kFNamePool         FNamePool RVA
//   kFNamePoolBlocks   Blocks[] offset within the pool
//   kClassPrivate      UObject::ClassPrivate offset
//   kNamePrivate       UObject::NamePrivate offset
//   kOuterPrivate      UObject::OuterPrivate offset
struct UObjectGlobalsLayout {
    std::uintptr_t kObjObjects;
    std::size_t    kObjObjects_Num;
    std::size_t    kFUObjectItemSize;
    std::size_t    kChunkNumElems;
    std::uintptr_t kFNamePool;
    std::size_t    kFNamePoolBlocks;
    std::size_t    kClassPrivate;
    std::size_t    kNamePrivate;
    std::size_t    kOuterPrivate;
};

// Set once (e.g. from the hook-install path) with the game module's base/end
// and the active build profile's UObject globals layout.
void SetRuntime(std::uintptr_t moduleBase, std::uintptr_t moduleEnd,
                const UObjectGlobalsLayout& layout);
std::uintptr_t ModuleBase();
std::uintptr_t ModuleEnd();
const UObjectGlobalsLayout& Layout();

// Fault-guarded reads/writes (SEH __try). Return false on access violation.
bool SafeReadPtr(std::uintptr_t addr, std::uintptr_t& out);
bool SafeReadU32(std::uintptr_t addr, std::uint32_t& out);
bool SafeReadU16(std::uintptr_t addr, std::uint16_t& out);
bool SafeReadFloat(std::uintptr_t addr, float& out);
bool SafeReadFQuat(std::uintptr_t addr, FQuat4d& out);
bool SafeReadFVector(std::uintptr_t addr, FVector& out);
bool SafeWriteFloat(std::uintptr_t addr, float v);
bool SafeWriteFQuat(std::uintptr_t addr, const FQuat4d& q);
bool SafeWriteFVector(std::uintptr_t addr, const FVector& v);

// Heap or .data in the user-mode 64-bit range, 8-byte aligned.
bool LooksLikePointer(std::uintptr_t v);

// Resolve an FName ComparisonIndex to its string via the FNamePool.
std::string ResolveFName(std::uint32_t id);
std::string ObjectName(std::uintptr_t obj);
std::string ClassName(std::uintptr_t obj);
std::string OuterName(std::uintptr_t obj);

// Case-insensitive substring test. Folds case in place rather than
// allocating lowercased copies - this runs once per UObject across
// full-table enumerations. Semantics match
// lowercase(hay).find(lowercase(needle)) (empty needle -> true).
inline bool ContainsCI(const std::string& hay, const char* needle) {
    const std::size_t nlen = std::strlen(needle);
    if (nlen == 0) return true;
    if (hay.size() < nlen) return false;
    auto lc = [](char c) {
        return static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    };
    const std::size_t last = hay.size() - nlen;
    for (std::size_t i = 0; i <= last; ++i) {
        std::size_t j = 0;
        for (; j < nlen; ++j) {
            if (lc(hay[i + j]) != lc(needle[j])) break;
        }
        if (j == nlen) return true;
    }
    return false;
}

// Visit every live UObject. visit(obj) returns true to stop early.
template <typename Fn>
void ForEachUObject(Fn&& visit) {
    if (ModuleBase() == 0) return;
    const UObjectGlobalsLayout& off = Layout();
    const std::uintptr_t objArr = ModuleBase() + off.kObjObjects;
    std::uintptr_t chunks = 0;
    std::uint32_t num = 0;
    if (!SafeReadPtr(objArr, chunks) || !chunks) return;
    if (!SafeReadU32(objArr + off.kObjObjects_Num, num)) return;
    if (num == 0 || num > 0x4000000) return;
    for (std::uint32_t i = 0; i < num; ++i) {
        std::uintptr_t chunk = 0;
        if (!SafeReadPtr(chunks + (static_cast<std::uintptr_t>(
                i / off.kChunkNumElems) * 8), chunk) || !chunk)
            continue;
        const std::uintptr_t item = chunk +
            static_cast<std::uintptr_t>(i % off.kChunkNumElems)
                * off.kFUObjectItemSize;
        std::uintptr_t obj = 0;
        if (!SafeReadPtr(item, obj) || !obj) continue;
        if (visit(obj)) return;
    }
}

// First object whose name (and optional class/outer) match.
std::uintptr_t FindLiveObject(const char* wantClass, const char* wantName,
                              const char* wantOuter);

}  // namespace cameraunlock::unreal
