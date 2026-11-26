// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/re_engine/exe.h>

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>
#include <string>

#include <cameraunlock/memory/safe_memory.h>

namespace cameraunlock::re_engine {

bool SafeRead(std::uintptr_t at, void* out, std::size_t size) {
    // Windows maps nothing below this.
    constexpr std::uintptr_t kLowestAddress = 0x10000;
    if (at < kLowestAddress || at + size < at) return false;
    return memory::GuardedMemcpy(out, reinterpret_cast<const void*>(at), size);
}

namespace {

struct Pattern {
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint8_t> any;
};

Pattern Parse(std::string_view text) {
    Pattern p;
    std::size_t at = 0;
    while (at < text.size()) {
        if (text[at] == ' ') {
            ++at;
            continue;
        }
        if (text[at] == '?') {
            p.bytes.push_back(0);
            p.any.push_back(1);
            at += (at + 1 < text.size() && text[at + 1] == '?') ? 2 : 1;
            continue;
        }
        if (at + 1 >= text.size()) throw std::runtime_error("pattern '" + std::string(text) + "' ends half a byte in");
        if (!std::isxdigit(static_cast<unsigned char>(text[at])) || !std::isxdigit(static_cast<unsigned char>(text[at + 1])))
            throw std::runtime_error("pattern '" + std::string(text) + "' has '" + std::string(text.substr(at, 2)) + "', which is no hex byte");
        p.bytes.push_back(static_cast<std::uint8_t>(std::stoul(std::string(text.substr(at, 2)), nullptr, 16)));
        p.any.push_back(0);
        at += 2;
    }
    if (p.bytes.empty()) throw std::runtime_error("pattern '" + std::string(text) + "' has no bytes");
    return p;
}

bool Readable(const MEMORY_BASIC_INFORMATION& mbi) {
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    return (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                           PAGE_EXECUTE_WRITECOPY)) != 0;
}

}  // namespace

std::vector<std::uintptr_t> ScanAll(std::string_view text) {
    const Pattern p = Parse(text);
    std::vector<std::uintptr_t> hits;
    const Exe& exe = Game();
    std::uintptr_t at = exe.base;
    while (at < exe.end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<const void*>(at), &mbi, sizeof(mbi))) break;
        const auto regionEnd = std::min(reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize, exe.end);
        if (Readable(mbi) && regionEnd - at >= p.bytes.size()) {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(at);
            const std::size_t last = regionEnd - at - p.bytes.size();
            for (std::size_t i = 0; i <= last; ++i) {
                if (!p.any[0]) {
                    const void* first = std::memchr(bytes + i, p.bytes[0], last - i + 1);
                    if (!first) break;
                    i = static_cast<std::size_t>(static_cast<const std::uint8_t*>(first) - bytes);
                }
                std::size_t k = 1;
                while (k < p.bytes.size() && (p.any[k] || bytes[i + k] == p.bytes[k])) ++k;
                if (k == p.bytes.size()) hits.push_back(at + i);
            }
        }
        at = regionEnd;
    }
    return hits;
}

std::uintptr_t RipTarget(std::uintptr_t disp) {
    return disp + 4 + *reinterpret_cast<const std::int32_t*>(disp);
}

}  // namespace cameraunlock::re_engine
