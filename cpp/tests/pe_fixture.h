// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

#include <cameraunlock/memory/pe_image.h>

// A hand-made PE image as the loader lays it out: headers, .text at 0x1000
// (int3 throughout), .rdata at 0x2000, .data at 0x3000 and .pdata at 0x4000,
// each 0x1000 long, x86 or x64.
struct PeFixture {
    static constexpr std::uint64_t kBase32 = 0x400000;
    static constexpr std::uint64_t kBase64 = 0x140000000ull;
    static constexpr std::uint32_t kOpt = 0x98;

    bool is64;
    std::uint64_t base;
    std::vector<std::uint8_t> data = std::vector<std::uint8_t>(0x5000);

    explicit PeFixture(bool x64) : is64(x64), base(x64 ? kBase64 : kBase32) {
        Put(0, std::uint16_t{0x5A4D});
        Put(0x3C, std::uint32_t{0x80});
        Put(0x80, std::uint32_t{0x4550});
        Put(0x84, std::uint16_t{static_cast<std::uint16_t>(x64 ? 0x8664 : 0x014C)});
        Put(0x86, std::uint16_t{4});
        const std::uint16_t optSize = x64 ? 0xF0 : 0xE0;
        Put(0x94, optSize);
        Put(kOpt, std::uint16_t{static_cast<std::uint16_t>(x64 ? 0x20B : 0x10B)});
        Put(kOpt + 56, static_cast<std::uint32_t>(data.size()));
        if (x64) Put(kOpt + 24, base);
        else Put(kOpt + 28, static_cast<std::uint32_t>(base));
        Put(kOpt + (x64 ? 108 : 92), std::uint32_t{16});
        const char* names[] = {".text", ".rdata", ".data", ".pdata"};
        const std::uint32_t flags[] = {0x60000020u, 0x40000040u, 0xC0000040u, 0x40000040u};
        for (std::uint32_t i = 0; i < 4; ++i) {
            const std::uint32_t s = kOpt + optSize + 40 * i;
            std::memcpy(data.data() + s, names[i], std::strlen(names[i]));
            Put(s + 8, std::uint32_t{0x1000});
            Put(s + 12, 0x1000 * (i + 1));
            Put(s + 36, flags[i]);
        }
        std::fill(data.begin() + 0x1000, data.begin() + 0x2000, std::uint8_t{0xCC});
    }

    template <class T>
    void Put(std::size_t at, const T& value) {
        std::memcpy(data.data() + at, &value, sizeof(value));
    }
    // An absolute pointer of the image's width to `rva`.
    void Pointer(std::size_t at, std::uint32_t rva) {
        if (is64) Put(at, base + rva);
        else Put(at, static_cast<std::uint32_t>(base + rva));
    }
    void Code(std::size_t at, std::initializer_list<std::uint8_t> bytes) { std::copy(bytes.begin(), bytes.end(), data.begin() + at); }
    void Text(std::size_t at, const char* text) { std::memcpy(data.data() + at, text, std::strlen(text) + 1); }
    void Call(std::uint32_t at, std::uint32_t target) {
        Code(at, {0xE8});
        Put(at + 1, static_cast<std::int32_t>(target - (at + 5)));
    }
    void Directory(unsigned index, std::uint32_t rva, std::uint32_t size) {
        const std::uint32_t at = kOpt + (is64 ? 112 : 96) + 8 * index;
        Put(at, rva);
        Put(at + 4, size);
    }
    // An import directory at .rdata 0x2800 naming `function` from `dll`, past
    // an import by name and one by ordinal. Its slot is the third.
    void Import(const char* dll, const char* function) {
        const std::uint32_t w = is64 ? 8 : 4;
        Directory(1, 0x2800, 40);
        Put(0x2800, std::uint32_t{0x2880});  // OriginalFirstThunk
        Put(0x280C, std::uint32_t{0x2980});  // Name
        Put(0x2810, std::uint32_t{0x2900});  // FirstThunk
        const auto entry = [&](std::uint32_t at, std::uint64_t v) {
            if (is64) Put(at, v);
            else Put(at, static_cast<std::uint32_t>(v));
        };
        entry(0x2880, 0x29A0);
        entry(0x2880 + w, (is64 ? 0x8000000000000000ull : 0x80000000ull) | 7);
        entry(0x2880 + 2 * w, 0x29C0);
        entry(0x2900, 0x77001000);  // the loader's addresses, not names
        entry(0x2900 + w, 0x77002000);
        entry(0x2900 + 2 * w, 0x77003000);
        Text(0x2980, dll);
        Text(0x29A2, "glOther");
        Text(0x29C2, function);
    }
    cameraunlock::memory::pe::Image Open() const { return cameraunlock::memory::pe::Image(data.data(), data.size(), base); }
};
