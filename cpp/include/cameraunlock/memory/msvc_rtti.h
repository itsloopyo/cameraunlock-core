// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <cameraunlock/memory/memory_view.h>
#include <cameraunlock/memory/pe_image.h>

// MSVC run-time type information, read by a class's exact decorated name
// (".?AVCCamera@NGraphicsEngine@@", ".?AUStruct@@"), never a substring.
//
// x86: the type descriptor is {vtable, spare, name} with the name 8 bytes in;
// the complete object locator {signature 0, offset, cd offset, type
// descriptor, hierarchy descriptor} holds absolute pointers.
// x64: the name is 16 bytes in; the locator {signature 1, offset, cd offset,
// type descriptor, hierarchy descriptor, itself} holds RVAs.
// Either way the pointer to a vtable's locator sits just before its first slot.
namespace cameraunlock::memory::pe {

// The RTTI of an image: every class with a locator, each with the vtables of
// its subobjects and its base classes with where each sits in it. Type
// descriptors are looked for in .data and locators and vtables in .rdata; an
// image lacking either throws.
class Rtti {
public:
    explicit Rtti(const Image& image);

    struct Vtable {
        std::uint32_t rva = 0;     // the first slot
        std::uint32_t offset = 0;  // the subobject's place in the class
    };
    struct Base {
        std::string name;        // decorated
        std::int32_t mdisp = 0;  // where it sits in the class
        std::int32_t pdisp = -1; // -1 for a base that is not virtual
        std::int32_t vdisp = 0;
    };

    // The vtables of the class, by subobject offset; empty when it has none.
    std::vector<Vtable> Vtables(const std::string& decorated) const;
    // The vtable of its subobject at `offset`; throws unless it has exactly one there.
    std::uint32_t VtableAt(const std::string& decorated, std::uint32_t offset) const;
    // Its bases from its class hierarchy descriptor, itself first; empty when
    // the class has no locator.
    std::vector<Base> Bases(const std::string& decorated) const;
    // Where the non-virtual base `base` sits in the class; throws when it is
    // not a base once, or is a virtual one.
    std::uint32_t BaseAt(const std::string& decorated, const std::string& base) const;
    // The vtable at offset 0 of every class that has `base` at offset 0 among
    // its bases (itself included), sorted.
    std::vector<std::uint32_t> PrimaryVtablesFrom(const std::string& base) const;
    // Slot `index` of the vtable at `vtable` when it is code in .text, else nothing.
    std::optional<std::uint32_t> Slot(std::uint32_t vtable, unsigned index) const;
    // The decorated name of the class whose vtable starts at `vtable`, "" for none.
    std::string ClassOf(std::uint32_t vtable) const;
    // Whether a complete object locator sits just before `rva`, as before every vtable.
    bool IsVtable(std::uint32_t rva) const { return locatorOfVtable_.count(rva) != 0; }
    // How many classes have a locator.
    std::size_t Count() const { return descriptors_.size(); }

private:
    std::uint32_t Ref(std::uint32_t at) const;
    std::string NameOfDescriptor(std::uint32_t td) const;

    const Image& image_;
    std::map<std::string, std::uint32_t> descriptors_;              // decorated name -> type descriptor
    std::map<std::uint32_t, std::vector<std::uint32_t>> locators_;  // type descriptor -> complete object locators
    std::map<std::uint32_t, std::uint32_t> vtableOfLocator_;
    std::map<std::uint32_t, std::uint32_t> locatorOfVtable_;
};

// The vtable of the class's complete object (offset 0); throws unless there
// is exactly one.
std::uint32_t VtableOf(const Image& image, const std::string& decorated);

}  // namespace cameraunlock::memory::pe

namespace cameraunlock::memory {

// The same RTTI read through Memory from a module of this build's bitness
// (live, guarded, or a test's heap), with no PE image needed: the vtables at
// offset 0 of the class, each found by its type descriptor (in the map's
// writable or read-only ranges), a locator at offset 0 naming it, and the
// pointer to that locator in the read-only ranges followed by a slot that is
// code. Empty when the class has none; more than one is the caller's to judge.
std::vector<std::uintptr_t> RttiVtables(const Memory& mem, const ModuleMap& map, const std::string& decorated);

// The decorated name of the class of the object at `object`, by its vtable's
// locator; empty when its first pointer is not a vtable with RTTI in the module.
std::string RttiClassOf(const Memory& mem, const ModuleMap& map, std::uintptr_t object);

}  // namespace cameraunlock::memory
