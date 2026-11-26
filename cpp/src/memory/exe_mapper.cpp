// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/memory/exe_mapper.h>

#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cameraunlock::memory {

MappedExe::MappedExe(MappedExe&& other) noexcept { *this = std::move(other); }

MappedExe& MappedExe::operator=(MappedExe&& other) noexcept {
    if (this != &other) {
        if (at_) VirtualFree(at_, 0, MEM_RELEASE);
        at_ = std::exchange(other.at_, nullptr);
        size_ = other.size_;
        is64_ = other.is64_;
        preferred_ = other.preferred_;
        pointerBase_ = other.pointerBase_;
        timeDateStamp_ = other.timeDateStamp_;
        relocations_ = other.relocations_;
    }
    return *this;
}

MappedExe::~MappedExe() {
    if (at_) VirtualFree(at_, 0, MEM_RELEASE);
}

namespace {

constexpr unsigned kDirectoryBaseReloc = 5;

std::string Name(const std::filesystem::path& path) { return path.u8string(); }

std::string Hex(std::uint64_t v) {
    char text[24];
    std::snprintf(text, sizeof text, "0x%llX", static_cast<unsigned long long>(v));
    return text;
}

template <class T>
T FileRead(const std::vector<char>& file, std::uint64_t at, const char* what) {
    if (at > file.size() || sizeof(T) > file.size() - at) throw std::runtime_error(std::string(what) + " is past the end of the file");
    T v;
    std::memcpy(&v, file.data() + at, sizeof v);
    return v;
}

}  // namespace

struct ExeMapping {
    enum class To { HeaderImageBase, WhereItLies, Given };

    static MappedExe Map(const std::filesystem::path& path, To to, std::uint64_t given) {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open " + Name(path));
        const std::vector<char> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        if (FileRead<std::uint16_t>(file, 0, "the MZ header") != 0x5A4D) throw std::runtime_error(Name(path) + " is not an MZ file");
        const std::uint64_t nt = FileRead<std::uint32_t>(file, 0x3C, "the MZ header");
        if (FileRead<std::uint32_t>(file, nt, "the PE header") != 0x4550) throw std::runtime_error(Name(path) + " has no PE header");
        const auto machine = FileRead<std::uint16_t>(file, nt + 4, "the PE header");
        const std::uint64_t opt = nt + 24;
        const auto magic = FileRead<std::uint16_t>(file, opt, "the optional header");
        MappedExe m;
        if (machine == 0x014C && magic == 0x10B) m.is64_ = false;
        else if (machine == 0x8664 && magic == 0x20B) m.is64_ = true;
        else throw std::runtime_error(Name(path) + " is neither an x86 nor an x64 PE file");
        m.timeDateStamp_ = FileRead<std::uint32_t>(file, nt + 8, "the PE header");
        m.size_ = FileRead<std::uint32_t>(file, opt + 56, "the optional header");
        const auto sizeOfHeaders = FileRead<std::uint32_t>(file, opt + 60, "the optional header");
        m.preferred_ = m.is64_ ? FileRead<std::uint64_t>(file, opt + 24, "the optional header") : FileRead<std::uint32_t>(file, opt + 28, "the optional header");
        if (sizeOfHeaders > file.size() || sizeOfHeaders > m.size_) throw std::runtime_error("the headers are larger than the file or the image");

        m.at_ = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, m.size_, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!m.at_) throw std::runtime_error("cannot allocate " + std::to_string(m.size_) + " bytes for the image, error " + std::to_string(GetLastError()));
        std::memcpy(m.at_, file.data(), sizeOfHeaders);
        const auto count = FileRead<std::uint16_t>(file, nt + 6, "the PE header");
        const std::uint64_t first = opt + FileRead<std::uint16_t>(file, nt + 20, "the PE header");
        for (unsigned i = 0; i < count; ++i) {
            const std::uint64_t s = first + 40ull * i;
            const auto virtualSize = FileRead<std::uint32_t>(file, s + 8, "a section header");
            const auto rva = FileRead<std::uint32_t>(file, s + 12, "a section header");
            const auto rawSize = FileRead<std::uint32_t>(file, s + 16, "a section header");
            const auto raw = FileRead<std::uint32_t>(file, s + 20, "a section header");
            if (rva > m.size_) throw std::runtime_error("a section starts past the image");
            std::size_t n = (std::min)(static_cast<std::size_t>(rawSize), file.size() - (std::min)(file.size(), static_cast<std::size_t>(raw)));
            n = (std::min)(n, static_cast<std::size_t>(m.size_ - rva));
            // The loader maps no more of the raw data than the section's VirtualSize.
            if (virtualSize) n = (std::min)(n, static_cast<std::size_t>(virtualSize));
            if (n) std::memcpy(m.at_ + rva, file.data() + raw, n);
        }

        m.pointerBase_ = to == To::HeaderImageBase ? m.preferred_ : to == To::WhereItLies ? m.Address() : given;
        if (m.pointerBase_ != m.preferred_) {
            if (!m.is64_ && m.pointerBase_ > 0xFFFFFFFFull)
                throw std::runtime_error("an x86 exe cannot be relocated to " + Hex(m.pointerBase_) + ", above 4 GiB: map it from an x86 process");
            const auto directories = FileRead<std::uint32_t>(file, opt + (m.is64_ ? 108 : 92), "the optional header");
            if (directories <= kDirectoryBaseReloc) throw std::runtime_error("the exe has no base relocations, so it can only be read at its own base");
            m.relocations_ = Relocate(m, opt + (m.is64_ ? 112 : 96), file);
        }
        return m;
    }

    // Adds the difference between where the image's pointers are to point and
    // its preferred base at every base relocation; how many there were.
    static std::size_t Relocate(MappedExe& m, std::uint64_t directories, const std::vector<char>& file) {
        const auto rva = FileRead<std::uint32_t>(file, directories + 8 * kDirectoryBaseReloc, "the optional header");
        const auto size = FileRead<std::uint32_t>(file, directories + 8 * kDirectoryBaseReloc + 4, "the optional header");
        if (!rva || !size) throw std::runtime_error("the exe has no base relocations, so it can only be read at its own base");
        if (std::uint64_t{rva} + size > m.size_) throw std::runtime_error("the base relocations run past the image");
        const std::uint64_t delta = m.pointerBase_ - m.preferred_;
        std::size_t fixed = 0;
        for (std::uint32_t off = 0; off + 8 <= size;) {
            std::uint32_t page, blockSize;
            std::memcpy(&page, m.at_ + rva + off, 4);
            std::memcpy(&blockSize, m.at_ + rva + off + 4, 4);
            if (blockSize == 0) break;  // the directory's zero padding
            if (blockSize < 8) throw std::runtime_error("a base relocation block of " + std::to_string(blockSize) + " bytes, shorter than its header");
            if (std::uint64_t{off} + blockSize > size) throw std::runtime_error("a base relocation block runs past the directory");
            for (std::uint32_t i = 8; i + 2 <= blockSize; i += 2) {
                std::uint16_t entry;
                std::memcpy(&entry, m.at_ + rva + off + i, 2);
                const unsigned type = entry >> 12;
                if (type == 0) continue;  // IMAGE_REL_BASED_ABSOLUTE, padding
                const std::uint64_t where = std::uint64_t{page} + (entry & 0xFFF);
                if (type == 3) {  // HIGHLOW
                    if (where + 4 > m.size_) throw std::runtime_error("a base relocation points past the image");
                    std::uint32_t v;
                    std::memcpy(&v, m.at_ + where, 4);
                    v += static_cast<std::uint32_t>(delta);
                    std::memcpy(m.at_ + where, &v, 4);
                } else if (type == 10) {  // DIR64
                    if (where + 8 > m.size_) throw std::runtime_error("a base relocation points past the image");
                    std::uint64_t v;
                    std::memcpy(&v, m.at_ + where, 8);
                    v += delta;
                    std::memcpy(m.at_ + where, &v, 8);
                } else {
                    throw std::runtime_error("a base relocation of type " + std::to_string(type) + ", neither HIGHLOW nor DIR64");
                }
                ++fixed;
            }
            off += blockSize;
        }
        return fixed;
    }
};

MappedExe MapExe(const std::filesystem::path& path, PointersAt pointers) {
    return ExeMapping::Map(path, pointers == PointersAt::WhereItLies ? ExeMapping::To::WhereItLies : ExeMapping::To::HeaderImageBase, 0);
}

MappedExe MapExe(const std::filesystem::path& path, std::uint64_t pointerBase) { return ExeMapping::Map(path, ExeMapping::To::Given, pointerBase); }

}  // namespace cameraunlock::memory
