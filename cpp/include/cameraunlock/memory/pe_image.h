// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// A PE image (x86 or x64, whatever this build is) as the loader lays it out,
// read where it is loaded (in the game) or from a copy laid out the same way (an
// offline scan's mapped exe, a saved memory image, a test's hand-made image).
// Everything is in RVAs, so the two read the same; absolute pointers in the
// image are relative to PointerBase: the load address in the game, the
// header's ImageBase in a copy whose relocations were never applied.
//
// Portable: the headers are parsed from bytes, with no Windows header.
namespace cameraunlock::memory::pe {

struct Section {
    std::string name;
    std::uint32_t rva = 0, size = 0;  // size: the header's VirtualSize
    std::uint32_t characteristics = 0;

    bool Contains(std::uint64_t at, std::uint64_t len = 1) const { return at >= rva && at + len <= std::uint64_t{rva} + size; }
    bool Executable() const { return (characteristics & 0x20000000u) != 0; }
    bool Writable() const { return (characteristics & 0x80000000u) != 0; }
};

class Image {
public:
    // The image whose headers start at `at`, trusted to hold SizeOfImage bytes
    // (a loaded module, or a buffer sized from its headers). Throws when the
    // headers are not an x86 or x64 PE.
    Image(const std::uint8_t* at, std::uint64_t pointerBase);
    // The same over `size` bytes; also throws when SizeOfImage is larger.
    Image(const std::uint8_t* data, std::size_t size, std::uint64_t pointerBase);

    bool Is64() const { return is64_; }
    unsigned PointerSize() const { return is64_ ? 8u : 4u; }

    // Unchecked, as a scan over a section reads from it.
    const std::uint8_t* At(std::uint32_t rva) const { return base_ + rva; }
    std::uint32_t SizeOfImage() const { return size_; }
    // Whether the `len` bytes at `rva` all lie inside the image.
    bool Has(std::uint64_t rva, std::uint64_t len) const { return rva + len <= size_ && rva + len >= rva; }
    // The file header's TimeDateStamp, which tells one build of an exe from another.
    std::uint32_t TimeDateStamp() const { return timeDateStamp_; }
    std::uint64_t PointerBase() const { return pointerBase_; }
    // The optional header's ImageBase: where the image's own pointers point
    // before relocation.
    std::uint64_t HeaderImageBase() const { return headerImageBase_; }

    // Each throws std::out_of_range for a read past the image.
    std::uint8_t U8(std::uint32_t rva) const;
    std::uint16_t U16(std::uint32_t rva) const;
    std::uint32_t U32(std::uint32_t rva) const;
    std::int32_t I32(std::uint32_t rva) const { return static_cast<std::int32_t>(U32(rva)); }
    std::uint64_t U64(std::uint32_t rva) const;
    // An absolute pointer of the image's own width.
    std::uint64_t Pointer(std::uint32_t rva) const { return is64_ ? U64(rva) : U32(rva); }

    // The RVA an absolute pointer in the image points at, or nothing when it
    // points outside it.
    std::optional<std::uint32_t> RvaOf(std::uint64_t pointer) const;
    // The absolute address of an RVA, as the image's own code names it.
    std::uint64_t PointerOf(std::uint32_t rva) const { return pointerBase_ + rva; }

    const std::vector<Section>& Sections() const { return sections_; }
    // The first section called `name`, or null.
    const Section* SectionNamed(const std::string& name) const;
    // .text, .rdata and .data; each throws std::runtime_error when the image lacks it.
    const Section& Text() const { return Required(".text"); }
    const Section& Rdata() const { return Required(".rdata"); }
    const Section& Data() const { return Required(".data"); }
    // The import directory, from the optional header; size 0 when there is none.
    const Section& Imports() const { return imports_; }
    // The exception directory (.pdata's RUNTIME_FUNCTION entries, 12 bytes
    // each, as the file orders them), from the optional header; size 0 when
    // there is none.
    const Section& Exceptions() const { return exceptions_; }

    // Whether the image has an exception directory (.pdata): x64 images do.
    bool HasUnwindTable() const { return !functions_.empty(); }
    // The unwind entry `rva` is in: begin inclusive, end exclusive; {0, 0}
    // where none covers it. Throws when the image has no unwind table.
    std::pair<std::uint32_t, std::uint32_t> FunctionAt(std::uint32_t rva) const;
    // The start of the function `rva` is in, following chained entries (a
    // compiler that splits a function into hot and cold parts chains the later
    // ones to the first). 0 when no entry covers it. Throws when the image has
    // no unwind table.
    std::uint32_t RootOf(std::uint32_t rva) const;
    // Every unwind entry of the function starting at `root`, by address.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> Parts(std::uint32_t root) const;

    // Every RVA in `section` where the `n` bytes at `needle` occur, at an RVA
    // that is a multiple of `align`, in address order. Throws
    // std::invalid_argument for no bytes or an alignment of 0, and
    // std::out_of_range for a section that runs past the image.
    std::vector<std::uint32_t> Find(const Section& section, const void* needle, std::size_t n, std::uint32_t align = 1) const;

private:
    void ReadHeaders(std::size_t span);
    const Section& Required(const char* name) const;
    struct Function {
        std::uint32_t begin, end, unwind;
    };
    const Function* EntryAt(std::uint32_t rva) const;
    std::uint32_t RootOfEntry(const Function* e) const;

    const std::uint8_t* base_;
    std::uint64_t pointerBase_;
    std::uint64_t headerImageBase_ = 0;
    std::uint32_t size_ = 0;
    std::uint32_t timeDateStamp_ = 0;
    bool is64_ = false;
    std::vector<Section> sections_;
    Section imports_;
    Section exceptions_;
    std::vector<Function> functions_;  // by begin
};

// A byte pattern written as hex pairs, "??" for any byte: "8B 41 ?? 85 C0".
// Throws std::invalid_argument for half a byte, a byte that is not two hex
// digits or ?? (a sign or whitespace inside one included), or a
// pattern that does not start with a known byte.
class Pattern {
public:
    explicit Pattern(const char* text);
    std::size_t Size() const { return bytes_.size(); }
    bool At(const std::uint8_t* p) const;

private:
    std::vector<std::int16_t> bytes_;  // -1 for any
};

// Where `pattern` first occurs in [from, from + span), or nothing.
std::optional<std::uint32_t> Find(const Image& image, std::uint32_t from, std::uint32_t span, const Pattern& pattern);
// Every place it occurs in [from, from + span).
std::vector<std::uint32_t> FindAll(const Image& image, std::uint32_t from, std::uint32_t span, const Pattern& pattern);
// Where it last occurs in [from, from + span), or nothing.
std::optional<std::uint32_t> FindLast(const Image& image, std::uint32_t from, std::uint32_t span, const Pattern& pattern);

// The target of the rel32 operand whose four bytes start at `operand` (a call,
// a jmp, or an x64 RIP-relative memory operand with no immediate after it).
std::uint32_t Rel32(const Image& image, std::uint32_t operand);
// A function reached through any chain of `jmp rel32` thunks, up to 8 of them.
// A jmp leaving the image ends the chain: its target is returned as it is.
std::uint32_t Through(const Image& image, std::uint32_t function);

// Every `call rel32` in .text that lands on `target`: the RVA of each call.
// Every E8 byte is read as a call.
std::vector<std::uint32_t> CallsTo(const Image& image, std::uint32_t target);
// The target of every `call rel32` in .text that lands in [from, from + span),
// each once, in address order.
std::vector<std::uint32_t> CallTargetsIn(const Image& image, std::uint32_t from, std::uint32_t span);

// Every `push imm32` in .text whose immediate is the absolute address of `rva`:
// the RVA of each push. x86 images only; throws std::invalid_argument for x64.
std::vector<std::uint32_t> Pushes(const Image& image, std::uint32_t rva);

// Where a string may start for UniqueString: straight after a NUL, or also on
// a four-byte boundary (an x86 compiler puts a string straight after the data
// before it when that ends aligned; Titanium's engine exe).
enum class StringStart { AfterNul, AfterNulOrAligned4 };
// The RVA of the NUL-terminated ASCII string `text` in .rdata, or nothing when
// there is not exactly one starting where `start` allows.
std::optional<std::uint32_t> UniqueString(const Image& image, const std::string& text,
                                          StringStart start = StringStart::AfterNul);

// The RVA of the import address table slot the loader fills with `function`
// from `dll` (case does not matter in the DLL's name), or nothing when the
// image does not import it by name.
std::optional<std::uint32_t> ImportSlot(const Image& image, const std::string& dll, const std::string& function);

// The start of the function holding `inside` in an image with no unwind table:
// the first byte after the int3 padding the compiler put before it (two or
// more, or one after a ret), looked for up to `reach` bytes back. Throws when
// there is none that near, or `inside` is not in .text.
std::uint32_t FunctionStart(const Image& image, std::uint32_t inside, std::uint32_t reach);

}  // namespace cameraunlock::memory::pe
