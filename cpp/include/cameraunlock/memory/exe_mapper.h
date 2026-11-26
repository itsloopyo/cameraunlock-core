// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#ifndef _WIN32
#error "cameraunlock/memory/exe_mapper.h is Windows-only: it maps into pages of this process."
#endif

#include <cstddef>
#include <cstdint>
#include <filesystem>

// An x86 or x64 exe read from its file and laid out in this process as the
// loader would lay it out, for reading a game's code offline (scan tools, tests)
// with the same discovery that runs in the game. Nothing in it is run: the
// pages are read-write, the imports are not bound and no TLS callback runs.
namespace cameraunlock::memory {

class MappedExe {
public:
    MappedExe(const MappedExe&) = delete;
    MappedExe& operator=(const MappedExe&) = delete;
    MappedExe(MappedExe&& other) noexcept;
    MappedExe& operator=(MappedExe&& other) noexcept;
    ~MappedExe();

    const std::uint8_t* At() const { return at_; }
    std::uintptr_t Address() const { return reinterpret_cast<std::uintptr_t>(at_); }
    std::uint32_t SizeOfImage() const { return size_; }
    bool Is64() const { return is64_; }
    // The header's ImageBase, which the file's own pointers name.
    std::uint64_t PreferredBase() const { return preferred_; }
    // Where the image's absolute pointers point now: PreferredBase unless it
    // was relocated.
    std::uint64_t PointerBase() const { return pointerBase_; }
    std::uint32_t TimeDateStamp() const { return timeDateStamp_; }
    // How many base relocations were applied.
    std::size_t Relocations() const { return relocations_; }

private:
    friend struct ExeMapping;
    MappedExe() = default;

    std::uint8_t* at_ = nullptr;
    std::uint32_t size_ = 0;
    bool is64_ = false;
    std::uint64_t preferred_ = 0;
    std::uint64_t pointerBase_ = 0;
    std::uint32_t timeDateStamp_ = 0;
    std::size_t relocations_ = 0;
};

// Where a mapped exe's absolute pointers point.
enum class PointersAt {
    // As in the file: the header's ImageBase. No relocation is applied, and a
    // pe::Image over it takes PreferredBase as its pointer base.
    HeaderImageBase,
    // Where the copy lies in this process, so its pointers can be followed as
    // a loaded module's are. Throws when the exe has no base relocations, or an
    // x86 exe lands above 4 GiB (map it from an x86 process).
    WhereItLies,
};

// Throws std::runtime_error naming what is wrong: a file that will not open,
// is not an x86 or x64 PE, has headers or sections past it, or relocations of
// a type other than HIGHLOW and DIR64.
MappedExe MapExe(const std::filesystem::path& path, PointersAt pointers);

// The same with its pointers relocated as if it were loaded at `pointerBase`
// (a running copy's base, to compare against it).
MappedExe MapExe(const std::filesystem::path& path, std::uint64_t pointerBase);

}  // namespace cameraunlock::memory
