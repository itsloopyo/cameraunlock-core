// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/memory/x64_analysis.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <set>
#include <stdexcept>

namespace cameraunlock::memory::x64 {
namespace {

constexpr std::uint32_t kRuntimeFunctionSize = 12;  // one .pdata entry: begin, end, unwind

}  // namespace

Analysis::Analysis(const pe::Image& img) : img_(img) {
    if (!img_.Is64()) throw std::runtime_error("x64::Analysis reads an x64 image, and this one is x86");
    const pe::Section& pd = img_.Exceptions();
    if (pd.size == 0 || pd.size % kRuntimeFunctionSize)
        throw std::runtime_error("x64::Analysis: the image's exception directory is empty or not whole entries");
    const pe::Section& text = img_.Text();
    for (std::uint32_t a = pd.rva; a < pd.rva + pd.size; a += kRuntimeFunctionSize) {
        const Chunk c{img_.U32(a), img_.U32(a + 4), img_.U32(a + 8), 0};
        if (c.begin == 0) continue;
        if (c.end <= c.begin || !text.Contains(c.begin, c.end - c.begin)) continue;
        if (!chunks_.empty() && c.begin < chunks_.back().end)
            throw std::runtime_error("x64::Analysis: the image's exception directory is not sorted");
        chunks_.push_back(c);
    }
    if (chunks_.empty()) throw std::runtime_error("x64::Analysis: no entry of the image's exception directory lies in .text");
    for (Chunk& c : chunks_) c.primary = ResolvePrimary(c);

    for (std::size_t i = 0; i < chunks_.size(); ++i) {
        const Chunk& c = chunks_[i];
        std::uint32_t at = c.begin;
        while (at < c.end) {
            Insn in;
            if (!DecodeX64(img_.At(at), c.end - at, at, in)) break;
            if (in.relative && in.len == 5 && (in.opcode == 0xE8 || in.opcode == 0xE9)) rel_.push_back({in.target, in.rva});
            if (in.IsRipRelative()) rip_.push_back({in.RipTarget(), in.rva});
            at += in.len;
        }
        byPrimary_.emplace(c.primary, i);
    }
    std::sort(rel_.begin(), rel_.end());
    std::sort(rip_.begin(), rip_.end());

    for (std::uint32_t at = text.rva; at + 5 <= text.rva + text.size; ++at) {
        const std::uint8_t op = img_.U8(at);
        if (op != 0xE8 && op != 0xE9) continue;
        const auto rel = static_cast<std::int32_t>(img_.U32(at + 1));
        rawRel_.push_back(static_cast<std::uint32_t>(static_cast<std::int64_t>(at) + 5 + rel));
    }
    std::sort(rawRel_.begin(), rawRel_.end());
}

const Chunk* Analysis::ChunkAt(std::uint32_t rva) const {
    auto it = std::upper_bound(chunks_.begin(), chunks_.end(), rva,
                               [](std::uint32_t r, const Chunk& c) { return r < c.begin; });
    if (it == chunks_.begin()) return nullptr;
    --it;
    return rva < it->end ? &*it : nullptr;
}

std::uint32_t Analysis::PrimaryOf(std::uint32_t rva) const {
    const Chunk* c = ChunkAt(rva);
    return c ? c->primary : 0;
}

bool Analysis::IsFunctionStart(std::uint32_t rva) const {
    const Chunk* c = ChunkAt(rva);
    return c && c->begin == rva && c->primary == rva;
}

std::size_t Analysis::RawRelCount(std::uint32_t target) const {
    const auto [lo, hi] = std::equal_range(rawRel_.begin(), rawRel_.end(), target);
    return static_cast<std::size_t>(hi - lo);
}

const Insns& Analysis::Function(std::uint32_t primary) const {
    auto cached = fnCache_.find(primary);
    if (cached != fnCache_.end()) return cached->second;
    Insns& out = fnCache_[primary];
    std::vector<std::size_t> idx;
    auto [lo, hi] = byPrimary_.equal_range(primary);
    for (auto it = lo; it != hi; ++it) idx.push_back(it->second);
    std::sort(idx.begin(), idx.end());
    for (std::size_t i : idx) {
        const Chunk& c = chunks_[i];
        for (std::uint32_t at = c.begin; at < c.end;) {
            Insn in;
            if (!DecodeX64(img_.At(at), c.end - at, at, in)) break;
            out.push_back(in);
            at += in.len;
        }
    }
    return out;
}

bool Analysis::Code(std::uint32_t primary, std::vector<bool>& code, std::map<std::uint32_t, std::vector<std::uint32_t>>& tables) const {
    const Insns& body = Function(primary);
    code.assign(body.size(), true);
    tables.clear();
    std::set<std::uint32_t> words, bytes;
    for (const Insn& x : body) {
        if (!x.HasMemory() || x.base < 0 || x.base == Insn::kRip || x.index == Insn::kNone || x.disp <= 0) continue;
        const auto at = static_cast<std::uint32_t>(x.disp);
        if (PrimaryOf(at) != primary) continue;
        if (x.scale == 4) words.insert(at);
        else if (x.scale == 1 && insn::IsTwoByte(x, 0xB6)) bytes.insert(at);
    }
    std::set<std::uint32_t> starts = words;
    starts.insert(bytes.begin(), bytes.end());
    auto [lo, hi] = byPrimary_.equal_range(primary);
    for (auto it = lo; it != hi; ++it) {
        const Chunk& c = chunks_[it->second];
        const auto first = starts.lower_bound(c.begin);
        const std::uint32_t codeEnd = first != starts.end() && *first < c.end ? *first : c.end;
        std::uint32_t reached = c.begin;
        for (std::size_t k = 0; k < body.size(); ++k) {
            if (body[k].rva < c.begin || body[k].rva >= c.end) continue;
            if (body[k].rva >= codeEnd) code[k] = false;
            else reached = body[k].rva + body[k].len;
        }
        if (reached != codeEnd) return false;
        for (auto t = first; t != starts.end() && *t < c.end; ++t) {
            const std::uint32_t end = std::next(t) != starts.end() && *std::next(t) < c.end ? *std::next(t) : c.end;
            if (!words.count(*t)) continue;
            if ((end - *t) % 4 != 0) return false;
            std::vector<std::uint32_t>& cases = tables[*t];
            for (std::uint32_t a = *t; a < end; a += 4) cases.push_back(img_.U32(a));
        }
    }
    for (const auto& [table, cases] : tables)
        for (std::uint32_t target : cases) {
            const std::size_t k = IndexOf(body, target);
            if (k == SIZE_MAX || !code[k]) return false;
        }
    return true;
}

Insns Analysis::Leaf(std::uint32_t rva, std::size_t maxInsns) const {
    Insns out;
    const pe::Section& t = img_.Text();
    for (std::uint32_t at = rva; t.Contains(at) && out.size() < maxInsns;) {
        Insn in;
        if (!DecodeX64(img_.At(at), t.rva + t.size - at, at, in)) break;
        out.push_back(in);
        at += in.len;
        if (in.opcode == 0xC3 || in.opcode == 0xE9 || in.opcode == 0xEB) break;
    }
    return out;
}

const Insns& Analysis::Body(std::uint32_t rva) const {
    if (IsPrimary(rva)) return Function(rva);
    auto cached = leafCache_.find(rva);
    if (cached == leafCache_.end()) cached = leafCache_.emplace(rva, Leaf(rva, 64)).first;
    return cached->second;
}

std::size_t Analysis::IndexOf(const Insns& fn, std::uint32_t rva) {
    for (std::size_t i = 0; i < fn.size(); ++i)
        if (fn[i].rva == rva) return i;
    return SIZE_MAX;
}

std::vector<Ref> Analysis::Range(const std::vector<Ref>& v, std::uint32_t target) {
    auto lo = std::lower_bound(v.begin(), v.end(), Ref{target, 0});
    std::vector<Ref> out;
    for (auto it = lo; it != v.end() && it->target == target; ++it) out.push_back(*it);
    return out;
}

// Follows UNW_FLAG_CHAININFO back to the entry that owns the function.
std::uint32_t Analysis::ResolvePrimary(const Chunk& c) const {
    std::uint32_t begin = c.begin, unwind = c.unwind;
    for (int depth = 0; depth < 32; ++depth) {
        if (!img_.Has(unwind, 4)) return begin;
        const std::uint8_t flags = img_.U8(unwind) >> 3;
        if (!(flags & 4)) return begin;
        const std::uint8_t count = img_.U8(unwind + 2);
        const std::uint32_t chained = unwind + 4 + ((count + 1u) & ~1u) * 2;
        if (!img_.Has(chained, kRuntimeFunctionSize)) return begin;
        begin = img_.U32(chained);
        unwind = img_.U32(chained + 8);
    }
    return begin;
}

std::string MangledClass(std::string_view cxxName) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t sep = cxxName.find("::", start);
        parts.push_back(cxxName.substr(start, sep == std::string_view::npos ? std::string_view::npos : sep - start));
        if (sep == std::string_view::npos) break;
        start = sep + 2;
    }
    std::string out = ".?AV";
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (it->empty()) throw std::invalid_argument("class name '" + std::string(cxxName) + "' has an empty part");
        out.append(*it);
        out += '@';
    }
    out += '@';
    return out;
}

bool FindVtable(const pe::Image& img, const std::string& mangled, std::uint32_t& out, diagnostics::Fail& fail) {
    const std::size_t n = mangled.size() + 1;
    std::vector<std::uint32_t> names = img.Find(img.Data(), mangled.c_str(), n);
    for (std::uint32_t r : img.Find(img.Rdata(), mangled.c_str(), n)) names.push_back(r);
    std::vector<std::uint32_t> vtables;
    for (std::uint32_t name : names) {
        const std::uint32_t td = name - 16;
        for (std::uint32_t hit : img.Find(img.Rdata(), &td, 4, 4)) {
            const std::uint32_t col = hit - 12;
            if (!img.Rdata().Contains(col, 24)) continue;
            if (img.U32(col) != 1 || img.U32(col + 4) != 0 || img.U32(col + 20) != col) continue;
            const std::uint64_t colPtr = img.PointerBase() + col;
            for (std::uint32_t meta : img.Find(img.Rdata(), &colPtr, 8, 8)) vtables.push_back(meta + 8);
        }
    }
    if (vtables.size() != 1) return fail("rtti: expected one primary vtable for %s, found %zu", mangled.c_str(), vtables.size());
    out = vtables[0];
    return true;
}

namespace insn {

bool IsCall(const Insn& in) { return in.opcode == 0xE8 && in.relative; }
bool IsRet(const Insn& in) { return in.opcode == 0xC3; }
bool IsLea(const Insn& in, int dst) { return in.opcode == 0x8D && in.rex_w && in.reg == dst && in.HasMemory(); }
bool IsMovRegFromMem(const Insn& in, int dst) { return in.opcode == 0x8B && in.rex_w && in.reg == dst && in.HasMemory(); }

bool IsMovRegReg(const Insn& in, int dst, int src) {
    if (!in.has_modrm || in.mod != 3 || !in.rex_w) return false;
    if (in.opcode == 0x8B) return in.reg == dst && in.rm == src;
    if (in.opcode == 0x89) return in.rm == dst && in.reg == src;
    return false;
}

int RegRegDst(const Insn& in) {
    if (!in.has_modrm || in.mod != 3) return Insn::kNone;
    if (in.opcode == 0x8B) return in.reg;
    if (in.opcode == 0x89) return in.rm;
    return Insn::kNone;
}

bool IsTwoByte(const Insn& in, std::uint8_t op2) { return in.map == 1 && !in.vex && in.opcode2 == op2; }

bool IsMovDwordImm(const Insn& in) {
    return in.opcode == 0xC7 && !in.rex_w && !in.opsize16 && in.reg == 0 && in.HasMemory() && in.has_imm;
}
bool IsCmpDwordImm8(const Insn& in) {
    return in.opcode == 0x83 && !in.rex_w && !in.opsize16 && in.reg == 7 && in.HasMemory() && in.has_imm;
}
bool IsMovEcxImm(const Insn& in) { return in.opcode == 0xB9 && !in.rex_w && !in.rex_b && !in.opsize16 && in.has_imm; }

namespace {

using Regs = std::uint32_t;  // one bit per general purpose register, in encoding order
constexpr int kRbx = 3, kRbp = 5, kRsi = 6, kRdi = 7, kR8 = 8;
constexpr Regs Bit(int r) { return Regs{1} << r; }
constexpr Regs kEvery = 0xFFFF;
// What a call may leave changed under the Windows x64 ABI, which has the
// callee keep rbx, rbp, rsi, rdi, rsp and r12 to r15.
constexpr Regs kCallerSaved = Bit(kRax) | Bit(kRcx) | Bit(kRdx) | Bit(8) | Bit(9) | Bit(10) | Bit(11);

struct Operands {
    const Insn& in;
    int Ext() const { return in.reg & 7; }  // ModRM.reg as a group opcode's extension
    bool Direct() const { return in.has_modrm && in.mod == 3; }
    // A byte register 4 to 7 with no REX prefix is ah, ch, dh or bh.
    Regs Byte(int r) const { return Bit(!in.rex && r >= 4 && r < 8 ? r - 4 : r); }
    Regs Reg() const { return Bit(in.reg); }
    Regs Reg8() const { return Byte(in.reg); }
    Regs Rm() const { return Direct() ? Bit(in.rm) : 0; }
    Regs Rm8() const { return Direct() ? Byte(in.rm) : 0; }
    int Embedded(std::uint8_t op) const { return (op & 7) | (in.rex_b ? 8 : 0); }  // B8+r, 58+r, 0F C8+r
};

struct Span {
    std::uint8_t lo, hi;
};
bool In(std::uint8_t op, std::initializer_list<Span> spans) {
    for (const Span& s : spans)
        if (op >= s.lo && op <= s.hi) return true;
    return false;
}

// 0F 71 to 73, legacy or VEX: shifts of a vector register by an immediate,
// in register forms only.
Regs ShiftByImmediate(const Operands& o) {
    const int e = o.Ext();
    const bool known = o.in.opcode2 == 0x73 ? e == 2 || e == 3 || e == 6 || e == 7 : e == 2 || e == 4 || e == 6;
    return o.Direct() && known ? 0 : kEvery;
}

Regs OneByteWrites(const Operands& o) {
    const Insn& in = o.in;
    const std::uint8_t op = in.opcode;
    if (op < 0x40) {
        if ((op & 7) >= 6) return kEvery;  // a prefix, or no instruction in 64-bit mode
        if (op >= 0x38) return 0;          // cmp
        const bool byte = (op & 1) == 0;
        if ((op & 7) < 2) return byte ? o.Rm8() : o.Rm();
        if ((op & 7) < 4) return byte ? o.Reg8() : o.Reg();
        return Bit(kRax);
    }
    if (op >= 0x50 && op <= 0x57) return Bit(kRsp);
    if (op >= 0x58 && op <= 0x5F) return Bit(o.Embedded(op)) | Bit(kRsp);
    if (op >= 0x70 && op <= 0x7F) return 0;
    if (op >= 0x91 && op <= 0x97) return Bit(kRax) | Bit(o.Embedded(op));
    if (op >= 0xB0 && op <= 0xB7) return o.Byte(o.Embedded(op));
    if (op >= 0xB8 && op <= 0xBF) return Bit(o.Embedded(op));
    if (op >= 0xD8 && op <= 0xDF) return op == 0xDF && o.Direct() && o.Ext() == 4 ? Bit(kRax) : 0;  // x87; fnstsw ax
    switch (op) {
        case 0x63: case 0x69: case 0x6B: case 0x8B: case 0x8D: return o.Reg();
        case 0x8A: return o.Reg8();
        case 0x88: return o.Rm8();
        case 0x89: case 0x8C: return o.Rm();
        case 0x86: return o.Reg8() | o.Rm8();
        case 0x87: return o.Reg() | o.Rm();
        case 0x84: case 0x85: case 0x8E: case 0x9B: case 0x9E: case 0xA2: case 0xA3: case 0xA8: case 0xA9:
        case 0xE3: case 0xE6: case 0xE7: case 0xE9: case 0xEB: case 0xEE: case 0xEF:
        case 0xF5: case 0xF8: case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0xFD: return 0;
        case 0x68: case 0x6A: case 0x9C: case 0x9D: case 0xC2: case 0xC3: case 0xCA: case 0xCB: return Bit(kRsp);
        case 0x98: case 0x9F: case 0xA0: case 0xA1: case 0xD7: case 0xE4: case 0xE5: case 0xEC: case 0xED: return Bit(kRax);
        case 0x99: return Bit(kRdx);
        case 0xE0: case 0xE1: case 0xE2: return Bit(kRcx);  // loop
        // String instructions, with rcx for a rep prefix.
        case 0x6C: case 0x6D: case 0xAA: case 0xAB: case 0xAE: case 0xAF: return Bit(kRdi) | Bit(kRcx);
        case 0x6E: case 0x6F: return Bit(kRsi) | Bit(kRcx);
        case 0xA4: case 0xA5: case 0xA6: case 0xA7: return Bit(kRsi) | Bit(kRdi) | Bit(kRcx);
        case 0xAC: case 0xAD: return Bit(kRax) | Bit(kRsi) | Bit(kRcx);
        case 0x80: return o.Ext() == 7 ? 0 : o.Rm8();
        case 0x81: case 0x83: return o.Ext() == 7 ? 0 : o.Rm();
        case 0x8F: return o.Ext() == 0 ? o.Rm() | Bit(kRsp) : kEvery;
        case 0x90: return in.rex_b ? Bit(kRax) | Bit(kR8) : 0;  // xchg r8, rax; else nop or pause
        case 0xC0: case 0xD0: case 0xD2: return o.Rm8();
        case 0xC1: case 0xD1: case 0xD3: return o.Rm();
        case 0xC6: return o.Ext() == 0 ? o.Rm8() : kEvery;
        case 0xC7: return o.Ext() == 0 ? o.Rm() : kEvery;
        case 0xC8: case 0xC9: return Bit(kRsp) | Bit(kRbp);
        case 0xE8: return kCallerSaved;
        case 0xF6: case 0xF7:
            if (o.Ext() < 2) return 0;                                  // test
            if (o.Ext() < 4) return op == 0xF6 ? o.Rm8() : o.Rm();      // not, neg
            return op == 0xF6 ? Bit(kRax) : Bit(kRax) | Bit(kRdx);      // mul, imul, div, idiv
        case 0xFE: return o.Ext() < 2 ? o.Rm8() : kEvery;
        case 0xFF:
            if (o.Ext() < 2) return o.Rm();        // inc, dec
            if (o.Ext() < 4) return kCallerSaved;  // call
            if (o.Ext() < 6) return 0;             // jmp
            return o.Ext() == 6 ? Bit(kRsp) : kEvery;
        default: return kEvery;
    }
}

Regs TwoByteWrites(const Operands& o) {
    const Insn& in = o.in;
    const std::uint8_t op = in.opcode2;
    if (op >= 0x10 && op <= 0x1F)  // SSE moves, prefetches and hint nops; F3 0F 1E /1 is rdssp
        return op == 0x1E && in.rep == 0xF3 && o.Direct() && o.Ext() == 1 ? o.Rm() : 0;
    if (op >= 0x40 && op <= 0x4F) return o.Reg();  // cmov
    if (op >= 0x71 && op <= 0x73) return ShiftByImmediate(o);
    if (In(op, {{0x51, 0x70}, {0x74, 0x77}, {0x7C, 0x7D}, {0x7F, 0x7F}})) return 0;
    if (op >= 0x80 && op <= 0x8F) return 0;          // jcc
    if (op >= 0x90 && op <= 0x9F) return o.Rm8();    // setcc
    if (op >= 0xC8 && op <= 0xCF) return Bit(o.Embedded(op));  // bswap
    if (op >= 0xD0 && op != 0xD7 && op != 0xFF) return 0;
    switch (op) {
        case 0x02: case 0x03: case 0x2C: case 0x2D: case 0x50: case 0xAF: case 0xB2: case 0xB4: case 0xB5:
        case 0xB6: case 0xB7: case 0xB8: case 0xBC: case 0xBD: case 0xBE: case 0xBF: case 0xC5: case 0xD7: return o.Reg();
        case 0x0D: case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2E: case 0x2F:
        case 0xA3: case 0xC2: case 0xC3: case 0xC4: case 0xC6: return 0;
        case 0x05: return kCallerSaved;  // syscall writes rcx and r11, and the kernel is taken to keep only what a callee keeps
        case 0x31: case 0x32: case 0x33: return Bit(kRax) | Bit(kRdx);
        case 0x7E: return in.rep == 0xF3 ? 0 : o.Rm();  // movd/movq r/m, xmm; with F3, movq xmm, xmm/m64
        case 0xA0: case 0xA1: case 0xA8: case 0xA9: return Bit(kRsp);
        case 0xA2: return Bit(kRax) | Bit(kRbx) | Bit(kRcx) | Bit(kRdx);  // cpuid
        case 0xA4: case 0xA5: case 0xAB: case 0xAC: case 0xAD: case 0xB3: case 0xBB: return o.Rm();
        case 0xAE:
            if (!o.Direct()) return 0;       // fxsave, fxrstor, ldmxcsr, xsave, clflush and the rest of the memory forms
            if (o.Ext() < 2) return o.Rm();  // rdfsbase, rdgsbase
            return o.Ext() == 4 ? kEvery : 0;
        case 0xB0: return Bit(kRax) | o.Rm8();
        case 0xB1: return Bit(kRax) | o.Rm();
        case 0xBA: return o.Ext() == 4 ? 0 : o.Ext() > 4 ? o.Rm() : kEvery;
        case 0xC0: return o.Reg8() | o.Rm8();
        case 0xC1: return o.Reg() | o.Rm();
        case 0xC7:
            if (o.Direct()) return o.Rm();  // rdrand, rdseed, rdpid
            return o.Ext() == 1 ? Bit(kRax) | Bit(kRdx) : kEvery;
        default: return kEvery;
    }
}

// The 0F 38 and 0F 3A maps, legacy and VEX. The rest of each map's known
// opcodes write vector, mask or tile registers, the flags or memory.
Regs ThreeByteWrites(const Operands& o) {
    const Insn& in = o.in;
    const std::uint8_t op = in.opcode2;
    if (in.map == 3) {
        if (op >= 0x14 && op <= 0x17) return o.Rm();     // pextrb/w/d/q, extractps
        if (op == 0x61 || op == 0x63) return Bit(kRcx);  // pcmpestri, pcmpistri
        if (in.vex && op == 0xF0) return o.Reg();        // rorx
        const bool known = in.vex ? In(op, {{0x00, 0x02}, {0x04, 0x06}, {0x08, 0x0F}, {0x18, 0x19}, {0x1D, 0x1D}, {0x20, 0x22},
                                            {0x30, 0x33}, {0x38, 0x39}, {0x40, 0x42}, {0x44, 0x44}, {0x46, 0x46}, {0x48, 0x4C},
                                            {0x5C, 0x60}, {0x62, 0x62}, {0x68, 0x6F}, {0x78, 0x7F}, {0xCE, 0xCF}, {0xDE, 0xDF}})
                                  : In(op, {{0x08, 0x0F}, {0x20, 0x22}, {0x40, 0x42}, {0x44, 0x44}, {0x60, 0x60}, {0x62, 0x62},
                                            {0xCC, 0xCC}, {0xCE, 0xCF}, {0xDF, 0xDF}});
        return known ? 0 : kEvery;
    }
    if (!in.vex) {
        if (op == 0xF0 || op == 0xF1 || op == 0xF6) return o.Reg();  // movbe, crc32, adcx, adox
        const bool known = In(op, {{0x00, 0x0B}, {0x10, 0x10}, {0x14, 0x15}, {0x17, 0x17}, {0x1C, 0x1E}, {0x20, 0x25}, {0x28, 0x2B},
                                   {0x30, 0x35}, {0x37, 0x41}, {0x80, 0x82}, {0xC8, 0xCD}, {0xCF, 0xCF}, {0xDB, 0xDF}});
        return known ? 0 : kEvery;
    }
    if (op >= 0xE0 && op <= 0xEF) return o.Reg();  // cmpccxadd
    switch (op) {
        case 0xF2: case 0xF5: case 0xF7: return o.Reg();  // andn, bzhi, pdep, pext, bextr, shlx, sarx, shrx
        case 0xF3: return o.Ext() >= 1 && o.Ext() <= 3 ? Bit(in.vvvv) : kEvery;  // blsr, blsmsk, blsi
        case 0xF6: return o.Reg() | Bit(in.vvvv);  // mulx
        default: break;
    }
    const bool known = In(op, {{0x00, 0x0F}, {0x13, 0x13}, {0x16, 0x1A}, {0x1C, 0x1E}, {0x20, 0x25}, {0x28, 0x2F}, {0x30, 0x41},
                               {0x45, 0x47}, {0x49, 0x49}, {0x4B, 0x4B}, {0x50, 0x53}, {0x58, 0x5A}, {0x5C, 0x5C}, {0x5E, 0x5E},
                               {0x72, 0x72}, {0x78, 0x79}, {0x8C, 0x8C}, {0x8E, 0x8E}, {0x90, 0x93}, {0x96, 0x9F}, {0xA6, 0xAF},
                               {0xB0, 0xB1}, {0xB4, 0xBF}, {0xCB, 0xCD}, {0xCF, 0xCF}, {0xD2, 0xD3}, {0xDA, 0xDF}});
    return known ? 0 : kEvery;
}

// VEX 0F, where the rest of the known opcodes write vector or mask registers,
// the flags or memory.
Regs VexTwoByteWrites(const Operands& o) {
    const Insn& in = o.in;
    const std::uint8_t op = in.opcode2;
    switch (op) {
        case 0x2C: case 0x2D: case 0x50: case 0x93: case 0xC5: case 0xD7: return o.Reg();
        case 0x7E: return in.rep == 0xF3 ? 0 : o.Rm();
        case 0x71: case 0x72: case 0x73: return ShiftByImmediate(o);
        case 0xAE: return !o.Direct() && (o.Ext() == 2 || o.Ext() == 3) ? 0 : kEvery;  // vldmxcsr, vstmxcsr
        default: break;
    }
    const bool known = In(op, {{0x10, 0x17}, {0x28, 0x2F}, {0x41, 0x42}, {0x44, 0x47}, {0x4A, 0x4B}, {0x51, 0x70}, {0x74, 0x77},
                               {0x7C, 0x7D}, {0x7F, 0x7F}, {0x90, 0x92}, {0x98, 0x99}, {0xC2, 0xC2}, {0xC4, 0xC4}, {0xC6, 0xC6},
                               {0xD0, 0xFE}});
    return known ? 0 : kEvery;
}

// The general purpose registers `in` may write, at any width.
Regs Writes(const Insn& in) {
    const Operands o{in};
    if (in.map >= 2) return ThreeByteWrites(o);
    if (in.vex) return VexTwoByteWrites(o);
    return in.map == 1 ? TwoByteWrites(o) : OneByteWrites(o);
}

}  // namespace

bool Clobbers(const Insn& in, int reg) { return (Writes(in) >> reg & 1) != 0; }

bool LeafLoadsStatic(const Insns& leaf, std::uint32_t& staticRva) {
    if (leaf.size() != 2 || !IsRet(leaf[1])) return false;
    if (!IsMovRegFromMem(leaf[0], kRax) || !leaf[0].IsRipRelative()) return false;
    staticRva = leaf[0].RipTarget();
    return true;
}

bool LeafReadsField(const Insns& leaf, bool qword, std::uint32_t& field) {
    int self = kRcx;
    std::size_t at = 0;
    if (leaf.size() == 4) {
        const Insn& spill = leaf[0];
        const Insn& reload = leaf[1];
        if (spill.opcode != 0x89 || !spill.rex_w || spill.reg != kRcx || !spill.IsMem(kRsp) || reload.opcode != 0x8B ||
            !reload.rex_w || reload.reg != kRax || !reload.IsMem(kRsp) || reload.disp != spill.disp)
            return false;
        self = kRax;
        at = 2;
    } else if (leaf.size() != 2) {
        return false;
    }
    if (!IsRet(leaf[at + 1])) return false;
    const Insn& m = leaf[at];
    if (m.opcode != 0x8B || m.rex_w != qword || m.opsize16 || m.reg != kRax || !m.IsMem(self) || m.disp <= 0) return false;
    field = static_cast<std::uint32_t>(m.disp);
    return true;
}

}  // namespace insn

}  // namespace cameraunlock::memory::x64
