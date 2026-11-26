// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <cameraunlock/memory/memory_view.h>
#include <cameraunlock/unreal/ue_names.h>

// GUObjectArray and the UObjectBase header, found by what they hold.
//
// InternalIndex is the object's own slot in the array, at +0x0C in every UE4
// and UE5 build. So the array is the structure in writable data whose items
// point at objects that state their own index - which also pins the item size,
// since a wrong stride breaks the sequence at the first step. The rest of the
// header (ClassPrivate, NamePrivate, ObjectFlags) and the flag bits that mark a
// template are then read off the objects in the array (FindObjectHeader).
namespace cameraunlock::unreal {

inline constexpr std::size_t kChunkElements = 65536;

struct ObjectArray {
    bool chunked = true;          // FChunkedFixedUObjectArray (4.20 on) or FFixedUObjectArray
    std::uintptr_t objects = 0;   // address of the ObjObjects member
    std::size_t itemSize = 0x18;  // sizeof(FUObjectItem), or 8 where the items are the objects' pointers
    std::size_t itemObject = 0;   // the object pointer within an item: 8 from UE 5.7, which packs flags ahead of it
    std::size_t numOffset = 0x14; // NumElements, relative to `objects`
    // Early UE4 (4.9) keeps the chunk table itself in ObjObjects, not a pointer to it.
    bool inlineChunks = false;
    std::size_t chunkElements = kChunkElements;
};

inline constexpr std::size_t kUObjectIndex = 0x0C;

// Where UObjectBase keeps ClassPrivate, NamePrivate and ObjectFlags, and the
// EObjectFlags bits a class default object (RF_ClassDefaultObject) and an
// archetype (RF_ArchetypeObject) carry.
struct ObjectHeader {
    std::size_t cls = 0, name = 0, flags = 0;
    std::uint32_t classDefault = 0, archetype = 0;
};

std::optional<ObjectArray> FindObjectArray(const memory::Memory& mem, const memory::ModuleMap& module);

std::int32_t ObjectCount(const memory::Memory& mem, const ObjectArray& array);

// The address of slot `index`'s item, or 0 where the chunk table does not read.
std::uintptr_t ItemAt(const memory::Memory& mem, const ObjectArray& array, std::int32_t index);

// The object in slot `index`, or 0 for a free slot or a read that fails.
std::uintptr_t ObjectAt(const memory::Memory& mem, const ObjectArray& array, std::int32_t index);

// Names and the class/outer chain of one object.
struct Objects {
    const memory::Memory* mem = nullptr;
    NameTable names;
    ObjectArray array;
    ObjectHeader header;

    std::string Name(std::uintptr_t obj) const {
        return obj ? FNameToString(*mem, names, obj + header.name) : std::string{};
    }
    std::uintptr_t Class(std::uintptr_t obj) const { return obj ? mem->Ptr(obj + header.cls) : 0; }
    std::uint32_t Flags(std::uintptr_t obj) const { return mem->Or0<std::uint32_t>(obj + header.flags); }
    bool IsClassDefault(std::uintptr_t obj) const { return (Flags(obj) & header.classDefault) != 0; }
    // A class default object or an archetype: a template, not a live object.
    bool IsTemplate(std::uintptr_t obj) const { return (Flags(obj) & (header.classDefault | header.archetype)) != 0; }
    // OuterPrivate follows the 8-aligned FName: 0x20, or 0x28 with case-preserving names.
    std::size_t OuterOffset() const { return header.name + ((FNameSize(names) + 7) & ~std::size_t{7}); }
    std::uintptr_t Outer(std::uintptr_t obj) const { return obj ? mem->Ptr(obj + OuterOffset()) : 0; }
    std::string ClassName(std::uintptr_t obj) const { return Name(Class(obj)); }

    // "/Script/Engine.Actor": the outer chain joined the way the engine prints
    // a path, '.' after the package and ':' below a non-package outer.
    std::string PathName(std::uintptr_t obj) const;

    template <class Fn>
    void ForEach(Fn&& fn) const {
        const std::int32_t n = ObjectCount(*mem, array);
        for (std::int32_t i = 0; i < n; ++i) {
            const std::uintptr_t obj = ObjectAt(*mem, array, i);
            if (obj && fn(obj)) return;
        }
    }

    // The objects at full path names ("/Script/CoreUObject.Object",
    // "/Script/Engine.Controller:GetPlayerViewPoint"), 0 for any not found. One
    // pass over the array for the lot.
    std::vector<std::uintptr_t> FindPaths(const std::vector<std::string>& paths) const;
    std::uintptr_t FindPath(const std::string& path) const { return FindPaths({path})[0]; }
};

// The header of `objects` (whose own header is not read): ClassPrivate and
// NamePrivate are where every object's class's class is one object, itself,
// named "Class"; ObjectFlags and RF_ClassDefaultObject are the dword and the
// bit set on every object named "Default__..." and on no other, and
// RF_ArchetypeObject the other bit every class default carries and no native
// class does. Nothing, with `why`, unless each is found once; `notLoaded` is
// set while the engine has made too few class defaults to tell.
std::optional<ObjectHeader> FindObjectHeader(const Objects& objects, std::string& why, bool& notLoaded);

}  // namespace cameraunlock::unreal
