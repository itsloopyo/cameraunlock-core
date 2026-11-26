// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Tests for the MSVC RTTI reader (cameraunlock/memory/msvc_rtti.h): on
// hand-made x86 and x64 images, and on classes compiled into this executable,
// read through its image and through Memory.

#include <cameraunlock/memory/msvc_rtti.h>

#include "pe_fixture.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>

#ifdef _WIN32
#include <cameraunlock/memory/process_memory.h>

#include <Windows.h>
#endif

// At global scope, so their decorated names are ".?AV<name>@@".
class RttiKitBase {
public:
    virtual ~RttiKitBase() = default;
    virtual int A() { return 1; }
    int a = 0;
};

class RttiKitOther {
public:
    virtual ~RttiKitOther() = default;
    virtual int B() { return 2; }
    int b = 0;
};

class RttiKitDerived : public RttiKitBase, public RttiKitOther {
public:
    int A() override { return 3; }
    int B() override { return 4; }
};

namespace {

namespace pe = cameraunlock::memory::pe;

int g_failures = 0;

void Check(bool cond, const std::string& name) {
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << name << "\n";
    if (!cond) ++g_failures;
}

template <class Fn>
bool Throws(Fn fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

std::string Bits(bool x64) { return x64 ? " (x64)" : " (x86)"; }

// A reference inside the RTTI: an absolute pointer in x86, an RVA in x64.
void Ref(PeFixture& f, std::size_t at, std::uint32_t rva) {
    if (f.is64) f.Put(at, rva);
    else f.Put(at, static_cast<std::uint32_t>(f.base + rva));
}

std::uint32_t NameOffset(const PeFixture& f) { return f.is64 ? 0x10 : 8; }

// A locator at `col` for the type descriptor `td`, the subobject at `offset`,
// with hierarchy descriptor `chd`.
void Locator(PeFixture& f, std::uint32_t col, std::uint32_t td, std::uint32_t offset, std::uint32_t chd) {
    f.Put(col, std::uint32_t{f.is64 ? 1u : 0u});
    f.Put(col + 4, offset);
    Ref(f, col + 12, td);
    Ref(f, col + 16, chd);
    if (f.is64) f.Put(col + 20, col);
}

// A class whose locator, descriptors and vtable are laid out as MSVC lays them
// out: its vtable is found by name and its slots read as code.
void TheRttiIsReadByName(bool x64) {
    const std::string b = Bits(x64);
    PeFixture f(x64);
    const std::string name = ".?AVCFixtureGame@@";
    f.Text(0x3000 + NameOffset(f), name.c_str());
    Locator(f, 0x2100, 0x3000, 0x30, 0x2200);
    f.Put(0x2208, std::uint32_t{1});  // one base, itself
    Ref(f, 0x220C, 0x2240);
    Ref(f, 0x2240, 0x2260);
    Ref(f, 0x2260, 0x3000);
    f.Put(0x2268, std::int32_t{0x30});
    f.Put(0x226C, std::int32_t{-1});
    const std::uint32_t w = f.is64 ? 8 : 4;
    f.Pointer(0x2300, 0x2100);  // the vtable's locator, then its slots
    f.Pointer(0x2300 + w, 0x1100);
    f.Pointer(0x2300 + 2 * w, 0x1180);
    const pe::Image image = f.Open();
    const pe::Rtti rtti(image);
    const std::uint32_t vtable = 0x2300 + w;
    Check(rtti.Count() == 1, "one class is found" + b);
    Check(rtti.VtableAt(name, 0x30) == vtable, "its vtable is found by subobject offset" + b);
    Check(Throws([&] { rtti.VtableAt(name, 0); }), "a subobject it has no vtable for throws" + b);
    Check(rtti.Vtables(".?AVCFixture@@").empty(), "a name is matched exactly, not as a prefix" + b);
    Check(rtti.Slot(vtable, 1) == 0x1180u && !rtti.Slot(vtable, 2).has_value(), "a slot is read while it is code" + b);
    Check(rtti.ClassOf(vtable) == name && rtti.ClassOf(0x2300).empty(), "ClassOf names a vtable's class" + b);
    Check(rtti.IsVtable(vtable) && !rtti.IsVtable(0x2300), "IsVtable knows a vtable by its locator" + b);
    const auto bases = rtti.Bases(name);
    Check(bases.size() == 1 && bases[0].name == name && bases[0].mdisp == 0x30 && bases[0].pdisp == -1, "its bases are read" + b);
    Check(Throws([&] { pe::VtableOf(image, name); }), "VtableOf throws with no vtable at offset 0" + b);
    f.Put(0x2104, std::uint32_t{0});
    Check(pe::VtableOf(f.Open(), name) == vtable, "VtableOf finds the complete object's vtable" + b);
}

// A class with a base 0xF0 into it, as its class hierarchy descriptor lists
// them: the base is found there, and refused when it is virtual or absent.
void TheBaseIsWhereTheHierarchySaysItSits(bool x64) {
    const std::string b = Bits(x64);
    PeFixture f(x64);
    const std::string derived = ".?AVCFixture@@";
    const std::string base = ".?AVCFixtureBase@N@@";
    f.Text(0x3000 + NameOffset(f), derived.c_str());
    f.Text(0x3100 + NameOffset(f), base.c_str());
    for (const auto& [col, td, chd] : {std::tuple{0x2100u, 0x3000u, 0x2200u}, std::tuple{0x2140u, 0x3100u, 0x2300u}})
        Locator(f, col, td, 0, chd);
    f.Put(0x2208, std::uint32_t{2});
    Ref(f, 0x220C, 0x2240);
    Ref(f, 0x2240, 0x2260);
    Ref(f, 0x2244, 0x2280);
    Ref(f, 0x2260, 0x3000);
    f.Put(0x226C, std::int32_t{-1});
    Ref(f, 0x2280, 0x3100);
    f.Put(0x2288, std::uint32_t{0xF0});
    f.Put(0x228C, std::int32_t{-1});
    {
        const pe::Image image = f.Open();
        const pe::Rtti rtti(image);
        Check(rtti.BaseAt(derived, base) == 0xF0u, "a base is where the hierarchy says" + b);
        Check(Throws([&] { rtti.BaseAt(base, derived); }), "a class that is not a base throws" + b);
        Check(Throws([&] { rtti.BaseAt(derived, ".?AVCMissing@@"); }), "a class with no RTTI throws" + b);
    }
    f.Put(0x228C, std::int32_t{0});
    const pe::Image image = f.Open();
    const pe::Rtti rtti(image);
    Check(Throws([&] { rtti.BaseAt(derived, base); }), "a virtual base throws" + b);
}

void PrimaryVtablesComeFromTheBase(bool x64) {
    const std::string b = Bits(x64);
    PeFixture f(x64);
    const std::uint32_t w = f.is64 ? 8 : 4;
    const std::string base = ".?AVCBase@@";
    // Two classes: the base, and one deriving from it at offset 0.
    f.Text(0x3000 + NameOffset(f), base.c_str());
    f.Text(0x3100 + NameOffset(f), ".?AVCDerived@@");
    Locator(f, 0x2100, 0x3000, 0, 0x2200);
    Locator(f, 0x2140, 0x3100, 0, 0x2300);
    f.Put(0x2208, std::uint32_t{1});
    Ref(f, 0x220C, 0x2240);
    Ref(f, 0x2240, 0x2260);
    Ref(f, 0x2260, 0x3000);
    f.Put(0x2308, std::uint32_t{2});
    Ref(f, 0x230C, 0x2340);
    Ref(f, 0x2340, 0x2360);
    Ref(f, 0x2344, 0x2260);
    Ref(f, 0x2360, 0x3100);
    f.Pointer(0x2400, 0x2100);
    f.Pointer(0x2400 + w, 0x1100);
    f.Pointer(0x2480, 0x2140);
    f.Pointer(0x2480 + w, 0x1200);
    const pe::Image image = f.Open();
    const pe::Rtti rtti(image);
    const auto primaries = rtti.PrimaryVtablesFrom(base);
    Check(primaries.size() == 2 && primaries[0] == 0x2400 + w && primaries[1] == 0x2480 + w,
          "the base's vtable and its derived class's are found" + b);
    Check(rtti.PrimaryVtablesFrom(".?AVCDerived@@").size() == 1, "a class derived from nothing has only its own" + b);
}

#ifdef _WIN32
RttiKitDerived* volatile g_object = nullptr;

void ReadThisExesClasses() {
    g_object = new RttiKitDerived();
    RttiKitDerived* object = g_object;
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto primary = *reinterpret_cast<std::uintptr_t*>(object);
    const auto otherAt = reinterpret_cast<std::uintptr_t>(static_cast<RttiKitOther*>(object)) - reinterpret_cast<std::uintptr_t>(object);
    const auto secondary = *reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(object) + otherAt);
    const std::string name = ".?AVRttiKitDerived@@";

    const pe::Image image(reinterpret_cast<const std::uint8_t*>(base), base);
    const pe::Rtti rtti(image);
    const auto vtables = rtti.Vtables(name);
    Check(vtables.size() == 2 && vtables[0].offset == 0 && vtables[0].rva == primary - base &&
              vtables[1].offset == otherAt && vtables[1].rva == secondary - base,
          "both of the class's vtables are found, each at its subobject's offset");
    Check(pe::VtableOf(image, name) == primary - base, "VtableOf is the object's own vtable");
    Check(rtti.ClassOf(static_cast<std::uint32_t>(primary - base)) == name, "ClassOf names it");
    const auto bases = rtti.Bases(name);
    const auto at = [&](const char* base) {
        const auto b = std::find_if(bases.begin(), bases.end(), [&](const pe::Rtti::Base& x) { return x.name == base; });
        return b == bases.end() ? -1 : b->mdisp;
    };
    Check(bases.size() == 3 && bases[0].name == name && at(".?AVRttiKitBase@@") == 0 &&
              at(".?AVRttiKitOther@@") == static_cast<std::int32_t>(otherAt),
          "its bases are where the compiler put them");
    const auto slot = rtti.Slot(static_cast<std::uint32_t>(primary - base), 1);
    Check(slot && image.Text().Contains(*slot), "its slots are code");

    const cameraunlock::memory::ProcessMemory mem;
    const cameraunlock::memory::LoadedModule module(base);
    const auto live = cameraunlock::memory::RttiVtables(mem, module.Map(), name);
    Check(live.size() == 1 && live[0] == primary, "RttiVtables finds the same vtable through Memory");
    Check(cameraunlock::memory::RttiVtables(mem, module.Map(), ".?AVRttiKitMissing@@").empty(), "a class that is not there has none");
    Check(cameraunlock::memory::RttiClassOf(mem, module.Map(), reinterpret_cast<std::uintptr_t>(object)) == name,
          "RttiClassOf names a live object's class");
    const std::uintptr_t notAnObject = 0;
    Check(cameraunlock::memory::RttiClassOf(mem, module.Map(), reinterpret_cast<std::uintptr_t>(&notAnObject)).empty(),
          "a word that is not a vtable names nothing");
    delete g_object;
    g_object = nullptr;
}

void ThisExesClassesAreRead() {
    std::cout << "RTTI of this executable:\n";
    try {
        ReadThisExesClasses();
    } catch (const std::exception& e) {
        Check(false, std::string("reading this exe's RTTI threw: ") + e.what());
    }
}
#endif

}  // namespace

int RunMsvcRttiTests() {
    g_failures = 0;
    for (const bool x64 : {false, true}) {
        std::cout << "MSVC RTTI" << Bits(x64) << ":\n";
        TheRttiIsReadByName(x64);
        TheBaseIsWhereTheHierarchySaysItSits(x64);
        PrimaryVtablesComeFromTheBase(x64);
    }
#ifdef _WIN32
    ThisExesClassesAreRead();
#endif
    return g_failures;
}
