// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Tests for the x64 code analysis (cameraunlock/memory/x64_analysis.h) on
// hand-made x64 images: function chunks and their starts, cross references,
// jump tables, leaves, a class's primary vtable by its type descriptor, the
// instruction shapes, and the registers each kind of instruction may write.

#include <cameraunlock/memory/x64_analysis.h>

#include "pe_fixture.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cameraunlock::memory;

namespace {

int g_failures = 0;

bool Check(bool cond, const std::string& name) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name.c_str());
    if (!cond) ++g_failures;
    return cond;
}

template <class Fn>
std::string Thrown(Fn fn) {
    try {
        fn();
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

// .pdata at 0x4000: entries for 0x1100-0x1120, 0x1200-0x120D and a cold part
// at 0x1300-0x1305 chained to 0x1200; 0x1400-0x1401 chained to 0x1500, which
// has no entry of its own. Unwind info at .rdata 0x2600 onwards.
struct CodeFixture : PeFixture {
    CodeFixture() : PeFixture(true) {
        Entry(0, 0x1100, 0x1120, 0x2600);
        Entry(1, 0x1200, 0x120D, 0x2610);
        Entry(2, 0x1300, 0x1305, 0x2620);
        Entry(3, 0x1400, 0x1401, 0x2640);
        Directory(3, 0x4000, 4 * 12);
        data[0x2600] = 0x01;
        data[0x2610] = 0x01;
        Chained(0x2620, 0x1200, 0x120D, 0x2610);
        Chained(0x2640, 0x1500, 0x1510, 0x2660);
        data[0x2660] = 0x01;

        // 0x1100: lea rdx, [rip - 0x1107] (the image base); mov eax, [rdx +
        // rcx*4 + 0x1118]; add rax, rdx; jmp rax; two cases, each a ret; int3
        // padding; the table of the two cases at 0x1118.
        Code(0x1100, {0x48, 0x8D, 0x15});
        Put(0x1103, static_cast<std::int32_t>(0 - 0x1107));
        Code(0x1107, {0x8B, 0x84, 0x8A});
        Put(0x110A, std::uint32_t{0x1118});
        Code(0x110E, {0x48, 0x03, 0xC2, 0xFF, 0xE0, 0xC3, 0xC3});
        Put(0x1118, std::uint32_t{0x1113});
        Put(0x111C, std::uint32_t{0x1114});

        // 0x1200: call 0x1100; mov rax, [rip + 0x2800]; ret.
        Call(0x1200, 0x1100);
        Code(0x1205, {0x48, 0x8B, 0x05});
        Put(0x1208, static_cast<std::int32_t>(0x2800 - 0x120C));
        Code(0x120C, {0xC3});
        // 0x1300, the cold part: jmp 0x1100.
        Code(0x1300, {0xE9});
        Put(0x1301, static_cast<std::int32_t>(0x1100 - 0x1305));
        // 0x1400: ret.
        Code(0x1400, {0xC3});
        // A leaf with no entry: mov eax, [rcx + 0x48]; ret. And a call of
        // 0x1100 in bytes no chunk holds.
        Code(0x1600, {0x8B, 0x41, 0x48, 0xC3});
        Call(0x1700, 0x1100);
    }
    void Entry(unsigned i, std::uint32_t begin, std::uint32_t end, std::uint32_t unwind) {
        Put(0x4000 + 12 * i, begin);
        Put(0x4004 + 12 * i, end);
        Put(0x4008 + 12 * i, unwind);
    }
    void Chained(std::uint32_t at, std::uint32_t begin, std::uint32_t end, std::uint32_t unwind) {
        data[at] = 0x01 | (4 << 3);
        Put(at + 4, begin);
        Put(at + 8, end);
        Put(at + 12, unwind);
    }
};

void ChunksAndTheirStarts() {
    std::printf("Function chunks\n");
    const CodeFixture f;
    const pe::Image image = f.Open();
    const x64::Analysis an(image);
    const x64::Chunk* c = an.ChunkAt(0x1302);
    Check(c && c->begin == 0x1300 && c->end == 0x1305 && c->primary == 0x1200, "a cold part's chunk names its parent as its start");
    Check(an.ChunkAt(0x1120) == nullptr && an.PrimaryOf(0x1150) == 0, "a place no chunk holds has none, and no start");
    Check(an.IsFunctionStart(0x1200) && !an.IsFunctionStart(0x1300) && !an.IsFunctionStart(0x1205), "a function starts at its own chunk's start");
    Check(an.PrimaryOf(0x1400) == 0x1500 && an.IsPrimary(0x1500) && !an.IsFunctionStart(0x1500),
          "a chunk chained to a parent with no entry still names the parent's start");
    Check(image.RootOf(0x1400) == 0, "where pe::Image::RootOf finds no root for it");

    CodeFixture unsorted;
    unsorted.Entry(0, 0x1200, 0x120D, 0x2610);
    unsorted.Entry(1, 0x1100, 0x1120, 0x2600);
    const pe::Image u = unsorted.Open();
    Check(Thrown([&] { x64::Analysis a(u); }).find("not sorted") != std::string::npos, "an unsorted exception directory throws");
    CodeFixture empty;
    empty.Directory(3, 0x4000, 0);
    const pe::Image e = empty.Open();
    Check(!Thrown([&] { x64::Analysis a(e); }).empty(), "an empty exception directory throws");
    CodeFixture partial;
    partial.Directory(3, 0x4000, 4 * 12 + 4);
    const pe::Image p = partial.Open();
    Check(Thrown([&] { x64::Analysis a(p); }).find("not whole entries") != std::string::npos, "an exception directory that ends inside an entry throws");
    CodeFixture outside;
    for (unsigned i = 0; i < 4; ++i) outside.Entry(i, 0x2000 + 0x20 * i, 0x2010 + 0x20 * i, 0x2600);
    const pe::Image o = outside.Open();
    Check(Thrown([&] { x64::Analysis a(o); }).find(".text") != std::string::npos, "an exception directory with nothing in .text throws");
    const PeFixture x86(false);
    const pe::Image i86 = x86.Open();
    Check(!Thrown([&] { x64::Analysis a(i86); }).empty(), "an x86 image throws");
}

void ReferencesByTarget() {
    std::printf("Cross references\n");
    const CodeFixture f;
    const pe::Image image = f.Open();
    const x64::Analysis an(image);
    const std::vector<x64::Ref> rel = an.RelTo(0x1100);
    Check(rel.size() == 2 && rel[0].site == 0x1200 && rel[1].site == 0x1300, "RelTo lists the decoded call and jmp landing on a target, by site");
    Check(an.RawRelCount(0x1100) == 3, "RawRelCount also counts the E8 bytes no chunk's decode reads");
    const std::vector<x64::Ref> rip = an.RipTo(0x2800);
    Check(rip.size() == 1 && rip[0].site == 0x1205 && rip[0].target == 0x2800, "RipTo lists the RIP-relative operand reaching an RVA");
    Check(an.RipTo(0).size() == 1 && an.RelTo(0x1234).empty(), "and nothing else");
}

void FunctionsAndTheirTables() {
    std::printf("Functions, jump tables and leaves\n");
    CodeFixture f;
    const pe::Image image = f.Open();
    const x64::Analysis an(image);
    const x64::Insns& split = an.Function(0x1200);
    Check(split.size() == 4 && split[0].rva == 0x1200 && split[3].rva == 0x1300, "Function decodes every chunk of a split function in address order");
    Check(&an.Function(0x1200) == &split, "and keeps its decode");
    std::vector<bool> code;
    std::map<std::uint32_t, std::vector<std::uint32_t>> tables;
    Check(an.Code(0x1100, code, tables), "Code tells a switch's table from its code");
    Check(tables.size() == 1 && tables[0x1118] == std::vector<std::uint32_t>{0x1113, 0x1114}, "the table's cases are read off it");
    const x64::Insns& sw = an.Function(0x1100);
    const std::size_t first = x64::Analysis::IndexOf(sw, 0x1118);
    Check(first != SIZE_MAX && !code[first] && code[x64::Analysis::IndexOf(sw, 0x1113)], "the table's bytes are not code, and the cases are");
    Check(x64::Analysis::IndexOf(sw, 0x1101) == SIZE_MAX, "IndexOf is SIZE_MAX where no instruction starts");
    Check(an.Code(0x1200, code, tables) && tables.empty() && code.size() == 4, "a function with no table is all code");

    f.Put(0x111C, std::uint32_t{0x1101});
    const pe::Image mid = f.Open();
    const x64::Analysis midAn(mid);
    Check(!midAn.Code(0x1100, code, tables), "a case inside an instruction is no table");

    const x64::Insns leaf = an.Leaf(0x1600);
    Check(leaf.size() == 2 && leaf[1].opcode == 0xC3, "Leaf decodes up to the first ret");
    Check(an.Leaf(0x1100, 2).size() == 2, "and stops at the most it is asked for");
    Check(an.Body(0x1600).size() == 2 && an.Body(0x1200).size() == 4, "Body is a leaf's decode, or every chunk of a function that has them");
    Check(an.Body(0x1500).size() == 1 && an.Body(0x1500)[0].rva == 0x1400, "including a start that only a chained chunk names");
}

// An x64 type descriptor for `name` at `td`, a complete object locator for it
// at `col` (offset `offset`) and a vtable whose locator pointer is at `meta`.
void Rtti(PeFixture& f, std::uint32_t td, const char* name, std::uint32_t col, std::uint32_t offset, std::uint32_t meta) {
    f.Text(td + 16, name);
    f.Put(col, std::uint32_t{1});
    f.Put(col + 4, offset);
    f.Put(col + 12, td);
    f.Put(col + 20, col);
    f.Pointer(meta, col);
    f.Pointer(meta + 8, 0x1100);
}

void VtablesByTypeDescriptor() {
    std::printf("Primary vtables\n");
    Check(x64::MangledClass("romu::CarCamera") == ".?AVCarCamera@romu@@" && x64::MangledClass("Cam") == ".?AVCam@@",
          "MangledClass names nested classes innermost first");
    Check(!Thrown([] { x64::MangledClass("romu::"); }).empty() && !Thrown([] { x64::MangledClass(""); }).empty(),
          "MangledClass throws on an empty part");

    PeFixture f(true);
    Rtti(f, 0x3000, ".?AVCam@@", 0x2400, 0, 0x2500);
    Rtti(f, 0x3100, ".?AVHud@@", 0x2420, 0, 0x2520);
    Rtti(f, 0x3100, ".?AVHud@@", 0x2440, 8, 0x2540);  // Hud's subobject at +8: not its primary vtable
    Rtti(f, 0x2700, ".?AVInRdata@@", 0x2460, 0, 0x2560);
    std::string why;
    cameraunlock::diagnostics::Fail fail(why);
    std::uint32_t vt = 0;
    const pe::Image image = f.Open();
    Check(x64::FindVtable(image, ".?AVCam@@", vt, fail) && vt == 0x2508, "FindVtable finds the slot after the pointer to the locator");
    Check(x64::FindVtable(image, ".?AVHud@@", vt, fail) && vt == 0x2528, "and only the locator at offset 0 is the primary one");
    Check(x64::FindVtable(image, ".?AVInRdata@@", vt, fail) && vt == 0x2568, "a type descriptor in .rdata counts");
    Check(!x64::FindVtable(image, ".?AVNone@@", vt, fail) && why == "rtti: expected one primary vtable for .?AVNone@@, found 0",
          "a class with no RTTI fails with the reason");
    f.Pointer(0x2580, 0x2400);
    const pe::Image twice = f.Open();
    Check(!x64::FindVtable(twice, ".?AVCam@@", vt, fail) && why == "rtti: expected one primary vtable for .?AVCam@@, found 2",
          "a class with two primary vtables fails with the reason");

    std::string text = "kept";
    cameraunlock::diagnostics::Fail say(text);
    Check(!say("value %s: %d", "x", 7) && text == "value x: 7", "Fail formats the reason into its string and returns false");
}

x64::Insns Decoded(std::initializer_list<std::initializer_list<std::uint8_t>> code) {
    x64::Insns out;
    std::uint32_t at = 0x1000;
    for (const auto& bytes : code) {
        const std::vector<std::uint8_t> b(bytes);
        Insn in;
        if (!DecodeX64(b.data(), b.size(), at, in)) throw std::runtime_error("a test instruction does not decode");
        out.push_back(in);
        at += in.len;
    }
    return out;
}

void Shapes() {
    std::printf("Instruction shapes\n");
    namespace insn = x64::insn;
    const x64::Insns lea = Decoded({{0x48, 0x8D, 0x4B, 0x08}, {0x48, 0x8B, 0x48, 0x08}, {0x48, 0x89, 0xC8}, {0x48, 0x8B, 0xC1}});
    Check(insn::IsLea(lea[0], insn::kRcx) && !insn::IsLea(lea[0], insn::kRax), "IsLea: lea rcx, [rbx+8]");
    Check(insn::IsMovRegFromMem(lea[1], insn::kRcx), "IsMovRegFromMem: mov rcx, [rax+8]");
    Check(insn::IsMovRegReg(lea[2], insn::kRax, insn::kRcx) && insn::IsMovRegReg(lea[3], insn::kRax, insn::kRcx), "IsMovRegReg reads 89 /r and 8B /r");
    Check(insn::RegRegDst(lea[2]) == insn::kRax && insn::RegRegDst(lea[1]) == Insn::kNone, "RegRegDst is a register move's destination");
    const x64::Insns imm = Decoded({{0xC7, 0x41, 0x10, 1, 0, 0, 0}, {0x83, 0x79, 0x10, 0x05}, {0xB9, 1, 0, 0, 0}, {0x0F, 0xB6, 0xC1}, {0xE8, 0, 0, 0, 0}, {0xC3}});
    Check(insn::IsMovDwordImm(imm[0]) && insn::IsCmpDwordImm8(imm[1]) && insn::IsMovEcxImm(imm[2]), "the dword immediate shapes");
    Check(insn::IsTwoByte(imm[3], 0xB6) && insn::IsCall(imm[4]) && insn::IsRet(imm[5]) && !insn::IsCall(imm[5]), "IsTwoByte, IsCall, IsRet");

    std::uint32_t at = 0;
    Check(insn::LeafLoadsStatic(Decoded({{0x48, 0x8B, 0x05, 0x10, 0, 0, 0}, {0xC3}}), at) && at == 0x1017, "LeafLoadsStatic: mov rax, [rip + x]; ret");
    Check(insn::LeafReadsField(Decoded({{0x48, 0x8B, 0x41, 0x48}, {0xC3}}), true, at) && at == 0x48, "LeafReadsField: mov rax, [rcx + 0x48]; ret");
    Check(!insn::LeafReadsField(Decoded({{0x48, 0x8B, 0x41, 0x48}, {0xC3}}), false, at), "of the width asked for");
    Check(insn::LeafReadsField(Decoded({{0x48, 0x89, 0x4C, 0x24, 0x08}, {0x48, 0x8B, 0x44, 0x24, 0x08}, {0x8B, 0x40, 0x30}, {0xC3}}), false, at) && at == 0x30,
          "LeafReadsField: the same read through a spill of rcx");
}

constexpr int kRax = 0, kRcx = 1, kRdx = 2, kRbx = 3, kRsp = 4, kRbp = 5, kRsi = 6, kRdi = 7;
constexpr int kEvery = -1;

struct Writes {
    const char* name;
    std::vector<std::uint8_t> bytes;
    std::vector<int> regs;  // every register it may leave changed, or kEvery
};

void ClobbersTest() {
    std::printf("Registers each kind of instruction may write\n");
    const std::vector<int> callerSaved = {kRax, kRcx, kRdx, 8, 9, 10, 11};
    const std::vector<Writes> cases = {
        // Zeroing idioms and arithmetic.
        {"xor eax, eax (31 /r, r/m destination)", {0x31, 0xC0}, {kRax}},
        {"xor eax, eax (33 /r)", {0x33, 0xC0}, {kRax}},
        {"xor r8d, r8d", {0x45, 0x31, 0xC0}, {8}},
        {"sub edx, edx", {0x29, 0xD2}, {kRdx}},
        {"sbb ecx, ecx", {0x19, 0xC9}, {kRcx}},
        {"and ecx, 0", {0x83, 0xE1, 0x00}, {kRcx}},
        {"or eax, -1", {0x83, 0xC8, 0xFF}, {kRax}},
        {"add eax, ecx (01 /r)", {0x01, 0xC8}, {kRax}},
        {"add eax, ecx (03 /r)", {0x03, 0xC1}, {kRax}},
        {"add al, 1", {0x04, 0x01}, {kRax}},
        {"add [rcx], eax", {0x01, 0x01}, {}},
        {"neg rax", {0x48, 0xF7, 0xD8}, {kRax}},
        {"not cl", {0xF6, 0xD1}, {kRcx}},
        {"mul ecx", {0xF7, 0xE1}, {kRax, kRdx}},
        {"div cl", {0xF6, 0xF1}, {kRax}},
        {"imul rax, rcx", {0x48, 0x0F, 0xAF, 0xC1}, {kRax}},
        {"imul rax, rcx, 3", {0x48, 0x6B, 0xC1, 0x03}, {kRax}},
        {"shl rax, cl", {0x48, 0xD3, 0xE0}, {kRax}},
        {"sar ecx, 4", {0xC1, 0xF9, 0x04}, {kRcx}},
        {"inc rcx", {0x48, 0xFF, 0xC1}, {kRcx}},
        {"dec byte [rcx]", {0xFE, 0x09}, {}},
        {"shld rcx, rax, 4", {0x48, 0x0F, 0xA4, 0xC1, 0x04}, {kRcx}},
        {"bts ecx, 3", {0x0F, 0xBA, 0xE9, 0x03}, {kRcx}},
        {"btr ecx, eax", {0x0F, 0xB3, 0xC1}, {kRcx}},
        // Reads that write no register.
        {"cmp rcx, 5", {0x48, 0x83, 0xF9, 0x05}, {}},
        {"cmp eax, ecx", {0x39, 0xC8}, {}},
        {"test ecx, ecx", {0x85, 0xC9}, {}},
        {"test cl, 1", {0xF6, 0xC1, 0x01}, {}},
        {"bt ecx, 3", {0x0F, 0xBA, 0xE1, 0x03}, {}},
        {"mov [rbx+8], rcx", {0x48, 0x89, 0x4B, 0x08}, {}},
        {"mov byte [rcx], 0", {0xC6, 0x01, 0x00}, {}},
        // Moves, and the registers inside registers.
        {"mov al, cl", {0x8A, 0xC1}, {kRax}},
        {"mov ah, cl", {0x8A, 0xE1}, {kRax}},
        {"mov spl, cl", {0x40, 0x8A, 0xE1}, {kRsp}},
        {"mov ch, al", {0x88, 0xC5}, {kRcx}},
        {"mov ch, 1", {0xB5, 0x01}, {kRcx}},
        {"mov bpl, 1", {0x40, 0xB5, 0x01}, {kRbp}},
        {"mov r9b, 1", {0x41, 0xB1, 0x01}, {9}},
        {"mov ecx, 1", {0xB9, 0x01, 0x00, 0x00, 0x00}, {kRcx}},
        {"mov rax, imm64", {0x48, 0xB8, 1, 2, 3, 4, 5, 6, 7, 8}, {kRax}},
        {"mov rcx, [rax+8]", {0x48, 0x8B, 0x48, 0x08}, {kRcx}},
        {"mov rax, rcx (89 /r)", {0x48, 0x89, 0xC8}, {kRax}},
        {"mov ax, cx", {0x66, 0x89, 0xC8}, {kRax}},
        {"lea rcx, [rbx+8]", {0x48, 0x8D, 0x4B, 0x08}, {kRcx}},
        {"movzx eax, cl", {0x0F, 0xB6, 0xC1}, {kRax}},
        {"movsx rax, cl", {0x48, 0x0F, 0xBE, 0xC1}, {kRax}},
        {"movsxd rax, ecx", {0x48, 0x63, 0xC1}, {kRax}},
        {"cmove eax, ecx", {0x0F, 0x44, 0xC1}, {kRax}},
        {"sete cl", {0x0F, 0x94, 0xC1}, {kRcx}},
        {"sete ah", {0x0F, 0x94, 0xC4}, {kRax}},
        {"sete spl", {0x40, 0x0F, 0x94, 0xC4}, {kRsp}},
        {"sete r12b", {0x41, 0x0F, 0x94, 0xC4}, {12}},
        {"xchg rdx, rcx", {0x48, 0x87, 0xCA}, {kRcx, kRdx}},
        {"xchg [rbx], rcx", {0x48, 0x87, 0x0B}, {kRcx}},
        {"xchg rcx, rax (91)", {0x48, 0x91}, {kRax, kRcx}},
        {"xchg r8, rax (49 90)", {0x49, 0x90}, {kRax, 8}},
        {"xchg cl, ah (86 /r)", {0x86, 0xE1}, {kRax, kRcx}},
        {"xchg [rbx], cl (86 /r)", {0x86, 0x0B}, {kRcx}},
        {"xchg sil, r9b", {0x44, 0x86, 0xCE}, {kRsi, 9}},
        {"nop", {0x90}, {}},
        {"pause", {0xF3, 0x90}, {}},
        {"nop dword [rax]", {0x0F, 0x1F, 0x40, 0x00}, {}},
        {"cmpxchg [rbx], rcx", {0x48, 0x0F, 0xB1, 0x0B}, {kRax}},
        {"cmpxchg rcx, rdx", {0x48, 0x0F, 0xB1, 0xD1}, {kRax, kRcx}},
        {"cmpxchg cl, dl (0F B0)", {0x0F, 0xB0, 0xD1}, {kRax, kRcx}},
        {"cmpxchg16b [rbx]", {0x48, 0x0F, 0xC7, 0x0B}, {kRax, kRdx}},
        {"xadd [rbx], rcx", {0x48, 0x0F, 0xC1, 0x0B}, {kRcx}},
        {"xadd cl, dl (0F C0)", {0x0F, 0xC0, 0xD1}, {kRcx, kRdx}},
        {"bswap rcx", {0x48, 0x0F, 0xC9}, {kRcx}},
        {"bswap r8d", {0x41, 0x0F, 0xC8}, {8}},
        {"cdqe", {0x48, 0x98}, {kRax}},
        {"cqo", {0x48, 0x99}, {kRdx}},
        {"lahf", {0x9F}, {kRax}},
        {"popcnt rcx, rax", {0xF3, 0x48, 0x0F, 0xB8, 0xC8}, {kRcx}},
        {"tzcnt rcx, rax", {0xF3, 0x48, 0x0F, 0xBC, 0xC8}, {kRcx}},
        {"rdrand ecx", {0x0F, 0xC7, 0xF1}, {kRcx}},
        {"rdsspq rcx", {0xF3, 0x48, 0x0F, 0x1E, 0xC9}, {kRcx}},
        {"endbr64", {0xF3, 0x0F, 0x1E, 0xFA}, {}},
        {"fxsave [rbx] (0F AE /0)", {0x0F, 0xAE, 0x03}, {}},
        {"fxrstor [rbx] (0F AE /1)", {0x0F, 0xAE, 0x0B}, {}},
        {"ldmxcsr [rbx]", {0x0F, 0xAE, 0x13}, {}},
        {"lfence", {0x0F, 0xAE, 0xE8}, {}},
        {"bsf rcx, rax", {0x48, 0x0F, 0xBC, 0xC8}, {kRcx}},
        {"movbe ecx, [rax] (0F 38 F0)", {0x0F, 0x38, 0xF0, 0x08}, {kRcx}},
        // Vector instructions that write a general purpose register.
        {"cvttsd2si rcx, xmm0", {0xF2, 0x48, 0x0F, 0x2C, 0xC8}, {kRcx}},
        {"movmskps ecx, xmm0", {0x0F, 0x50, 0xC8}, {kRcx}},
        {"movd ecx, xmm0", {0x66, 0x0F, 0x7E, 0xC1}, {kRcx}},
        {"pmovmskb ecx, xmm0", {0x66, 0x0F, 0xD7, 0xC8}, {kRcx}},
        {"pextrw ecx, xmm0, 1", {0x66, 0x0F, 0xC5, 0xC8, 0x01}, {kRcx}},
        {"pextrd ecx, xmm0, 1", {0x66, 0x0F, 0x3A, 0x16, 0xC1, 0x01}, {kRcx}},
        {"pcmpistri xmm0, xmm1, 0", {0x66, 0x0F, 0x3A, 0x63, 0xC1, 0x00}, {kRcx}},
        {"crc32 eax, ecx", {0xF2, 0x0F, 0x38, 0xF1, 0xC1}, {kRax}},
        {"adcx eax, ecx", {0x66, 0x0F, 0x38, 0xF6, 0xC1}, {kRax}},
        // And those that write only vector registers, whatever their fields name.
        {"movq xmm0, xmm1 (F3 0F 7E)", {0xF3, 0x0F, 0x7E, 0xC1}, {}},
        {"movaps xmm0, xmm1", {0x0F, 0x28, 0xC1}, {}},
        {"xorps xmm1, xmm1", {0x0F, 0x57, 0xC9}, {}},
        {"pxor xmm1, xmm1", {0x66, 0x0F, 0xEF, 0xC9}, {}},
        {"movss xmm1, [rbx+8]", {0xF3, 0x0F, 0x10, 0x4B, 0x08}, {}},
        {"psrldq xmm1, 8", {0x66, 0x0F, 0x73, 0xD9, 0x08}, {}},
        {"pshufb xmm0, xmm1", {0x66, 0x0F, 0x38, 0x00, 0xC1}, {}},
        {"palignr xmm0, xmm1, 8", {0x66, 0x0F, 0x3A, 0x0F, 0xC1, 0x08}, {}},
        // Calls, jumps and the stack.
        {"call rel32", {0xE8, 0x00, 0x00, 0x00, 0x00}, callerSaved},
        {"call [rax+0x10]", {0xFF, 0x50, 0x10}, callerSaved},
        {"call rax", {0xFF, 0xD0}, callerSaved},
        {"jmp rax", {0xFF, 0xE0}, {}},
        {"jmp rel32", {0xE9, 0x00, 0x00, 0x00, 0x00}, {}},
        {"je rel8", {0x74, 0x05}, {}},
        {"ret", {0xC3}, {kRsp}},
        {"push rcx", {0x51}, {kRsp}},
        {"pop rcx", {0x59}, {kRcx, kRsp}},
        {"pop r9", {0x41, 0x59}, {9, kRsp}},
        {"pop rcx (8F /0)", {0x8F, 0xC1}, {kRcx, kRsp}},
        {"pop qword [rbx] (8F /0)", {0x8F, 0x03}, {kRsp}},
        {"push qword [rbx] (FF /6)", {0xFF, 0x33}, {kRsp}},
        {"enter 0x20, 0", {0xC8, 0x20, 0x00, 0x00}, {kRsp, kRbp}},
        {"sub rsp, 0x28", {0x48, 0x83, 0xEC, 0x28}, {kRsp}},
        {"leave", {0xC9}, {kRsp, kRbp}},
        // Registers written without being named.
        {"syscall", {0x0F, 0x05}, callerSaved},
        {"cpuid", {0x0F, 0xA2}, {kRax, kRbx, kRcx, kRdx}},
        {"rdtsc", {0x0F, 0x31}, {kRax, kRdx}},
        {"rep stosq", {0xF3, 0x48, 0xAB}, {kRcx, kRdi}},
        {"rep movsb", {0xF3, 0xA4}, {kRcx, kRsi, kRdi}},
        {"loop", {0xE2, 0xFE}, {kRcx}},
        {"fnstsw ax", {0xDF, 0xE0}, {kRax}},
        {"fxch st1", {0xD9, 0xC9}, {}},
        // VEX.
        {"vmovaps xmm0, xmm1", {0xC5, 0xF8, 0x28, 0xC1}, {}},
        {"vfmadd231ps xmm0, xmm1, xmm2", {0xC4, 0xE2, 0x71, 0xB8, 0xC2}, {}},
        {"vzeroupper", {0xC5, 0xF8, 0x77}, {}},
        {"vmovd ecx, xmm0", {0xC5, 0xF9, 0x7E, 0xC1}, {kRcx}},
        {"vmovq xmm0, xmm1", {0xC5, 0xFA, 0x7E, 0xC1}, {}},
        {"vmovmskps ecx, xmm0", {0xC5, 0xF8, 0x50, 0xC8}, {kRcx}},
        {"vpextrd ecx, xmm0, 1", {0xC4, 0xE3, 0x79, 0x16, 0xC1, 0x01}, {kRcx}},
        {"andn eax, ecx, edx", {0xC4, 0xE2, 0x70, 0xF2, 0xC2}, {kRax}},
        {"blsr ecx, edx (vvvv names the destination)", {0xC4, 0xE2, 0x70, 0xF3, 0xCA}, {kRcx}},
        {"mulx rax, rcx, rdx", {0xC4, 0xE2, 0xF3, 0xF6, 0xC2}, {kRax, kRcx}},
        {"rorx eax, ecx, 5", {0xC4, 0xE3, 0x7B, 0xF0, 0xC1, 0x05}, {kRax}},
        {"bzhi eax, ecx, edx", {0xC4, 0xE2, 0x68, 0xF5, 0xC1}, {kRax}},
        {"pdep eax, edx, ecx", {0xC4, 0xE2, 0x6B, 0xF5, 0xC1}, {kRax}},
        {"shlx eax, ecx, edx", {0xC4, 0xE2, 0x69, 0xF7, 0xC1}, {kRax}},
        {"cmpoxadd [rbx], eax, ecx (VEX 0F 38 E0)", {0xC4, 0xE2, 0x71, 0xE0, 0x03}, {kRax}},
        {"cmpnlexadd [rbx], r9d, ecx (VEX 0F 38 EF)", {0xC4, 0x62, 0x71, 0xEF, 0x0B}, {9}},
        {"vpextrb ecx, xmm0, 1", {0xC4, 0xE3, 0x79, 0x14, 0xC1, 0x01}, {kRcx}},
        {"vpcmpestri xmm0, xmm1, 0", {0xC4, 0xE3, 0x79, 0x61, 0xC1, 0x00}, {kRcx}},
        {"vcvttss2si rcx, xmm0", {0xC4, 0xE1, 0xFA, 0x2C, 0xC8}, {kRcx}},
        {"vpmovmskb ecx, xmm0", {0xC5, 0xF9, 0xD7, 0xC8}, {kRcx}},
        {"kmovw ecx, k1", {0xC5, 0xF8, 0x93, 0xC9}, {kRcx}},
        {"vldmxcsr [rbx]", {0xC5, 0xF8, 0xAE, 0x13}, {}},
        {"vpsrldq xmm1, xmm0, 8", {0xC5, 0xF1, 0x73, 0xD8, 0x08}, {}},
        {"vpgatherdd xmm0, [rax+xmm1*4], xmm2", {0xC4, 0xE2, 0x69, 0x90, 0x04, 0x88}, {}},
        {"vaesenc xmm0, xmm1, xmm2", {0xC4, 0xE2, 0x71, 0xDC, 0xC2}, {}},
        // Left unclassified, so taken to write every register.
        {"int3", {0xCC}, {kEvery}},
        {"int 0x29", {0xCD, 0x29}, {kEvery}},
        {"sldt eax", {0x0F, 0x00, 0xC0}, {kEvery}},
        {"ud2", {0x0F, 0x0B}, {kEvery}},
        {"hlt", {0xF4}, {kEvery}},
        {"xbegin, whose abort writes eax", {0xC7, 0xF8, 0x00, 0x00, 0x00, 0x00}, {kEvery}},
        {"xabort 1", {0xC6, 0xF8, 0x01}, {kEvery}},
        {"mov rcx, cr0 (0F 20)", {0x0F, 0x20, 0xC1}, {kEvery}},
        {"no 0F 71 /0", {0x66, 0x0F, 0x71, 0xC1, 0x01}, {kEvery}},
        {"no 0F 38 0C without VEX", {0x66, 0x0F, 0x38, 0x0C, 0xC1}, {kEvery}},
        {"no 0F 38 F8 classified (movdir64b)", {0x66, 0x0F, 0x38, 0xF8, 0x08}, {kEvery}},
        {"no 0F 3A 00 without VEX", {0x66, 0x0F, 0x3A, 0x00, 0xC1, 0x00}, {kEvery}},
        {"no VEX 0F 00", {0xC5, 0xF8, 0x00, 0xC1}, {kEvery}},
        {"no VEX 0F 38 11", {0xC4, 0xE2, 0x79, 0x11, 0xC1}, {kEvery}},
        {"no VEX 0F 3A 03", {0xC4, 0xE3, 0x79, 0x03, 0xC1, 0x00}, {kEvery}},
        {"no VEX 0F AE in a register form", {0xC5, 0xF8, 0xAE, 0xC1}, {kEvery}},
    };
    for (const Writes& c : cases) {
        Insn in;
        const bool decoded = DecodeX64(c.bytes.data(), c.bytes.size(), 0x1000, in) && in.len == c.bytes.size();
        if (!Check(decoded, (std::string(c.name) + " decodes to its full length").c_str())) continue;
        const bool every = !c.regs.empty() && c.regs[0] == kEvery;
        std::string got, want;
        for (int r = 0; r < 16; ++r) {
            bool expect = every;
            for (int e : c.regs) expect = expect || e == r;
            if (x64::insn::Clobbers(in, r)) got += " " + std::to_string(r);
            if (expect) want += " " + std::to_string(r);
        }
        if (!Check(got == want, (std::string(c.name) + " writes exactly {" + want + " }").c_str()))
            std::printf("    it says {%s }\n", got.c_str());
    }
}

}  // namespace

int RunX64AnalysisTests() {
    std::printf("x64 code analysis:\n");
    g_failures = 0;
    ChunksAndTheirStarts();
    ReferencesByTarget();
    FunctionsAndTheirTables();
    VtablesByTypeDescriptor();
    Shapes();
    ClobbersTest();
    return g_failures;
}
