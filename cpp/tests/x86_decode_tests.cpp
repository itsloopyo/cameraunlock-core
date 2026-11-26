// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// The decoders' own paths, each on a hand-encoded instruction. x64: the VEX,
// 0F 38, 0F 3A and popcnt lengths hde64 cannot give, the operand fields runtime
// discovery matches on, and the cases where the bytes run out (from Headstart's
// Bugbear backend). x86: the same fields from hde32. A wrong length derails the
// decode of the rest of a function, so every case checks it.

#include <cameraunlock/memory/x86_decode.h>

#include <cstdio>
#include <initializer_list>
#include <vector>

using namespace cameraunlock::memory;

namespace {

int g_failures = 0;

bool Check(bool cond, const char* name) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) ++g_failures;
    return cond;
}

bool Decode(std::initializer_list<std::uint8_t> bytes, Insn& out, std::uint32_t rva = 0x1000) {
    const std::vector<std::uint8_t> b(bytes);
    return DecodeX64(b.data(), b.size(), rva, out);
}

bool Decode32(std::initializer_list<std::uint8_t> bytes, Insn& out, std::uint32_t at = 0x401000) {
    const std::vector<std::uint8_t> b(bytes);
    return DecodeX86(b.data(), b.size(), at, out);
}

void VexTest() {
    std::printf("VEX\n");
    Insn in;
    if (Check(Decode({ 0xC5, 0xF8, 0x28, 0x74, 0x24, 0x40 }, in), "C5 vmovaps xmm6, [rsp+0x40] decodes")) {
        Check(in.len == 6 && in.vex && in.opcode == 0x0F && in.opcode2 == 0x28, "length 6, VEX opcode 0F 28");
        Check(in.IsMem(4) && in.disp == 0x40, "memory operand [rsp+0x40]");
    }
    if (Check(Decode({ 0xC4, 0xC1, 0x78, 0x28, 0x06 }, in), "C4 vmovaps xmm0, [r14] decodes")) {
        Check(in.len == 5 && in.IsMem(14) && in.disp == 0, "length 5, inverted VEX.B gives base r14");
    }
    if (Check(Decode({ 0xC4, 0xE3, 0x79, 0x0F, 0xC1, 0x04 }, in), "C4 map 3 vpalignr decodes")) {
        Check(in.len == 6 && in.has_imm && in.imm == 4, "length 6 with its imm8");
    }
    if (Check(Decode({ 0xC5, 0xF8, 0x77 }, in), "vzeroupper decodes")) {
        Check(in.len == 3 && !in.has_modrm, "length 3, no ModRM");
    }
    Check(!Decode({ 0x62, 0xF1, 0x7C, 0x48, 0x28, 0xC1 }, in), "EVEX is refused");
}

void LegacyMapsTest() {
    std::printf("0F 38, 0F 3A and popcnt\n");
    Insn in;
    if (Check(Decode({ 0x66, 0x0F, 0x38, 0x00, 0xC1 }, in), "pshufb xmm0, xmm1 decodes")) {
        Check(in.len == 5 && in.opcode2 == 0x00 && in.opsize16, "length 5, with the 66 prefix recorded");
    }
    if (Check(Decode({ 0x66, 0x0F, 0x3A, 0x0F, 0xC1, 0x08 }, in), "palignr xmm0, xmm1, 8 decodes")) {
        Check(in.len == 6 && in.has_imm && in.imm == 8, "length 6 with its imm8");
    }
    if (Check(Decode({ 0xF3, 0x48, 0x0F, 0xB8, 0xC1 }, in), "popcnt rax, rcx decodes")) {
        Check(in.len == 5 && in.rex_w && in.reg == 0 && in.rm == 1, "length 5, REX.W, rax from rcx");
    }
}

void OperandTest() {
    std::printf("Operands\n");
    Insn in;
    if (Check(Decode({ 0x48, 0x8B, 0x05, 0x10, 0x00, 0x00, 0x00 }, in, 0x1000), "mov rax, [rip+0x10] decodes")) {
        Check(in.len == 7 && in.IsRipRelative() && in.RipTarget() == 0x1017, "targets the end of the instruction plus 0x10");
    }
    if (Check(Decode({ 0xC7, 0x05, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 }, in, 0x1000),
              "mov dword [rip+0x10], 1 decodes")) {
        Check(in.len == 10 && in.RipTarget() == 0x101A && in.has_imm && in.imm == 1,
              "the RIP target counts the immediate after the displacement");
    }
    if (Check(Decode({ 0x41, 0x8B, 0x45, 0x08 }, in), "mov eax, [r13+8] decodes")) {
        Check(in.len == 4 && in.IsMem(13) && in.disp == 8, "base r13, not RIP");
    }
    if (Check(Decode({ 0x41, 0x8B, 0x04, 0x24 }, in), "mov eax, [r12] decodes")) {
        Check(in.len == 4 && in.IsMem(12), "SIB base r12 with no index");
    }
    if (Check(Decode({ 0x8B, 0x04, 0x25, 0x78, 0x56, 0x34, 0x12 }, in), "mov eax, [0x12345678] decodes")) {
        Check(in.len == 7 && in.base == Insn::kNone && in.index == Insn::kNone && in.disp == 0x12345678,
              "SIB with no base and no index");
    }
    if (Check(Decode({ 0x41, 0xB9, 0x10, 0x00, 0x00, 0x00 }, in), "mov r9d, 0x10 decodes")) {
        Check(in.opcode == 0xB9 && in.rex_b, "records REX.B, telling it from mov ecx");
    }
    if (Check(Decode({ 0xB9, 0x10, 0x00, 0x00, 0x00 }, in), "mov ecx, 0x10 decodes")) {
        Check(in.opcode == 0xB9 && !in.rex_b && in.imm == 0x10, "with no REX.B");
    }
    if (Check(Decode({ 0x66, 0xC7, 0x01, 0x05, 0x00 }, in), "mov word [rcx], 5 decodes")) {
        Check(in.len == 5 && in.opsize16 && in.opcode == 0xC7, "length 5, the 66 prefix recorded");
    }
    if (Check(Decode({ 0xE8, 0x00, 0x01, 0x00, 0x00 }, in, 0x2000), "call rel32 decodes")) {
        Check(in.relative && in.target == 0x2105, "targets the end of the call plus rel32");
    }
}

void PrefixFieldTest() {
    std::printf("Opcode map, REX, F2/F3 and VEX.vvvv\n");
    Insn in;
    if (Check(Decode({ 0x8A, 0xE1 }, in), "mov ah, cl decodes")) Check(!in.rex && in.map == 0, "no REX, one-byte map");
    if (Check(Decode({ 0x40, 0x8A, 0xE1 }, in), "mov spl, cl decodes")) Check(in.rex && !in.rex_w && !in.rex_b, "a bare REX recorded");
    if (Check(Decode({ 0x0F, 0x1F, 0x40, 0x00 }, in), "nop dword [rax] decodes")) Check(in.map == 1 && in.rep == 0, "the 0F map, no F2/F3");
    if (Check(Decode({ 0xF3, 0x0F, 0x7E, 0xC1 }, in), "movq xmm0, xmm1 decodes")) Check(in.map == 1 && in.rep == 0xF3, "the 0F map with F3");
    if (Check(Decode({ 0xF2, 0x0F, 0x38, 0xF1, 0xC1 }, in), "crc32 eax, ecx decodes"))
        Check(in.map == 2 && in.rep == 0xF2 && in.opcode2 == 0xF1, "the 0F 38 map with F2");
    if (Check(Decode({ 0x66, 0x0F, 0x3A, 0x0F, 0xC1, 0x08 }, in), "palignr decodes")) Check(in.map == 3, "the 0F 3A map");
    if (Check(Decode({ 0x4C, 0x0F, 0x38, 0xF0, 0x00 }, in), "movbe r8, [rax] decodes")) Check(in.rex && in.rex_w && in.reg == 8, "with its REX");
    if (Check(Decode({ 0xC5, 0xFA, 0x7E, 0xC1 }, in), "vmovq xmm0, xmm1 decodes"))
        Check(in.vex && in.map == 1 && in.rep == 0xF3 && in.vvvv == 0, "VEX map 1, the F3 its pp implies");
    if (Check(Decode({ 0xC4, 0xE2, 0x70, 0xF2, 0xC2 }, in), "andn eax, ecx, edx decodes"))
        Check(in.vex && in.map == 2 && in.vvvv == 1 && in.reg == 0 && in.rm == 2, "VEX map 2, vvvv naming ecx");
    // hde64 takes the byte after a REX for the opcode, so it lengths these wrong.
    Check(!Decode({ 0x40, 0x4C, 0x8B, 0xF0 }, in), "a REX followed by another REX is refused");
    Check(!Decode({ 0x48, 0x66, 0x89, 0xC0 }, in), "a REX followed by a 66 prefix is refused");
    Check(!Decode({ 0x66, 0x48, 0xF3, 0x0F, 0xB8, 0xC1 }, in), "a REX followed by an F3 prefix is refused");
    Check(!Decode({ 0x66, 0xC5, 0xF8, 0x77 }, in), "a VEX prefix after a 66 prefix is refused");
    Check(!Decode({ 0x48, 0xC5, 0xF8, 0x77 }, in), "a VEX prefix after a REX is refused");
    if (Check(Decode({ 0x66, 0x66, 0x66, 0x66, 0x66, 0x0F, 0x38, 0x00, 0xC1 }, in), "pshufb behind five 66 prefixes decodes"))
        Check(in.len == 9 && in.map == 2 && in.opcode2 == 0x00, "to its nine bytes, in the 0F 38 map");
    Check(!Decode({ 0xF3, 0xC4, 0xE2, 0x70, 0xF2, 0xC2 }, in), "a three-byte VEX prefix after an F3 prefix is refused");
    Check(!Decode({ 0x66, 0x62, 0xF1, 0x7C, 0x48, 0x28, 0xC1 }, in), "an EVEX prefix after a 66 prefix is refused");
}

void TruncatedTest() {
    std::printf("Bytes that run out\n");
    Insn in;
    Check(!Decode({ 0x48, 0x8B, 0x05, 0x10, 0x00 }, in), "a RIP load cut short of its displacement is refused");
    Check(!Decode({ 0xC5, 0xF8 }, in), "a VEX prefix with no opcode is refused");
    Check(!Decode({ 0x66, 0x0F, 0x38 }, in), "an 0F 38 escape with no opcode is refused");
    Check(!Decode({ 0x66, 0x0F, 0x3A, 0x0F, 0xC1 }, in), "an 0F 3A instruction missing its imm8 is refused");
    Check(!Decode({ 0xE8, 0x00, 0x01 }, in), "a call cut short of its rel32 is refused");
    std::vector<std::uint8_t> none;
    Check(!DecodeX64(none.data(), 0, 0, in), "no bytes at all is refused");
}

void X86Test() {
    std::printf("x86\n");
    Insn in;
    if (Check(Decode32({ 0x8B, 0x41, 0x10 }, in), "mov eax, [ecx+0x10] decodes"))
        Check(in.len == 3 && in.IsMem(1) && in.disp == 0x10 && in.reg == 0, "length 3, [ecx+0x10] into eax");
    if (Check(Decode32({ 0xA1, 0x78, 0x56, 0x34, 0x12 }, in), "mov eax, [0x12345678] decodes"))
        Check(in.len == 5 && in.opcode == 0xA1 && !in.has_modrm, "length 5, the moffs form has no ModRM");
    if (Check(Decode32({ 0x8B, 0x0D, 0x78, 0x56, 0x34, 0x12 }, in), "mov ecx, [0x12345678] decodes"))
        Check(in.len == 6 && in.IsAbsolute() && in.disp == 0x12345678, "disp32 with no base is absolute, not RIP-relative");
    if (Check(Decode32({ 0x8B, 0x04, 0x8D, 0x00, 0x10, 0x40, 0x00 }, in), "mov eax, [ecx*4+0x401000] decodes"))
        Check(in.len == 7 && in.base == Insn::kNone && in.index == 1 && in.scale == 4 && in.disp == 0x401000, "SIB with an index and no base");
    if (Check(Decode32({ 0x68, 0x00, 0x20, 0x80, 0x80 }, in), "push 0x80802000 decodes"))
        Check(in.has_imm && static_cast<std::uint32_t>(in.imm) == 0x80802000u, "an address above 2 GiB reads back as a uint32");
    if (Check(Decode32({ 0x83, 0xC4, 0xF8 }, in), "add esp, -8 decodes"))
        Check(in.has_imm && in.imm == -8, "the imm8 is sign-extended");
    if (Check(Decode32({ 0xE8, 0x00, 0x01, 0x00, 0x00 }, in, 0x402000), "call rel32 decodes"))
        Check(in.relative && in.target == 0x402105, "targets the end of the call plus rel32");
    if (Check(Decode32({ 0xEB, 0xFE }, in, 0x402000), "jmp $ decodes"))
        Check(in.relative && in.target == 0x402000 && in.len == 2, "a short jump back to itself");
    if (Check(Decode32({ 0x0F, 0x84, 0x10, 0x00, 0x00, 0x00 }, in, 0x402000), "je rel32 decodes"))
        Check(in.map == 1 && in.opcode2 == 0x84 && in.target == 0x402016, "the 0F map, and its target");
    if (Check(Decode32({ 0x66, 0xC7, 0x01, 0x05, 0x00 }, in), "mov word [ecx], 5 decodes"))
        Check(in.len == 5 && in.opsize16 && in.imm == 5, "length 5, the 66 prefix recorded");
    if (Check(Decode32({ 0xC2, 0x08, 0x00 }, in), "ret 8 decodes"))
        Check(in.len == 3 && in.has_imm && in.imm == 8, "with the bytes it pops");
    if (Check(Decode32({ 0xC4, 0x01 }, in), "les eax, [ecx] decodes"))
        Check(in.len == 2 && in.opcode == 0xC4, "C4 with a memory operand is LES in 32-bit mode");
    Check(!Decode32({ 0xC5, 0xF8, 0x77 }, in), "VEX is refused");
    Check(!Decode32({ 0x62, 0xF1, 0x7C, 0x48, 0x28, 0xC1 }, in), "EVEX is refused");
    Check(!Decode32({ 0x67, 0x8B, 0x06, 0x34, 0x12 }, in), "16-bit addressing (a 67 prefix) is refused");
    Check(!Decode32({ 0xE8, 0x00, 0x01 }, in), "a call cut short of its rel32 is refused");
    std::vector<std::uint8_t> none;
    Check(!DecodeX86(none.data(), 0, 0, in), "no bytes at all is refused");
}

unsigned Length64(std::initializer_list<std::uint8_t> bytes) {
    const std::vector<std::uint8_t> b(bytes);
    return LengthX64(b.data(), b.size());
}

unsigned Length32(std::initializer_list<std::uint8_t> bytes) {
    const std::vector<std::uint8_t> b(bytes);
    return LengthX86(b.data(), b.size());
}

// Each expected length is hde's own, worked from its tables, including where
// hde is wrong about the CPU: the old walks stepped by these.
void LengthTest() {
    std::printf("hde32 and hde64 lengths, refused or not\n");
    Check(Length32({ 0x8B, 0x41, 0x10 }) == 3, "x86 mov eax, [ecx+0x10] is 3 bytes, as DecodeX86 lengths it");
    Check(Length32({ 0xC5, 0xF8, 0x77 }) == 2, "x86 VEX, which DecodeX86 refuses, is hde32's LDS of 2 bytes");
    Check(Length32({ 0xF0, 0x01, 0xC0 }) == 3, "x86 lock on a register form, an hde32 error, is 3 bytes");
    Check(Length32({ 0x0F, 0x0B }) == 2, "x86 ud2, an hde32 error, is 2 bytes");
    Check(Length32({ 0x67, 0x8B, 0x06, 0x34, 0x12 }) == 5, "x86 mov eax, [0x1234] with 16-bit addressing is 5 bytes");
    Check(Length32({ 0x67, 0x8B, 0x00 }) == 3, "x86 mov eax, [bx+si] is 3 bytes");
    Check(Length32({ 0x67, 0x8B, 0x40, 0x08 }) == 4, "x86 mov eax, [bx+si+8] is 4 bytes");
    Check(Length32({ 0x67, 0x8B, 0x80, 0x34, 0x12 }) == 5, "x86 mov eax, [bx+si+0x1234] is 5 bytes, a disp16");
    Check(Length32({ 0x67, 0x8B, 0x04, 0x00 }) == 3, "x86 mov eax, [si] is 3 bytes: rm 4 is no SIB under 67");
    Check(Length32({ 0x67, 0xA1, 0x34, 0x12 }) == 4, "x86 mov eax, [moffs16] is 4 bytes");
    Check(Length32({ 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x90 }) == 15,
          "x86 sixteen prefixes are capped at 15");
    Check(Length32({ 0xE8, 0x00, 0x01 }) == 0, "x86 a call cut short of its rel32 is 0");
    std::vector<std::uint8_t> none;
    Check(LengthX86(none.data(), 0) == 0, "x86 no bytes at all is 0");

    Check(Length64({ 0x48, 0x8B, 0x05, 0x10, 0x00, 0x00, 0x00 }) == 7, "x64 mov rax, [rip+0x10] is 7 bytes");
    Check(Length64({ 0x48, 0xB8, 1, 2, 3, 4, 5, 6, 7, 8 }) == 10, "x64 mov rax, imm64 is 10 bytes");
    Check(Length64({ 0x62, 0xF1, 0x7C, 0x48, 0x28, 0xC1 }) == 1, "x64 EVEX, which DecodeX64 refuses, is hde64's 1-byte error");
    Check(Length64({ 0x40, 0x4C, 0x8B, 0xF0 }) == 2, "x64 a REX after a REX, which DecodeX64 refuses, is hde64's 2 bytes");
    Check(Length64({ 0xC5, 0xF8, 0x77 }) == 1, "x64 VEX is hde64's 1-byte error, not DecodeX64's 3");
    Check(Length64({ 0x66, 0x0F, 0x38, 0x00, 0xC1 }) == 3, "x64 pshufb is hde64's 3 bytes, not DecodeX64's 5");
    Check(Length64({ 0xF3, 0x0F, 0xB8, 0xC1 }) == 3, "x64 popcnt is hde64's 3-byte JMPE");
    Check(Length64({ 0x67, 0x8B, 0x06, 0x34, 0x12 }) == 5, "x64 67 8B 06 is hde64's 5 bytes with a disp16");
    Check(Length64({ 0x9F }) == 1, "x64 lahf is 1 byte");
    Check(Length64({ 0x48, 0x8B, 0x05, 0x10 }) == 0, "x64 a RIP load cut short is 0");
    Check(LengthX64(none.data(), 0) == 0, "x64 no bytes at all is 0");
}

void ImmediateTest() {
    std::printf("Immediates as encoded\n");
    Insn in;
    if (Check(Decode32({ 0x6A, 0x80 }, in), "x86 push -128 decodes"))
        Check(in.imm == -128 && in.imm_size == 1 && in.imm_raw == 0x80, "imm8: sign-extended imm, 1 byte, raw 0x80");
    if (Check(Decode32({ 0x66, 0x68, 0x00, 0x80 }, in), "x86 push word 0x8000 decodes"))
        Check(in.imm == -32768 && in.imm_size == 2 && in.imm_raw == 0x8000, "imm16: 2 bytes, raw 0x8000");
    if (Check(Decode32({ 0xC2, 0xFC, 0xFF }, in), "x86 ret 0xFFFC decodes"))
        Check(in.imm == -4 && in.imm_size == 2 && in.imm_raw == 0xFFFC, "ret's imm16 reads back unsigned from imm_raw");
    if (Check(Decode32({ 0x68, 0x00, 0x00, 0x00, 0x80 }, in), "x86 push 0x80000000 decodes"))
        Check(in.imm_size == 4 && in.imm_raw == 0x80000000u, "imm32: 4 bytes, raw 0x80000000");
    if (Check(Decode32({ 0xE8, 0xF0, 0xFF, 0xFF, 0xFF }, in, 0x402000), "x86 call back 16 decodes"))
        Check(!in.has_imm && in.imm_size == 4 && in.imm_raw == 0xFFFFFFF0u && in.target == 0x401FF5,
              "a rel32 has no has_imm, but imm_size and imm_raw carry its displacement");
    if (Check(Decode32({ 0xEB, 0xFE }, in), "x86 jmp $ decodes")) Check(in.imm_size == 1 && in.imm_raw == 0xFE, "a rel8 of 1 byte, raw 0xFE");
    if (Check(Decode32({ 0x8B, 0xC1 }, in), "x86 mov eax, ecx decodes")) Check(in.imm_size == 0 && in.imm_raw == 0, "no immediate");

    if (Check(Decode({ 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0x80 }, in), "x64 mov rax, imm64 decodes"))
        Check(in.imm_size == 8 && in.imm_raw == 0x8000000000000000ull, "imm64: 8 bytes, raw top bit set");
    if (Check(Decode({ 0x48, 0xC7, 0xC0, 0x00, 0x00, 0x00, 0x80 }, in), "x64 mov rax, imm32 decodes"))
        Check(in.imm == -2147483647LL - 1 && in.imm_size == 4 && in.imm_raw == 0x80000000u, "imm32 sign-extended in imm, as encoded in imm_raw");
    if (Check(Decode({ 0x66, 0xB8, 0x00, 0x80 }, in), "x64 mov ax, 0x8000 decodes"))
        Check(in.imm_size == 2 && in.imm_raw == 0x8000 && in.imm == -32768, "imm16: 2 bytes, raw 0x8000");
    if (Check(Decode({ 0x83, 0xC0, 0xFF }, in), "x64 add eax, -1 decodes")) Check(in.imm_size == 1 && in.imm_raw == 0xFF, "imm8: 1 byte, raw 0xFF");
    if (Check(Decode({ 0xC4, 0xE3, 0x79, 0x0F, 0xC1, 0x84 }, in), "x64 vpalignr with imm8 0x84 decodes"))
        Check(in.imm == -124 && in.imm_size == 1 && in.imm_raw == 0x84, "a VEX imm8 as encoded");
    if (Check(Decode({ 0x66, 0x0F, 0x3A, 0x0F, 0xC1, 0xF0 }, in), "x64 palignr with imm8 0xF0 decodes"))
        Check(in.imm_size == 1 && in.imm_raw == 0xF0, "an 0F 3A imm8 as encoded");
    if (Check(Decode({ 0xE9, 0x00, 0x00, 0x00, 0x80 }, in), "x64 jmp rel32 decodes"))
        Check(!in.has_imm && in.imm_size == 4 && in.imm_raw == 0x80000000u, "a rel32's displacement as encoded");
    if (Check(Decode({ 0x66, 0x0F, 0x38, 0x00, 0xC1 }, in), "x64 pshufb decodes")) Check(in.imm_size == 0, "no immediate");
}

void DisplacementTest() {
    std::printf("Displacement present or not\n");
    Insn in;
    if (Check(Decode32({ 0x8B, 0x01 }, in), "x86 mov eax, [ecx] decodes")) Check(!in.has_disp, "no displacement");
    if (Check(Decode32({ 0x8B, 0x41, 0x10 }, in), "x86 mov eax, [ecx+0x10] decodes")) Check(in.has_disp, "a disp8");
    if (Check(Decode32({ 0x8B, 0x45, 0x00 }, in), "x86 mov eax, [ebp+0] decodes")) Check(in.has_disp && in.disp == 0, "a disp8 of 0 is still there");
    if (Check(Decode32({ 0x8B, 0x0D, 0x78, 0x56, 0x34, 0x12 }, in), "x86 mov ecx, [0x12345678] decodes")) Check(in.has_disp, "a disp32");
    if (Check(Decode32({ 0x8B, 0x04, 0x24 }, in), "x86 mov eax, [esp] decodes")) Check(!in.has_disp, "SIB with no displacement");
    if (Check(Decode32({ 0x8B, 0x04, 0x8D, 0x00, 0x10, 0x40, 0x00 }, in), "x86 mov eax, [ecx*4+0x401000] decodes"))
        Check(in.has_disp, "SIB with no base has a disp32");
    if (Check(Decode32({ 0x8B, 0xC1 }, in), "x86 mov eax, ecx decodes")) Check(!in.has_disp, "a register form has none");
    if (Check(Decode32({ 0xA1, 0x78, 0x56, 0x34, 0x12 }, in), "x86 mov eax, [moffs32] decodes")) Check(!in.has_disp, "moffs has none; its address is imm");

    if (Check(Decode({ 0x48, 0x8B, 0x05, 0x10, 0x00, 0x00, 0x00 }, in), "x64 mov rax, [rip+0x10] decodes")) Check(in.has_disp, "RIP-relative has a disp32");
    if (Check(Decode({ 0x48, 0x8B, 0x01 }, in), "x64 mov rax, [rcx] decodes")) Check(!in.has_disp, "no displacement");
    if (Check(Decode({ 0x41, 0x8B, 0x45, 0x08 }, in), "x64 mov eax, [r13+8] decodes")) Check(in.has_disp, "a disp8");
    if (Check(Decode({ 0xC5, 0xF8, 0x28, 0x74, 0x24, 0x40 }, in), "x64 vmovaps xmm6, [rsp+0x40] decodes")) Check(in.has_disp, "a VEX disp8");
    if (Check(Decode({ 0xC5, 0xF8, 0x28, 0x06 }, in), "x64 vmovaps xmm0, [rsi] decodes")) Check(!in.has_disp, "a VEX form with none");
    if (Check(Decode({ 0x66, 0x0F, 0x38, 0x00, 0x40, 0x10 }, in), "x64 pshufb xmm0, [rax+0x10] decodes")) Check(in.has_disp, "an 0F 38 disp8");
    if (Check(Decode({ 0x66, 0x0F, 0x38, 0x00, 0x00 }, in), "x64 pshufb xmm0, [rax] decodes")) Check(!in.has_disp, "an 0F 38 form with none");
}

void AddressSizeTest() {
    std::printf("67 prefix\n");
    Insn in;
    Check(!Decode32({ 0x67, 0x8B, 0x40, 0x08 }, in), "x86 67 is still refused, and LengthX86 steps over it");
    if (Check(Decode({ 0x67, 0x8B, 0x40, 0x08 }, in), "x64 mov eax, [eax+8] decodes")) Check(in.addr_size && in.len == 4, "with addr_size, 4 bytes");
    if (Check(Decode({ 0x8B, 0x40, 0x08 }, in), "x64 mov eax, [rax+8] decodes")) Check(!in.addr_size, "with no addr_size");
    if (Check(Decode({ 0x67, 0x66, 0x0F, 0x38, 0x00, 0x40, 0x08 }, in), "x64 pshufb xmm0, [eax+8] decodes"))
        Check(in.addr_size && in.len == 7 && in.map == 2, "an 0F 38 form with addr_size, 7 bytes");
}

void Hde64ErrorTest() {
    std::printf("What hde64 calls an error\n");
    Insn in;
    if (Check(Decode({ 0x9F }, in), "x64 lahf decodes")) Check(in.hde64_error && in.len == 1, "hde64 calls it invalid in 64-bit mode");
    if (Check(Decode({ 0x0F, 0x0B }, in), "x64 ud2 decodes")) Check(in.hde64_error && in.len == 2, "hde64 calls it an error");
    if (Check(Decode({ 0xC5, 0xF8, 0x77 }, in), "x64 vzeroupper decodes")) Check(in.hde64_error, "hde64 refuses VEX");
    if (Check(Decode({ 0xC4, 0xE2, 0x70, 0xF2, 0xC2 }, in), "x64 andn decodes")) Check(in.hde64_error, "hde64 refuses three-byte VEX");
    if (Check(Decode({ 0x66, 0x0F, 0x38, 0x00, 0xC1 }, in), "x64 pshufb decodes")) Check(in.hde64_error, "hde64 refuses the 0F 38 map");
    if (Check(Decode({ 0x66, 0x0F, 0x3A, 0x0F, 0xC1, 0x08 }, in), "x64 palignr decodes")) Check(in.hde64_error, "hde64 refuses the 0F 3A map");
    if (Check(Decode({ 0xF3, 0x48, 0x0F, 0xB8, 0xC1 }, in), "x64 popcnt decodes")) Check(!in.hde64_error, "hde64 reads popcnt as JMPE with no error");
    if (Check(Decode({ 0x48, 0x8B, 0x05, 0x10, 0x00, 0x00, 0x00 }, in), "x64 mov rax, [rip+0x10] decodes")) Check(!in.hde64_error, "no error");
    if (Check(Decode({ 0x0F, 0x28, 0xC1 }, in), "x64 movaps xmm0, xmm1 decodes")) Check(!in.hde64_error, "no error");
    if (Check(Decode({ 0xF3, 0x0F, 0x28, 0xC1 }, in), "x64 F3 0F 28 decodes")) Check(in.hde64_error, "hde64's prefix table rejects F3 on 0F 28");
    if (Check(Decode({ 0xF0, 0xF3, 0x0F, 0xB8, 0xC1 }, in), "x64 lock popcnt decodes")) Check(in.hde64_error, "hde64 calls the lock an error");
    if (Check(Decode32({ 0x8B, 0x01 }, in), "x86 mov eax, [ecx] decodes")) Check(!in.hde64_error && !in.addr_size, "DecodeX86 sets neither");
}

}  // namespace

int RunX86DecodeTests() {
    g_failures = 0;
    VexTest();
    LegacyMapsTest();
    OperandTest();
    PrefixFieldTest();
    TruncatedTest();
    X86Test();
    LengthTest();
    ImmediateTest();
    DisplacementTest();
    AddressSizeTest();
    Hde64ErrorTest();
    return g_failures;
}
