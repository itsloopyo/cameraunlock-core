// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <cameraunlock/diagnostics/fail.h>
#include <cameraunlock/memory/pe_image.h>
#include <cameraunlock/memory/x86_decode.h>

// The code of an x64 MSVC exe as runtime discovery walks it: every function
// chunk from the unwind table, every rel32 call or jump and every RIP-relative
// operand by the address it reaches, each function decoded with its jump
// tables told apart from its code, and a class's primary vtable found from its
// type descriptor. Read from a pe::Image, live or offline alike. In
// cameraunlock_hooks, beside the decoder it is built on.
namespace cameraunlock::memory::x64 {

// One unwind table entry, with the start of the function it belongs to.
struct Chunk {
    std::uint32_t begin;
    std::uint32_t end;
    std::uint32_t unwind;
    std::uint32_t primary;
};

struct Ref {
    std::uint32_t target;
    std::uint32_t site;  // the instruction's RVA
    bool operator<(const Ref& o) const { return target < o.target || (target == o.target && site < o.site); }
};

using Insns = std::vector<Insn>;

class Analysis {
public:
    // Reads every unwind table entry that lies in .text, in the file's order,
    // and decodes each. Throws std::runtime_error when the image is not x64,
    // its exception directory is empty, not whole entries or not sorted, or no
    // entry lies in .text.
    explicit Analysis(const pe::Image& img);

    const Chunk* ChunkAt(std::uint32_t rva) const;
    // The start of the function `rva` is in, 0 when no chunk holds it.
    std::uint32_t PrimaryOf(std::uint32_t rva) const;
    bool IsFunctionStart(std::uint32_t rva) const;
    // Whether a chunk names `rva` as its function's start, as a chained chunk
    // does even when its parent has no entry of its own. Unlike
    // pe::Image::RootOf, a chunk's start is read from the chained entry
    // itself, so such a chunk still has one.
    bool IsPrimary(std::uint32_t rva) const { return byPrimary_.count(rva) != 0; }

    std::vector<Ref> RelTo(std::uint32_t target) const { return Range(rel_, target); }
    std::vector<Ref> RipTo(std::uint32_t target) const { return Range(rip_, target); }

    // E8 / E9 rel32 byte patterns anywhere in .text landing on `target`. A
    // chunk's decode stops at the first instruction the decoder cannot length
    // (EVEX) or at a jump table inside it, so a call after either is missing
    // from RelTo; a larger count here means RelTo cannot vouch for a count.
    std::size_t RawRelCount(std::uint32_t target) const;

    // All of a function's chunks decoded in address order.
    const Insns& Function(std::uint32_t primary) const;
    // Which of the function's decoded instructions are code, and the case
    // RVAs of each of its jump tables, by the table's RVA. MSVC puts a
    // switch's tables after the code of the chunk that uses them and reads
    // them as [base + index*4 + table], RVAs of the cases, or with movzx
    // [base + index + table], a byte table of indexes into one; the decode
    // runs on through them as if they were code. False when a chunk's decode
    // does not end exactly where its tables start, or at its end, or a case is
    // not an instruction of its code.
    bool Code(std::uint32_t primary, std::vector<bool>& code, std::map<std::uint32_t, std::vector<std::uint32_t>>& tables) const;
    // A leaf with no unwind data, decoded up to its first ret or direct jmp.
    Insns Leaf(std::uint32_t rva, std::size_t maxInsns = 16) const;
    // The function's chunks if any chunk names it its start, else its leaf decode.
    const Insns& Body(std::uint32_t rva) const;

    // The index of the instruction at `rva` in `fn`, SIZE_MAX when none starts there.
    static std::size_t IndexOf(const Insns& fn, std::uint32_t rva);

private:
    static std::vector<Ref> Range(const std::vector<Ref>& v, std::uint32_t target);
    std::uint32_t ResolvePrimary(const Chunk& c) const;

    const pe::Image& img_;
    std::vector<Chunk> chunks_;
    std::multimap<std::uint32_t, std::size_t> byPrimary_;
    std::vector<Ref> rel_;
    std::vector<Ref> rip_;
    std::vector<std::uint32_t> rawRel_;
    mutable std::unordered_map<std::uint32_t, Insns> fnCache_;
    mutable std::unordered_map<std::uint32_t, Insns> leafCache_;
};

// MSVC's type descriptor name for a class: "romu::CarCamera" is
// ".?AVCarCamera@romu@@". Throws std::invalid_argument on an empty name or component.
std::string MangledClass(std::string_view cxxName);

// The one primary vtable (complete object locator at offset 0) of the class
// whose type descriptor carries `mangled`, or false with the reason. The name
// is looked for in .data and .rdata, and the vtable is the slot after each
// pointer to the locator, whatever that slot holds; pe::VtableOf instead
// reads every class's RTTI, the names in .data only, and takes a vtable only
// where its first slot is code.
bool FindVtable(const pe::Image& img, const std::string& mangled, std::uint32_t& out, diagnostics::Fail& fail);

// Instruction shapes discovery steps match on.
namespace insn {
constexpr int kRax = 0, kRcx = 1, kRdx = 2, kRsp = 4;
bool IsCall(const Insn& in);
bool IsRet(const Insn& in);
bool IsLea(const Insn& in, int dst);
bool IsMovRegFromMem(const Insn& in, int dst);
bool IsMovRegReg(const Insn& in, int dst, int src);
int RegRegDst(const Insn& in);
// A legacy 0F opcode, not one of the 0F 38 or 0F 3A maps or a VEX one.
bool IsTwoByte(const Insn& in, std::uint8_t op2);
bool IsMovDwordImm(const Insn& in);
bool IsCmpDwordImm8(const Insn& in);
bool IsMovEcxImm(const Insn& in);
// Whether `in` may leave `reg` holding something else: a write to any part
// of it, conditional or not. A call may change the registers the Windows x64
// ABI lets a callee change. An instruction it does not know changes them all.
bool Clobbers(const Insn& in, int reg);
// mov rax, [rip + static] ; ret
bool LeafLoadsStatic(const Insns& leaf, std::uint32_t& staticRva);
// mov r(e)ax, [rcx + field] ; ret, of the given width, or the same read
// through an unoptimised spill of rcx:
//   mov [rsp+d], rcx ; mov rax, [rsp+d] ; mov r(e)ax, [rax + field] ; ret
bool LeafReadsField(const Insns& leaf, bool qword, std::uint32_t& field);
}  // namespace insn

}  // namespace cameraunlock::memory::x64
