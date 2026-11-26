// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Tests for the heap search (cameraunlock/memory/heap_search.h) over this
// process and the exe mapper (cameraunlock/memory/exe_mapper.h) over PE files
// the tests write.

#include <cameraunlock/memory/exe_mapper.h>
#include <cameraunlock/memory/heap_search.h>
#include <cameraunlock/memory/pe_image.h>

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pe_fixture.h"

namespace {

using namespace cameraunlock::memory;

int g_failures = 0;

void Check(bool cond, const std::string& name) {
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << name << "\n";
    if (!cond) ++g_failures;
}

template <class F>
bool Throws(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

void HeapSearchFindsAPlantedValue() {
    std::cout << "\n[heap search]\n";
    const std::uintptr_t value = static_cast<std::uintptr_t>(0x5EA2C4F1D00DBEEFull);
    auto* rw = static_cast<std::uintptr_t*>(VirtualAlloc(nullptr, 0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    auto* ro = static_cast<std::uintptr_t*>(VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    auto* guard = static_cast<std::uintptr_t*>(VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!rw || !ro || !guard) throw std::runtime_error("VirtualAlloc failed in the heap search test");
    rw[3] = value;
    rw[0x1000 / sizeof(std::uintptr_t) + 7] = value;
    ro[5] = value;
    guard[1] = value;
    DWORD old;
    if (!VirtualProtect(ro, 0x1000, PAGE_READONLY, &old) || !VirtualProtect(guard, 0x1000, PAGE_READWRITE | PAGE_GUARD, &old))
        throw std::runtime_error("VirtualProtect failed in the heap search test");
    volatile std::uintptr_t onStack = value;

    std::vector<std::uintptr_t> found;
    const HeapSearch look = FindInPrivateMemory(value, [&](std::uintptr_t at) { found.push_back(at); });
    const auto has = [&](const void* p) { return std::find(found.begin(), found.end(), reinterpret_cast<std::uintptr_t>(p)) != found.end(); };
    Check(has(&rw[3]) && has(&rw[0x1000 / sizeof(std::uintptr_t) + 7]), "both copies in private read-write pages are found");
    Check(!has(&ro[5]), "a read-only page is not looked in");
    Check(!has(&guard[1]), "a guard page is not looked in");
    Check(!has(const_cast<std::uintptr_t*>(&onStack)), "the calling thread's stack is not looked in");
    Check(look.matches == found.size() && look.pagesRead >= 2, "the tally counts every match and the pages read");

    MEMORY_BASIC_INFORMATION m;
    VirtualQuery(guard, &m, sizeof m);
    Check((m.Protect & PAGE_GUARD) != 0, "the guard page keeps its guard");
    VirtualFree(rw, 0, MEM_RELEASE);
    VirtualFree(ro, 0, MEM_RELEASE);
    VirtualFree(guard, 0, MEM_RELEASE);
}

// The fixture laid out as a file as well: every section's raw data at its RVA,
// headers 0x400 long, with a pointer to .text+0x10 at .rdata 0x2100 and one
// base relocation for it (and one padding entry) at .data 0x3800.
struct ExeFile {
    PeFixture f;
    std::filesystem::path path;

    explicit ExeFile(bool x64, const char* name) : f(x64) {
        f.Put(PeFixture::kOpt + 60, std::uint32_t{0x400});
        const std::uint16_t optSize = x64 ? 0xF0 : 0xE0;
        for (std::uint32_t i = 0; i < 4; ++i) {
            const std::uint32_t s = PeFixture::kOpt + optSize + 40 * i;
            f.Put(s + 16, std::uint32_t{0x1000});
            f.Put(s + 20, 0x1000 * (i + 1));
        }
        f.Pointer(0x2100, 0x1010);
        f.Directory(5, 0x3800, 12);
        f.Put(0x3800, std::uint32_t{0x2000});
        f.Put(0x3804, std::uint32_t{12});
        f.Put(0x3808, static_cast<std::uint16_t>(((x64 ? 10 : 3) << 12) | 0x100));
        f.Put(0x380A, std::uint16_t{0});
        path = std::filesystem::temp_directory_path() / (std::string("cameraunlock_exe_mapper_") + name + std::to_string(GetCurrentProcessId()) + ".exe");
        Write();
    }
    ~ExeFile() { std::filesystem::remove(path); }

    void Write() const {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(f.data.data()), static_cast<std::streamsize>(f.data.size()));
        out.close();
        if (!out) throw std::runtime_error("cannot write " + path.u8string());
    }
    std::uint64_t PointerAt(const MappedExe& m) const {
        if (f.is64) {
            std::uint64_t v;
            std::memcpy(&v, m.At() + 0x2100, 8);
            return v;
        }
        std::uint32_t v;
        std::memcpy(&v, m.At() + 0x2100, 4);
        return v;
    }
};

void ExeMapperLaysOutAndRelocates(bool x64) {
    const std::string b = x64 ? " (x64)" : " (x86)";
    std::cout << "\n[exe mapper" << b << "]\n";
    ExeFile file(x64, x64 ? "64_" : "32_");

    {
        const MappedExe m = MapExe(file.path, PointersAt::HeaderImageBase);
        Check(m.Is64() == x64 && m.SizeOfImage() == 0x5000 && m.PreferredBase() == file.f.base && m.PointerBase() == file.f.base,
              "the header's bitness, size and base are read" + b);
        Check(m.Relocations() == 0 && file.PointerAt(m) == file.f.base + 0x1010, "at the header's ImageBase nothing is relocated" + b);
        Check(std::memcmp(m.At() + 0x1000, file.f.data.data() + 0x1000, 0x3000) == 0, "the sections are laid out at their RVAs" + b);
        const pe::Image image(m.At(), m.SizeOfImage(), m.PointerBase());
        Check(image.Text().rva == 0x1000 && image.PointerOf(0x1010) == file.f.base + 0x1010, "a pe::Image reads the mapped copy" + b);
    }
    {
        const std::uint64_t to = x64 ? 0x7FF600000000ull : 0x10000000ull;
        const MappedExe m = MapExe(file.path, to);
        Check(m.PointerBase() == to && m.Relocations() == 1 && file.PointerAt(m) == to + 0x1010,
              "relocated to a given base, the one pointer moves and the padding entry is skipped" + b);
    }
    try {
        const MappedExe m = MapExe(file.path, PointersAt::WhereItLies);
        Check(m.PointerBase() == m.Address() && file.PointerAt(m) == m.Address() + 0x1010, "relocated to where it lies, the pointer points into the copy" + b);
    } catch (const std::runtime_error& e) {
        // An x64 test process may put the copy of an x86 exe above 4 GiB.
        Check(!x64 && sizeof(void*) == 8 && std::string(e.what()).find("above 4 GiB") != std::string::npos,
              "an x86 copy above 4 GiB is refused, never truncated" + b);
    }
    {
        MappedExe moved = MapExe(file.path, PointersAt::HeaderImageBase);
        const std::uintptr_t keep = moved.Address();
        MappedExe into = std::move(moved);
        Check(into.Address() == keep && moved.At() == nullptr, "a moved mapping keeps its pages" + b);
    }

    file.f.Put(0x3808, static_cast<std::uint16_t>((4 << 12) | 0x100));
    file.Write();
    Check(Throws([&] { MapExe(file.path, std::uint64_t{0x20000000}); }), "a relocation of another type throws" + b);
    const std::uint16_t type = static_cast<std::uint16_t>((x64 ? 10 : 3) << 12);
    const std::uint16_t lastFits = x64 ? 0xFF8 : 0xFFC;
    file.f.Put(0x3800, std::uint32_t{0x4000});
    file.f.Put(0x3808, static_cast<std::uint16_t>(type | lastFits));
    file.Write();
    Check(MapExe(file.path, std::uint64_t{0x20000000}).Relocations() == 1, "a relocation ending at the image's last byte is applied" + b);
    file.f.Put(0x3808, static_cast<std::uint16_t>(type | (lastFits + 2)));
    file.Write();
    Check(Throws([&] { MapExe(file.path, std::uint64_t{0x20000000}); }), "a relocation one past the image throws" + b);
    file.f.Put(0x3808, static_cast<std::uint16_t>(type | 0x100));
    file.f.Put(0x3804, std::uint32_t{4});
    file.Write();
    Check(Throws([&] { MapExe(file.path, std::uint64_t{0x20000000}); }), "a relocation block shorter than its header throws" + b);
    file.f.Put(0x3804, std::uint32_t{12});
    file.f.Put(PeFixture::kOpt + (x64 ? 108 : 92), std::uint32_t{5});
    file.Write();
    Check(Throws([&] { MapExe(file.path, std::uint64_t{0x20000000}); }), "relocating with five data directories, none of them relocations, throws" + b);
    file.f.Put(PeFixture::kOpt + (x64 ? 108 : 92), std::uint32_t{16});
    file.f.Directory(5, 0, 0);
    file.Write();
    Check(Throws([&] { MapExe(file.path, std::uint64_t{0x20000000}); }), "relocating an exe without relocations throws" + b);
    Check(MapExe(file.path, PointersAt::HeaderImageBase).Relocations() == 0, "an exe without relocations maps at its own base" + b);
    file.f.Put(PeFixture::kOpt + 60, std::uint32_t{0x6000});
    file.Write();
    Check(Throws([&] { MapExe(file.path, PointersAt::HeaderImageBase); }), "headers larger than the image throw" + b);
    file.f.Put(PeFixture::kOpt + 60, std::uint32_t{0x400});
    {
        // .rdata's raw data runs a page further than its VirtualSize of 0x100.
        const std::uint32_t rdata = PeFixture::kOpt + (x64 ? 0xF0 : 0xE0) + 40;
        file.f.Put(rdata + 8, std::uint32_t{0x100});
        file.f.Put(0x2200, std::uint32_t{0xA5A5A5A5});
        file.Write();
        const MappedExe m = MapExe(file.path, PointersAt::HeaderImageBase);
        std::uint32_t past;
        std::memcpy(&past, m.At() + 0x2200, 4);
        Check(past == 0, "raw data past a section's VirtualSize is not mapped" + b);
        file.f.Put(rdata + 8, std::uint32_t{0x1000});
    }
    file.f.Put(0x80, std::uint32_t{0});
    file.Write();
    Check(Throws([&] { MapExe(file.path, PointersAt::HeaderImageBase); }), "a file with no PE header throws" + b);
}

void ExeMapperRefusesBadFiles() {
    std::cout << "\n[exe mapper files]\n";
    const auto missing = std::filesystem::temp_directory_path() / "cameraunlock_exe_mapper_missing.exe";
    Check(Throws([&] { MapExe(missing, PointersAt::HeaderImageBase); }), "a file that is not there throws");
    const auto tiny = std::filesystem::temp_directory_path() / ("cameraunlock_exe_mapper_tiny" + std::to_string(GetCurrentProcessId()) + ".exe");
    {
        std::ofstream out(tiny, std::ios::binary);
        out.write("MZ", 2);
    }
    Check(Throws([&] { MapExe(tiny, PointersAt::HeaderImageBase); }), "a file cut off in its MZ header throws");
    std::filesystem::remove(tiny);
}

}  // namespace

int RunHeapExeTests() {
    g_failures = 0;
    HeapSearchFindsAPlantedValue();
    ExeMapperLaysOutAndRelocates(false);
    ExeMapperLaysOutAndRelocates(true);
    ExeMapperRefusesBadFiles();
    return g_failures;
}
