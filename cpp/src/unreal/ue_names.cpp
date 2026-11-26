// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/unreal/ue_names.h>

#include <cstring>
#include <string>
#include <utility>

#include <cameraunlock/memory/word_scan.h>

namespace cameraunlock::unreal {

using memory::ForEachQword;
using memory::Memory;
using memory::ModuleMap;
using memory::PlausiblePtr;

namespace {

// A pool entry's header holds its length in ten bits.
constexpr std::uint16_t kPoolLengthMask = 0x3FF;
constexpr int kMaxPoolBlocks = 8192;
constexpr std::size_t kMaxArrayName = 1024;
// The block shifts and chunk lengths looked for: blocks of 2^8 to 2^24 entries.
constexpr int kMinBlockShift = 8, kMaxBlockShift = 24;

bool PoolEntryIs(const Memory& mem, std::uintptr_t at, int shift, const char* text, std::size_t len) {
    std::uint16_t header = 0;
    if (!mem.Get(at, header)) return false;
    if ((header & 1) != 0) return false;
    if (static_cast<std::size_t>((header >> shift) & kPoolLengthMask) != len) return false;
    return mem.BytesEqual(at + 2, text, len);
}

struct PoolLayout {
    int shift;
    std::size_t header;
    std::size_t stride;
};

// The pool's block 0 starts "None" then "ByteProperty". With 2-byte entries the
// second header follows the first entry at +6. With case-preserving names each
// entry opens with a 4-byte ComparisonId and entries are 4-byte aligned, so the
// second entry starts at +0x0C and its header at +0x10 (Pinball FX, read in run f2).
std::optional<PoolLayout> FindPoolLayout(const Memory& mem, std::uintptr_t block0) {
    for (const auto& [header, stride] : {std::pair<std::size_t, std::size_t>{0, 2}, {4, 4}}) {
        const std::size_t second = (header + 2 + 4 + stride - 1) / stride * stride;
        for (int shift : {6, 1}) {
            if (PoolEntryIs(mem, block0 + header, shift, "None", 4) &&
                PoolEntryIs(mem, block0 + second + header, shift, "ByteProperty", 12))
                return PoolLayout{shift, header, stride};
        }
    }
    return std::nullopt;
}

// How many block pointers in a row read as pointers. The live pool has at least
// one; a stray copy of the block-0 pointer is followed by unrelated data.
int PoolBlocksScore(const Memory& mem, std::uintptr_t blocks) {
    int run = 0;
    while (run < kMaxPoolBlocks && PlausiblePtr(mem.Ptr(blocks + run * 8ull))) ++run;
    return run;
}

// The block shift. The engine begins block 1 when the next name no longer fits
// in block 0, and ends block 0's entries with an empty header
// (FNameEntryAllocator::AllocateNewBlock), so block 0's entries run to within
// one longest entry of its end. Its end, stride << shift bytes, is the one
// that holds them that closely.
std::optional<int> PoolBlockShift(const Memory& mem, std::uintptr_t blocks, const PoolLayout& layout, std::string& why) {
    if (!PlausiblePtr(mem.Ptr(blocks + 8))) {
        why = "the name pool has one block, so how many names a block holds cannot be told yet";
        return std::nullopt;
    }
    const std::uintptr_t block0 = mem.Ptr(blocks);
    const std::size_t most = layout.stride << kMaxBlockShift;
    std::size_t used = 0;
    for (;;) {
        std::uint16_t header = 0;
        if (!mem.Get(block0 + used + layout.header, header)) {
            why = "the name pool's block 0 does not read to its end";
            return std::nullopt;
        }
        const std::size_t len = (header >> layout.shift) & kPoolLengthMask;
        if (len == 0) break;
        used = (used + layout.header + 2 + len * ((header & 1) ? 2 : 1) + layout.stride - 1) / layout.stride * layout.stride;
        if (used > most) {
            why = "the name pool's block 0 runs on past any block size";
            return std::nullopt;
        }
    }
    const std::size_t longest = layout.header + 2 + 2 * kPoolLengthMask + layout.stride;
    std::optional<int> found;
    for (int shift = kMinBlockShift; shift <= kMaxBlockShift; ++shift) {
        const std::size_t size = layout.stride << shift;
        if (used > size || size - used >= longest) continue;
        if (found) {
            why = "block 0's names fill two block sizes as closely";
            return std::nullopt;
        }
        found = shift;
    }
    if (!found) why = "block 0's names (" + std::to_string(used) + " bytes) fill no block size to within one name";
    return found;
}

std::optional<NameTable> FindPool(const Memory& mem, const ModuleMap& module, std::string& why) {
    std::optional<NameTable> best;
    int bestScore = 0;
    ForEachQword(mem, module.writable, [&](std::uintptr_t at, std::uint64_t value) {
        if (!PlausiblePtr(value)) return;
        const auto layout = FindPoolLayout(mem, static_cast<std::uintptr_t>(value));
        if (!layout) return;
        const int score = PoolBlocksScore(mem, at);
        if (score > bestScore) {
            bestScore = score;
            NameTable t{NameTableKind::Pool, at};
            t.lenShift = layout->shift;
            t.poolHeader = layout->header;
            t.poolStride = layout->stride;
            t.fnameNumber = layout->header ? 8 : 4;
            best = t;
        }
    });
    if (!best) return std::nullopt;
    const auto shift =
        PoolBlockShift(mem, best->blocks, PoolLayout{best->lenShift, best->poolHeader, best->poolStride}, why);
    if (!shift) return std::nullopt;
    best->blockShift = *shift;
    return best;
}

bool ArrayEntryIs(const Memory& mem, std::uintptr_t entry, std::size_t chars, const char* text) {
    const std::size_t len = std::strlen(text) + 1;  // the terminator is part of the match
    return PlausiblePtr(entry) && mem.BytesEqual(entry + chars, text, len);
}

std::optional<NameTable> ArrayAt(const Memory& mem, std::uintptr_t chunks) {
    const std::uintptr_t chunk0 = mem.Ptr(chunks);
    if (!PlausiblePtr(chunk0)) return std::nullopt;
    const std::uintptr_t e0 = mem.Ptr(chunk0);
    const std::uintptr_t e1 = mem.Ptr(chunk0 + 8);
    for (std::size_t chars : {0x10, 0x0C, 0x08, 0x14, 0x18}) {
        if (!ArrayEntryIs(mem, e0, chars, "None") || !ArrayEntryIs(mem, e1, chars, "ByteProperty"))
            continue;
        // NAME_INDEX is (index << 1) | wide, so name 1 stores 2.
        for (std::size_t index = 0; index + 4 <= chars; index += 4) {
            if (mem.Or0<std::uint32_t>(e0 + index) == 0 && mem.Or0<std::uint32_t>(e1 + index) == 2) {
                NameTable t{NameTableKind::Array, chunks};
                t.entryChars = chars;
                t.entryIndex = index;
                return t;
            }
        }
    }
    return std::nullopt;
}

// A chunk's length: the index the second chunk's first entry holds, which
// the first chunk's last entry is one short of.
std::optional<std::uint32_t> ArrayChunkEntries(const Memory& mem, const NameTable& t, std::string& why) {
    const std::uintptr_t chunk1 = mem.Ptr(t.blocks + 8);
    const std::uintptr_t first = PlausiblePtr(chunk1) ? mem.Ptr(chunk1) : 0;
    if (!PlausiblePtr(first)) {
        why = "the name table has one chunk, so how many names a chunk holds cannot be told yet";
        return std::nullopt;
    }
    const std::uint32_t entries = mem.Or0<std::uint32_t>(first + t.entryIndex) >> 1;
    if (entries < (1u << kMinBlockShift) || entries > (1u << kMaxBlockShift)) {
        why = "the name table's second chunk opens with name " + std::to_string(entries) + ", no chunk length";
        return std::nullopt;
    }
    const std::uintptr_t last = mem.Ptr(mem.Ptr(t.blocks) + (entries - 1) * 8ull);
    if (!PlausiblePtr(last) || mem.Or0<std::uint32_t>(last + t.entryIndex) >> 1 != entries - 1) {
        why = "the name table's first chunk does not end with name " + std::to_string(entries - 1);
        return std::nullopt;
    }
    return entries;
}

std::optional<NameTable> FindArray(const Memory& mem, const ModuleMap& module, std::string& why) {
    std::optional<NameTable> found;
    ForEachQword(mem, module.writable, [&](std::uintptr_t at, std::uint64_t value) {
        if (found) return;
        // GNames is a pointer to a heap TNameEntryArray in most 4.x builds, and
        // the array itself in a static in some.
        if (PlausiblePtr(value)) found = ArrayAt(mem, static_cast<std::uintptr_t>(value));
        if (!found) found = ArrayAt(mem, at);
    });
    if (!found) return std::nullopt;
    const auto entries = ArrayChunkEntries(mem, *found, why);
    if (!entries) return std::nullopt;
    found->chunkEntries = *entries;
    return found;
}

}  // namespace

std::optional<NameTable> FindNameTable(const Memory& mem, const ModuleMap& module, std::string& why) {
    std::string poolWhy, arrayWhy;
    if (auto pool = FindPool(mem, module, poolWhy)) return pool;
    if (auto array = FindArray(mem, module, arrayWhy)) return array;
    why = !poolWhy.empty() ? poolWhy : !arrayWhy.empty() ? arrayWhy : "no FNamePool or TNameEntryArray holds None, ByteProperty";
    return std::nullopt;
}

std::string NameString(const Memory& mem, const NameTable& table, std::uint32_t index) {
    if (table.kind == NameTableKind::Pool) {
        // The block table holds at most kMaxPoolBlocks pointers, so a garbage
        // index past it reads as no name rather than past the table.
        if ((index >> table.blockShift) >= static_cast<std::uint32_t>(kMaxPoolBlocks)) return {};
        const std::uintptr_t block = mem.Ptr(table.blocks + (index >> table.blockShift) * 8ull);
        if (!PlausiblePtr(block)) return {};
        const std::uintptr_t offset = index & ((1u << table.blockShift) - 1);
        const std::uintptr_t entry = block + offset * table.poolStride + table.poolHeader;
        std::uint16_t header = 0;
        if (!mem.Get(entry, header) || (header & 1)) return {};
        const std::size_t len = (header >> table.lenShift) & kPoolLengthMask;
        std::string out(len, '\0');
        if (len && !mem.Read(entry + 2, out.data(), len)) return {};
        return out;
    }
    const std::uintptr_t chunk = mem.Ptr(table.blocks + (index / table.chunkEntries) * 8ull);
    if (!PlausiblePtr(chunk)) return {};
    const std::uintptr_t entry = mem.Ptr(chunk + (index % table.chunkEntries) * 8ull);
    if (!PlausiblePtr(entry)) return {};
    if (mem.Or0<std::uint32_t>(entry + table.entryIndex) & 1) return {};
    return mem.Ascii(entry + table.entryChars, kMaxArrayName);
}

std::string FNameToString(const Memory& mem, const NameTable& table, std::uintptr_t fnameAddr) {
    std::uint32_t index = 0, number = 0;
    if (!mem.Get(fnameAddr, index) || !mem.Get(fnameAddr + table.fnameNumber, number)) return {};
    std::string name = NameString(mem, table, index);
    if (!name.empty() && number != 0) {
        name += '_';
        name += std::to_string(number - 1);
    }
    return name;
}

}  // namespace cameraunlock::unreal
