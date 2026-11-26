// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/memory/memory_view.h>

#include <algorithm>

namespace cameraunlock::memory {
namespace {

constexpr std::size_t kChunk = 0x10000;

// Calls fn(address of chunk, bytes, count) over every readable chunk of each
// range, each chunk carrying `overlap` extra bytes so a match across a chunk
// boundary is seen once.
template <class Fn>
void ForEachChunk(const Memory& mem, const std::vector<Range>& ranges, std::size_t overlap, Fn&& fn) {
    std::vector<std::uint8_t> buffer(kChunk + overlap);
    for (const Range& range : ranges) {
        for (std::uintptr_t at = range.begin; at < range.end; at += kChunk) {
            std::size_t want = std::min<std::size_t>(kChunk + overlap, range.end - at);
            if (!mem.Read(at, buffer.data(), want)) {
                std::size_t got = 0;
                while (got < want && mem.Read(at + got, buffer.data() + got, std::min<std::size_t>(0x1000, want - got))) got += 0x1000;
                want = std::min(got, want);
                if (!want) continue;
            }
            fn(at, buffer.data(), want);
        }
    }
}

}  // namespace

std::vector<std::uintptr_t> FindBytes(const Memory& mem, const std::vector<Range>& ranges, std::string_view needle) {
    std::vector<std::uintptr_t> out;
    if (needle.empty()) return out;
    // The search runs from the needle's first non-NUL byte, its leading NULs
    // checked at each hit: find looks for a needle's first byte with memchr,
    // and in data sections a leading NUL stops it at nearly every byte.
    std::size_t lead = needle.find_first_not_of('\0');
    if (lead == std::string_view::npos) lead = 0;
    const std::string_view rest = needle.substr(lead);
    ForEachChunk(mem, ranges, needle.size() - 1, [&](std::uintptr_t at, const std::uint8_t* bytes, std::size_t n) {
        const std::string_view hay(reinterpret_cast<const char*>(bytes), n);
        for (std::size_t i = hay.find(rest, lead); i != std::string_view::npos && i - lead < kChunk; i = hay.find(rest, i + 1))
            if (hay.compare(i - lead, lead, needle, 0, lead) == 0) out.push_back(at + i - lead);
    });
    return out;
}

std::vector<std::uintptr_t> FindCString(const Memory& mem, const std::vector<Range>& ranges, const std::string& text) {
    std::string needle(1, '\0');
    needle += text;
    needle.push_back('\0');
    std::vector<std::uintptr_t> out;
    for (std::uintptr_t at : FindBytes(mem, ranges, needle)) out.push_back(at + 1);
    return out;
}

}  // namespace cameraunlock::memory
