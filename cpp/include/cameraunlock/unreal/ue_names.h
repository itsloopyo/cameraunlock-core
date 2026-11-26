// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <cameraunlock/memory/memory_view.h>

// The engine's FName table, found by what it holds rather than by where a
// particular build put it.
//
// Every Unreal build registers "None" as name 0 and "ByteProperty" as name 1,
// so the table is the structure in the module's writable data whose first two
// entries decode as exactly those strings. Two layouts exist:
//
//   Pool  (UE 4.23 onward): FNamePool. A block-pointer array whose block 0
//         starts with packed entries: a 16-bit header holding the length, then
//         the characters. An index is (block << shift) | (byte offset / stride),
//         the shift read off how full block 0 was when block 1 was begun.
//         Most builds pack entries on 2 bytes. A build with case-preserving
//         names (Pinball FX) puts a 4-byte ComparisonId before each header,
//         packs on 4 bytes, and its FName carries a DisplayIndex too, so an
//         FName is 12 bytes with the Number at +8.
//   Array (before 4.23): TNameEntryArray. Chunks of FNameEntry pointers;
//         each entry holds its index and the characters at a fixed offset, so
//         the chunk's length is the index of the second chunk's first entry.
namespace cameraunlock::unreal {

enum class NameTableKind { Pool, Array };

struct NameTable {
    NameTableKind kind = NameTableKind::Pool;

    // Pool: the address of Blocks[0] (the pool's block-pointer array).
    // Array: the address of Chunks[0].
    std::uintptr_t blocks = 0;

    // Pool: the bits of an index that are the byte offset in its block, over the stride.
    int blockShift = 0;
    // Array: the entries in a chunk.
    std::uint32_t chunkEntries = 0;

    // Pool: header bit position of the length field: 6 (10 bits under a probe hash),
    // or 1 (15 bits, as case-preserving builds have it). Bit 0 is the wide flag.
    int lenShift = 6;

    // Pool: where an entry's header sits in it, and the entry alignment an
    // index counts in. 0 and 2, or 4 and 4 with case-preserving names.
    std::size_t poolHeader = 0;
    std::size_t poolStride = 2;

    // Where an FName's Number sits: 4, or 8 when a DisplayIndex follows the
    // ComparisonIndex, as a UE 4.27 build with case-preserving names lays it
    // out (Pinball FX, run f2). No UE5 build with case-preserving names has
    // been read.
    std::size_t fnameNumber = 4;

    // Array: offset of the characters inside an FNameEntry, and of its index
    // field, whose low bit is the wide flag.
    std::size_t entryChars = 0x10;
    std::size_t entryIndex = 0x08;
};

// Nothing, with `why`, when no table is found or how its blocks or chunks
// split an index cannot be told yet.
std::optional<NameTable> FindNameTable(const memory::Memory& mem, const memory::ModuleMap& module, std::string& why);

// The bytes an FName takes in an object or a struct: 8, or 12 with case-preserving names.
inline std::size_t FNameSize(const NameTable& t) { return t.fnameNumber + 4; }

// The string for an FName ComparisonIndex. Empty when the entry does not read or
// is a wide (UTF-16) name, which no engine type or property a consumer looks
// up ever is.
std::string NameString(const memory::Memory& mem, const NameTable& table, std::uint32_t index);

// An FName as the engine prints it: the base string, then "_<Number-1>" when the
// instance number is non-zero.
std::string FNameToString(const memory::Memory& mem, const NameTable& table, std::uintptr_t fnameAddr);

}  // namespace cameraunlock::unreal
