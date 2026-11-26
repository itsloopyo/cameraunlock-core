// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

// Everything a discovery probe reads goes through Memory, so the probes run the
// same way against a live process (ProcessMemory, process_memory.h) and against
// a synthetic heap built by a test.
namespace cameraunlock::memory {

class Memory {
public:
    virtual ~Memory() = default;

    // False when any byte of [addr, addr + size) is unreadable.
    virtual bool Read(std::uintptr_t addr, void* out, std::size_t size) const = 0;

    template <class T>
    bool Get(std::uintptr_t addr, T& out) const {
        return Read(addr, &out, sizeof(T));
    }

    // The value, or zero when it does not read. For probes, where an unreadable
    // field and a zero field reject a candidate the same way.
    template <class T>
    T Or0(std::uintptr_t addr) const {
        T value{};
        return Get(addr, value) ? value : T{};
    }

    std::uintptr_t Ptr(std::uintptr_t addr) const { return Or0<std::uintptr_t>(addr); }

    // A NUL-terminated ASCII string of at most `max` bytes. Empty when it does
    // not read, holds a non-printable byte, or runs past `max`.
    std::string Ascii(std::uintptr_t addr, std::size_t max = 256) const {
        std::string out;
        char chunk[16];
        for (std::size_t at = 0; at < max; at += sizeof(chunk)) {
            const std::size_t n = (max - at) < sizeof(chunk) ? (max - at) : sizeof(chunk);
            if (!Read(addr + at, chunk, n)) {
                // A string can end within the last bytes of a page; finish byte by byte.
                for (std::size_t i = 0; i < n; ++i) {
                    char c = 0;
                    if (!Get(addr + at + i, c)) return {};
                    if (c == 0) return out;
                    if (c < 0x20 || c > 0x7e) return {};
                    out.push_back(c);
                }
                continue;
            }
            for (std::size_t i = 0; i < n; ++i) {
                if (chunk[i] == 0) return out;
                if (chunk[i] < 0x20 || chunk[i] > 0x7e) return {};
                out.push_back(chunk[i]);
            }
        }
        return {};
    }

    // Exactly `len` bytes compared against `text`, no terminator required.
    // False for `len` over 64.
    bool BytesEqual(std::uintptr_t addr, const char* text, std::size_t len) const {
        char buf[64];
        if (len > sizeof(buf) || !Read(addr, buf, len)) return false;
        return std::memcmp(buf, text, len) == 0;
    }
};

// A 64-bit user-mode heap or image pointer: canonical, 8-byte aligned, above
// the null page. Says nothing about whether it reads.
inline bool PlausiblePtr(std::uint64_t v) {
    return v >= 0x10000 && v < 0x7FFFFFFFFFFFull && (v & 7) == 0;
}

struct Range {
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;

    bool Contains(std::uintptr_t at, std::size_t length = 1) const {
        return at >= begin && at <= end && length <= end - at;
    }
};

// A module as discovery sees it: where its code and its writable data lie.
// Writable ranges are where an engine's globals live.
struct ModuleMap {
    std::uintptr_t base = 0;
    std::uintptr_t end = 0;
    std::vector<Range> code;
    std::vector<Range> writable;
    std::vector<Range> readonly;  // constant data: string literals

    bool InModule(std::uintptr_t at) const { return at >= base && at < end; }
    bool InCode(std::uintptr_t at) const {
        for (const Range& r : code)
            if (r.Contains(at)) return true;
        return false;
    }
};

// Function extents from a module's unwind table (.pdata). A compiler that
// splits a function into hot and cold parts gives each part its own entry and
// chains the later ones to the first; the root is the function's real start.
class CodeMap {
public:
    virtual ~CodeMap() = default;

    // The unwind entry covering `at`, or an empty range.
    virtual Range PartAt(std::uintptr_t at) const = 0;

    // The start of the function `at` belongs to, following chained entries.
    // 0 when `at` is not inside any function.
    virtual std::uintptr_t RootOf(std::uintptr_t at) const = 0;

    // Every part of the function starting at `root`.
    virtual std::vector<Range> Parts(std::uintptr_t root) const = 0;
};

// Every address in `ranges` where `needle` starts. The ranges are read in
// 64 KiB chunks; a chunk that does not read whole is read page by page from
// its start, and what follows its first unreadable page is not searched.
std::vector<std::uintptr_t> FindBytes(const Memory& mem, const std::vector<Range>& ranges, std::string_view needle);

// Every place `text` sits with a NUL before and after it, so a longer string
// ending in it does not count.
std::vector<std::uintptr_t> FindCString(const Memory& mem, const std::vector<Range>& ranges, const std::string& text);

}  // namespace cameraunlock::memory
