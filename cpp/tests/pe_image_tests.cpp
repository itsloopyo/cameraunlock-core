// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Tests for the PE image reader (cameraunlock/memory/pe_image.h) on hand-made
// x86 and x64 images, and on this test executable as it is loaded.

#include <cameraunlock/memory/pe_image.h>

#include "pe_fixture.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

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

void TheImageIsReadAsTheLoaderLaysItOut(bool x64) {
    PeFixture f(x64);
    const pe::Image image = f.Open();
    const std::string b = Bits(x64);
    Check(image.Is64() == x64 && image.PointerSize() == (x64 ? 8u : 4u), "the bitness is read from the headers" + b);
    Check(image.Text().rva == 0x1000 && image.Rdata().rva == 0x2000 && image.Data().rva == 0x3000, "the sections are found by name" + b);
    Check(image.Text().Executable() && !image.Text().Writable() && image.Data().Writable(), "a section's flags are read" + b);
    Check(image.SectionNamed(".tls") == nullptr && image.SectionNamed(".pdata") != nullptr, "SectionNamed is null for a missing section" + b);
    Check(image.HeaderImageBase() == f.base && image.PointerBase() == f.base, "the header's ImageBase is read" + b);
    Check(image.RvaOf(f.base + 0x1234) == 0x1234u, "RvaOf turns a pointer into an RVA" + b);
    Check(!image.RvaOf(f.base - 4).has_value() && !image.RvaOf(f.base + 0x5000).has_value(), "RvaOf is nothing outside the image" + b);
    Check(image.PointerOf(0x1234) == f.base + 0x1234, "PointerOf is the reverse" + b);
    Check(Throws([&] { image.U32(0x4FFE); }) && Throws([&] { image.U64(0x4FFC); }), "a read past the image throws" + b);
    f.Pointer(0x2010, 0x1100);
    Check(f.Open().Pointer(0x2010) == f.base + 0x1100, "Pointer reads the image's own width" + b);

    f.Code(0x1100, {0xE9, 0xFB, 0x00, 0x00, 0x00});  // jmp 0x1200
    f.Code(0x1200, {0xE9, 0xFB, 0x00, 0x00, 0x00});  // jmp 0x1300
    Check(pe::Through(f.Open(), 0x1100) == 0x1300u, "Through follows a chain of jmp thunks" + b);
    f.Code(0x1300, {0xE9});  // jmp 0x10000000, past the image: another tool's hook
    f.Put(0x1301, static_cast<std::int32_t>(0x10000000 - 0x1305));
    Check(pe::Through(f.Open(), 0x1100) == 0x10000000u, "Through stops at a jmp out of the image and returns its target" + b);
    f.Code(0x1300, {0xCC, 0xCC, 0xCC, 0xCC, 0xCC});
    f.Code(0x4FFD, {0xE9, 0x00, 0x00});
    Check(pe::Through(f.Open(), 0x4FFD) == 0x4FFDu, "a jmp cut off by the image's end is not followed" + b);
    f.Code(0x4FFD, {0x00, 0x00, 0x00});
    Check(pe::Find(image, 0x1000, 0x1000, pe::Pattern("CC CC")) == 0x1000u, "Find finds the first match" + b);
    Check(pe::FindLast(image, 0x1000, 0x10, pe::Pattern("CC CC")) == 0x100Eu, "FindLast finds the last match in the span" + b);
    Check(pe::FindAll(image, 0x1000, 0x10, pe::Pattern("CC ?? CC")).size() == 14, "?? matches any byte" + b);
    Check(!pe::Find(image, 0x4FF0, 0x100, pe::Pattern("CC")).has_value(), "a span past the image is cut at its end" + b);

    std::vector<std::uint8_t> bad(f.data);
    bad[0] = 0;
    Check(Throws([&] { pe::Image(bad.data(), bad.size(), f.base); }), "a missing MZ header throws" + b);
    Check(Throws([&] { pe::Image(f.data.data(), 0x4000, f.base); }), "a span shorter than SizeOfImage throws" + b);
    {
        // Headers alone claiming a large image with directories past the
        // buffer: refused before any directory is read.
        PeFixture big(x64);
        big.Put(PeFixture::kOpt + 56, std::uint32_t{0x100000});
        big.Directory(1, 0x1000, 0x40);
        big.Directory(3, 0x1000, 0x24000);
        const std::vector<std::uint8_t> headers(big.data.begin(), big.data.begin() + 0x400);
        Check(Throws([&] { pe::Image(headers.data(), headers.size(), big.base); }),
              "headers claiming more than the span throw before their directories are read" + b);
    }
    std::vector<std::uint8_t> arm(f.data);
    arm[0x84] = 0x64;
    arm[0x85] = 0xAA;
    Check(Throws([&] { pe::Image(arm.data(), arm.size(), f.base); }), "a machine that is neither x86 nor x64 throws" + b);
    std::vector<std::uint8_t> bare(0x5000);
    std::copy(f.data.begin(), f.data.begin() + 0x1000, bare.begin());
    for (std::size_t s = 0; s < 4; ++s) bare[PeFixture::kOpt + (x64 ? 0xF0 : 0xE0) + 40 * s] = 'x';
    Check(Throws([&] { pe::Image(bare.data(), bare.size(), f.base).Text(); }), "an image with no .text throws when asked for it" + b);
}

void PatternsAreChecked() {
    std::cout << "Pattern:\n";
    Check(Throws([] { pe::Pattern("?? 8B"); }), "a pattern starting with ?? throws");
    Check(Throws([] { pe::Pattern("8B 4"); }), "half a byte throws");
    Check(Throws([] { pe::Pattern("8B 4G"); }), "a byte that is not hex throws");
    Check(Throws([] { pe::Pattern(""); }), "an empty pattern throws");
    Check(pe::Pattern("8B41??85C0").Size() == 5, "spaces are optional");
    Check(Throws([] { pe::Pattern("8B -1 85"); }), "a signed byte throws rather than becoming a wildcard");
    Check(Throws([] { pe::Pattern("8B +5"); }), "a plus sign throws");
    Check(Throws([] { pe::Pattern("8B 4 C0"); }), "a space inside a byte throws");
    Check(Throws([] { pe::Pattern("8B ?5"); }), "half a wildcard throws");
    Check(pe::Pattern("8b aF").Size() == 2, "hex digits in either case are read");
}

void CallsAndPushesAreFoundByTarget(bool x64) {
    PeFixture f(x64);
    const std::string b = Bits(x64);
    f.Call(0x1100, 0x1800);
    f.Call(0x1200, 0x1800);
    f.Call(0x1300, 0x1840);
    f.Code(0x1400, {0x68});
    f.Put(0x1401, static_cast<std::uint32_t>(f.base + 0x2100));
    const pe::Image image = f.Open();
    const std::vector<std::uint32_t> calls = pe::CallsTo(image, 0x1800);
    Check(calls.size() == 2 && calls[0] == 0x1100u && calls[1] == 0x1200u, "CallsTo finds each call" + b);
    const std::vector<std::uint32_t> targets = pe::CallTargetsIn(image, 0x1800, 0x80);
    Check(targets.size() == 2 && targets[0] == 0x1800u && targets[1] == 0x1840u, "CallTargetsIn finds each target once" + b);
    if (x64) {
        Check(Throws([&] { pe::Pushes(image, 0x2100); }), "Pushes refuses an x64 image");
    } else {
        const std::vector<std::uint32_t> pushes = pe::Pushes(image, 0x2100);
        Check(pushes.size() == 1 && pushes[0] == 0x1400u, "Pushes finds the push of an address");
    }
}

// A string is found only where it stands alone and only once; where it may
// start is the caller's choice.
void AStringIsFoundOnce(bool x64) {
    const std::string b = Bits(x64);
    PeFixture f(x64);
    f.Text(0x2101, "CExoInput error");
    f.Text(0x2201, "xCExoInput error");
    Check(pe::UniqueString(f.Open(), "CExoInput error") == 0x2101u, "a string after a NUL is found" + b);
    f.Text(0x2301, "CExoInput error");
    Check(!pe::UniqueString(f.Open(), "CExoInput error").has_value(), "a string found twice is nothing" + b);

    PeFixture g(x64);
    g.data[0x2103] = 'q';  // the aligned string has no NUL before it
    g.Text(0x2104, "Camera");
    Check(!pe::UniqueString(g.Open(), "Camera").has_value(), "an aligned string with no NUL before it is not found by default" + b);
    Check(pe::UniqueString(g.Open(), "Camera", pe::StringStart::AfterNulOrAligned4) == 0x2104u,
          "it is found where a four-byte boundary may start a string" + b);
}

// An import is found by name through the lookup table, which the loader
// leaves alone, past an import by ordinal; the DLL's name in any case.
void AnImportIsFoundByName(bool x64) {
    const std::string b = Bits(x64);
    PeFixture f(x64);
    Check(!pe::ImportSlot(f.Open(), "GLU32.dll", "gluPerspective").has_value(), "an image with no imports has no slot" + b);
    f.Import("GLU32.dll", "gluPerspective");
    const pe::Image image = f.Open();
    Check(pe::ImportSlot(image, "glu32.DLL", "gluPerspective") == (x64 ? 0x2910u : 0x2908u), "the slot is found by name" + b);
    Check(!pe::ImportSlot(image, "GLU32.dll", "gluLookAt").has_value(), "a function not imported has no slot" + b);
    Check(!pe::ImportSlot(image, "OPENGL32.dll", "gluPerspective").has_value(), "a DLL not imported has no slot" + b);
}

void AFunctionStartsAfterItsPadding() {
    std::cout << "FunctionStart:\n";
    PeFixture f(false);
    std::fill(f.data.begin() + 0x1100, f.data.begin() + 0x1200, std::uint8_t{0x90});
    f.data[0x1140] = 0xCC;  // one int3 in the code, not after a ret: not padding
    Check(pe::FunctionStart(f.Open(), 0x1150, 0x100) == 0x1100u, "the start follows a run of int3");
    f.data[0x1160] = 0xC3;
    f.data[0x1161] = 0xCC;
    Check(pe::FunctionStart(f.Open(), 0x1170, 0x100) == 0x1162u, "one int3 after a ret is padding");
    Check(Throws([&] { pe::FunctionStart(f.Open(), 0x11F0, 0x20); }), "no start within reach throws");
}

void TheUnwindTableGivesFunctions() {
    std::cout << "Unwind table:\n";
    PeFixture f(true);
    f.Directory(3, 0x4000, 24);
    f.Put(0x4000, std::uint32_t{0x1100});
    f.Put(0x4004, std::uint32_t{0x1180});
    f.Put(0x4008, std::uint32_t{0x2600});
    f.Put(0x400C, std::uint32_t{0x1300});
    f.Put(0x4010, std::uint32_t{0x1340});
    f.Put(0x4014, std::uint32_t{0x2610});
    f.data[0x2600] = 0x01;              // version 1, no flags
    f.data[0x2610] = 0x01 | (4 << 3);   // version 1, chained
    f.Put(0x2614, std::uint32_t{0x1100});  // its parent's RUNTIME_FUNCTION
    f.Put(0x2618, std::uint32_t{0x1180});
    f.Put(0x261C, std::uint32_t{0x2600});
    const pe::Image image = f.Open();
    Check(image.HasUnwindTable(), "an x64 image has an unwind table");
    Check(image.FunctionAt(0x1110) == std::pair<std::uint32_t, std::uint32_t>{0x1100, 0x1180}, "FunctionAt is the entry holding an RVA");
    Check(image.FunctionAt(0x1200) == std::pair<std::uint32_t, std::uint32_t>{0, 0}, "FunctionAt is {0, 0} between entries");
    Check(image.RootOf(0x1310) == 0x1100u, "a chained part's root is its parent's start");
    Check(image.RootOf(0x1110) == 0x1100u && image.RootOf(0x1200) == 0u, "a root is its own; no entry has none");
    Check(image.Parts(0x1100).size() == 2, "Parts lists both entries of the split function");
    PeFixture g(false);
    Check(!g.Open().HasUnwindTable() && Throws([&] { g.Open().FunctionAt(0x1100); }), "an x86 image has none, and asking throws");
}

#ifdef _WIN32
void ThisExeReadsAsLoaded() {
    std::cout << "This executable:\n";
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const pe::Image image(reinterpret_cast<const std::uint8_t*>(base), base);
    Check(image.Is64() == (sizeof(void*) == 8), "this build's bitness");
    const auto at = image.RvaOf(reinterpret_cast<std::uintptr_t>(&ThisExeReadsAsLoaded));
    Check(at && image.Text().Contains(*at), "a function of this exe is in its .text");
    Check(pe::ImportSlot(image, "kernel32.dll", "GetModuleHandleW").has_value(), "an import of this exe has its slot");
#if defined(_WIN64)
    Check(image.HasUnwindTable() && image.RootOf(*at) == *at, "its unwind table names this function's start");
#endif
}
#endif

}  // namespace

int RunPeImageTests() {
    g_failures = 0;
    for (const bool x64 : {false, true}) {
        std::cout << "PE image" << Bits(x64) << ":\n";
        TheImageIsReadAsTheLoaderLaysItOut(x64);
        CallsAndPushesAreFoundByTarget(x64);
        AStringIsFoundOnce(x64);
        AnImportIsFoundByName(x64);
    }
    PatternsAreChecked();
    AFunctionStartsAfterItsPadding();
    TheUnwindTableGivesFunctions();
#ifdef _WIN32
    ThisExeReadsAsLoaded();
#endif
    return g_failures;
}
