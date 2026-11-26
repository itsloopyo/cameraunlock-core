// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/memory/msvc_rtti.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>

namespace cameraunlock::memory::pe {
namespace {

std::uint32_t NameOffset(const Image& image) { return image.Is64() ? 0x10 : 8; }

bool Decorated(const char* name) { return std::strncmp(name, ".?AV", 4) == 0 || std::strncmp(name, ".?AU", 4) == 0; }

// The NUL-terminated name at `rva`, at most 512 bytes and never past the image.
std::string NameAt(const Image& image, std::uint32_t rva) {
    if (rva >= image.SizeOfImage()) return {};
    const auto* p = reinterpret_cast<const char*>(image.At(rva));
    const std::size_t room = (std::min<std::size_t>)(512, image.SizeOfImage() - rva);
    const auto* nul = static_cast<const char*>(std::memchr(p, 0, room));
    return std::string(p, nul ? static_cast<std::size_t>(nul - p) : room);
}

// The RVA a reference in the RTTI names: an absolute pointer in x86, an RVA in
// x64. Nothing when it points outside the image.
std::optional<std::uint32_t> RefAt(const Image& image, std::uint32_t at) {
    if (!image.Is64()) return image.RvaOf(image.U32(at));
    const std::uint32_t rva = image.U32(at);
    if (rva >= image.SizeOfImage()) return std::nullopt;
    return rva;
}

}  // namespace

std::uint32_t Rtti::Ref(std::uint32_t at) const {
    const std::optional<std::uint32_t> rva = RefAt(image_, at);
    if (!rva) throw std::runtime_error("the RTTI at RVA " + std::to_string(at) + " points outside the image");
    return *rva;
}

std::string Rtti::NameOfDescriptor(std::uint32_t td) const { return NameAt(image_, td + NameOffset(image_)); }

Rtti::Rtti(const Image& image) : image_(image) {
    const Section& r = image.Rdata();
    const Section& d = image.Data();
    const bool x64 = image.Is64();
    const std::uint32_t nameOffset = NameOffset(image);
    const std::uint32_t locatorSize = x64 ? 24 : 20;
    for (std::uint32_t at = r.rva; at + locatorSize <= r.rva + r.size; at += 4) {
        std::optional<std::uint32_t> td;
        if (x64) {
            if (image.U32(at) != 1 || image.U32(at + 20) != at) continue;
            td = image.U32(at + 12);
        } else {
            if (image.U32(at) != 0) continue;
            td = image.RvaOf(image.U32(at + 12));
            const std::optional<std::uint32_t> chd = image.RvaOf(image.U32(at + 16));
            if (!td || !chd || !r.Contains(*chd, 16)) continue;
        }
        if (!d.Contains(std::uint64_t{*td} + nameOffset, 5)) continue;
        if (!Decorated(reinterpret_cast<const char*>(image.At(*td + nameOffset)))) continue;
        descriptors_.try_emplace(NameOfDescriptor(*td), *td);
        locators_[*td].push_back(at);
    }
    std::set<std::uint32_t> all;
    for (const auto& [td, list] : locators_) all.insert(list.begin(), list.end());
    const std::uint32_t w = image.PointerSize();
    for (std::uint32_t at = r.rva; at + 2 * w <= r.rva + r.size; at += w) {
        const std::optional<std::uint32_t> col = image.RvaOf(image.Pointer(at));
        if (!col || !all.count(*col)) continue;
        const std::optional<std::uint32_t> first = image.RvaOf(image.Pointer(at + w));
        if (!first || !image.Text().Contains(*first)) continue;
        vtableOfLocator_.emplace(*col, at + w);
        locatorOfVtable_.emplace(at + w, *col);
    }
}

std::vector<Rtti::Vtable> Rtti::Vtables(const std::string& decorated) const {
    std::vector<Vtable> out;
    const auto td = descriptors_.find(decorated);
    if (td == descriptors_.end()) return out;
    const auto cols = locators_.find(td->second);
    if (cols == locators_.end()) return out;
    for (const std::uint32_t col : cols->second) {
        const auto vt = vtableOfLocator_.find(col);
        if (vt != vtableOfLocator_.end()) out.push_back({vt->second, image_.U32(col + 4)});
    }
    std::sort(out.begin(), out.end(), [](const Vtable& a, const Vtable& b) { return a.offset < b.offset; });
    return out;
}

std::uint32_t Rtti::VtableAt(const std::string& decorated, std::uint32_t offset) const {
    std::optional<std::uint32_t> found;
    for (const Vtable& v : Vtables(decorated)) {
        if (v.offset != offset) continue;
        if (found) throw std::runtime_error(decorated + " has two vtables for its subobject at +" + std::to_string(offset));
        found = v.rva;
    }
    if (!found) throw std::runtime_error(decorated + " has no vtable for a subobject at +" + std::to_string(offset));
    return *found;
}

std::vector<Rtti::Base> Rtti::Bases(const std::string& decorated) const {
    std::vector<Base> out;
    const auto td = descriptors_.find(decorated);
    if (td == descriptors_.end()) return out;
    const auto cols = locators_.find(td->second);
    if (cols == locators_.end()) return out;
    // The class hierarchy descriptor: signature, attributes, the count of base
    // classes and the array of them. Each base class descriptor: its type
    // descriptor, the count of bases it contains, then where it sits (mdisp,
    // pdisp, vdisp).
    const Section& r = image_.Rdata();
    const std::uint32_t chd = Ref(cols->second.front() + 16);
    if (!r.Contains(chd, 16)) throw std::runtime_error(decorated + "'s class hierarchy descriptor is not in .rdata");
    const std::uint32_t count = image_.U32(chd + 8);
    const std::uint32_t array = Ref(chd + 12);
    if (!r.Contains(array, std::uint64_t{count} * 4)) throw std::runtime_error(decorated + "'s base class array is not in .rdata");
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t bcd = Ref(array + 4 * i);
        if (!r.Contains(bcd, 24)) throw std::runtime_error(decorated + "'s base class descriptor " + std::to_string(i) + " is not in .rdata");
        out.push_back({NameOfDescriptor(Ref(bcd)), image_.I32(bcd + 8), image_.I32(bcd + 12), image_.I32(bcd + 16)});
    }
    return out;
}

std::uint32_t Rtti::BaseAt(const std::string& decorated, const std::string& base) const {
    if (!descriptors_.count(decorated) || !descriptors_.count(base)) throw std::runtime_error(decorated + " or " + base + " has no RTTI");
    std::optional<std::uint32_t> found;
    for (const Base& b : Bases(decorated)) {
        if (b.name != base) continue;
        if (b.pdisp != -1) throw std::runtime_error(base + " is a virtual base of " + decorated);
        if (found) throw std::runtime_error(base + " is a base of " + decorated + " twice");
        found = static_cast<std::uint32_t>(b.mdisp);
    }
    if (!found) throw std::runtime_error(base + " is not a base of " + decorated);
    return *found;
}

std::vector<std::uint32_t> Rtti::PrimaryVtablesFrom(const std::string& base) const {
    std::vector<std::uint32_t> out;
    for (const auto& [name, descriptor] : descriptors_) {
        const std::vector<Base> bases = Bases(name);
        if (std::none_of(bases.begin(), bases.end(), [&](const Base& b) { return b.name == base && b.mdisp == 0; })) continue;
        for (const Vtable& v : Vtables(name))
            if (v.offset == 0) out.push_back(v.rva);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::optional<std::uint32_t> Rtti::Slot(std::uint32_t vtable, unsigned index) const {
    const std::optional<std::uint32_t> fn = image_.RvaOf(image_.Pointer(vtable + image_.PointerSize() * index));
    if (!fn || !image_.Text().Contains(*fn)) return std::nullopt;
    return fn;
}

std::string Rtti::ClassOf(std::uint32_t vtable) const {
    const auto col = locatorOfVtable_.find(vtable);
    if (col == locatorOfVtable_.end()) return {};
    return NameOfDescriptor(Ref(col->second + 12));
}

std::uint32_t VtableOf(const Image& image, const std::string& decorated) {
    const Rtti rtti(image);
    std::vector<std::uint32_t> found;
    for (const Rtti::Vtable& v : rtti.Vtables(decorated))
        if (v.offset == 0) found.push_back(v.rva);
    if (found.size() != 1)
        throw std::runtime_error("expected one vtable for " + decorated + ", found " + std::to_string(found.size()));
    return found[0];
}

}  // namespace cameraunlock::memory::pe

namespace cameraunlock::memory {

std::vector<std::uintptr_t> RttiVtables(const Memory& mem, const ModuleMap& map, const std::string& decorated) {
    constexpr bool x64 = sizeof(void*) == 8;
    constexpr std::uintptr_t kNameOffset = x64 ? 0x10 : 8;
    constexpr std::uintptr_t w = sizeof(void*);
    // Type descriptors are written by the runtime (their first slot is filled
    // at startup), so they sit in writable data; locators and vtables are
    // constant data.
    std::vector<Range> all = map.writable;
    all.insert(all.end(), map.readonly.begin(), map.readonly.end());
    // An x64 name has a NUL before it: the top byte of the spare pointer the
    // runtime caches the undecorated name in. An x86 one may not, as that cache
    // is a 32-bit heap address once typeid(x).name() has run.
    const std::vector<std::uintptr_t> names =
        x64 ? FindCString(mem, all, decorated) : FindBytes(mem, all, std::string_view(decorated.c_str(), decorated.size() + 1));
    std::vector<std::uintptr_t> out;
    for (const std::uintptr_t text : names) {
        const std::uintptr_t descriptor = text - kNameOffset;
        // x64 locators name the descriptor by RVA, x86 ones by address.
        const auto named = static_cast<std::uint32_t>(x64 ? descriptor - map.base : descriptor);
        for (const std::uintptr_t ref : FindBytes(mem, map.readonly, std::string_view(reinterpret_cast<const char*>(&named), 4))) {
            const std::uintptr_t col = ref - 0x0C;
            if (col % 4 != 0) continue;
            if (mem.Or0<std::uint32_t>(col) != (x64 ? 1u : 0u) || mem.Or0<std::uint32_t>(col + 4) != 0) continue;
            if (x64 && mem.Or0<std::uint32_t>(col + 0x14) != static_cast<std::uint32_t>(col - map.base)) continue;
            for (const std::uintptr_t s : FindBytes(mem, map.readonly, std::string_view(reinterpret_cast<const char*>(&col), w)))
                if (s % w == 0 && map.InCode(mem.Ptr(s + w))) out.push_back(s + w);
        }
    }
    return out;
}

std::string RttiClassOf(const Memory& mem, const ModuleMap& map, std::uintptr_t object) {
    constexpr bool x64 = sizeof(void*) == 8;
    const std::uintptr_t vtable = mem.Ptr(object);
    if (!map.InModule(vtable)) return {};
    const std::uintptr_t col = mem.Ptr(vtable - sizeof(void*));
    if (!map.InModule(col) || mem.Or0<std::uint32_t>(col) != (x64 ? 1u : 0u)) return {};
    const std::uintptr_t named = mem.Or0<std::uint32_t>(col + 0x0C);
    const std::uintptr_t descriptor = x64 ? map.base + named : named;
    std::string name = mem.Ascii(descriptor + (x64 ? 0x10 : 8), 512);
    if (name.size() < 7 || (name.compare(0, 4, ".?AV") != 0 && name.compare(0, 4, ".?AU") != 0)) return {};
    return name;
}

}  // namespace cameraunlock::memory
