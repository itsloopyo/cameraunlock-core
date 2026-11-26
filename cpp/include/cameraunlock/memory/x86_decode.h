// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstddef>
#include <cstdint>

// One x86 or x64 instruction decoded over the vendored Hacker Disassembler
// Engine (vendor/minhook/src/hde, hde32 and hde64 by Vyacheslav Patkov, BSD),
// reduced to what runtime discovery matches on. Both decoders are built in
// every build, so an x64 tool can read a 32-bit exe and the other way round.
// In cameraunlock_hooks.
namespace cameraunlock::memory {

// Registers are numbered 0..15 in encoding order (rax/eax, rcx, rdx, rbx, rsp,
// rbp, rsi, rdi, r8..r15), with REX.R / REX.B / REX.X already folded in.
struct Insn {
    static constexpr int kNone = -1;
    static constexpr int kRip = 16;

    // Where the instruction is, as the caller named it: an RVA, or in x86 an
    // absolute address.
    std::uint32_t rva = 0;
    std::uint8_t len = 0;
    // The first opcode byte, or 0x0F with opcode2 holding the opcode of the
    // 0F, 0F 38 or 0F 3A map that `map` names. VEX instructions report 0x0F
    // and their opcode in opcode2 the same way.
    std::uint8_t opcode = 0;
    std::uint8_t opcode2 = 0;
    std::uint8_t map = 0;  // 0 for one-byte opcodes, 1 for 0F, 2 for 0F 38, 3 for 0F 3A
    bool vex = false;
    // A VEX form whose implied prefix is 66 (the packed double and integer
    // forms), which opsize16 does not report.
    bool vex66 = false;
    // Any REX prefix: without one, byte registers 4 to 7 are ah, ch, dh and bh.
    bool rex = false;
    bool rex_w = false;
    // REX.B, which extends the register in the opcode byte itself (B8+r, 58+r).
    bool rex_b = false;
    // A 66 prefix: a dword opcode that is operating on a word.
    bool opsize16 = false;
    // The F2 or F3 prefix, or the one a VEX prefix implies; 0 for neither.
    std::uint8_t rep = 0;
    // The register VEX.vvvv names, which some VEX instructions write.
    std::uint8_t vvvv = 0;

    bool has_modrm = false;
    std::uint8_t mod = 0;
    std::uint8_t reg = 0;  // ModRM.reg, extended
    std::uint8_t rm = 0;   // ModRM.rm, extended; the register when mod == 3

    // Memory operand, when mod != 3.
    int base = kNone;
    int index = kNone;
    std::uint8_t scale = 1;  // of the index: 1, 2, 4 or 8
    std::int64_t disp = 0;

    // Sign-extended from its encoded width: an x86 address in an imm32 reads
    // back through static_cast<std::uint32_t>.
    bool has_imm = false;
    std::int64_t imm = 0;

    bool relative = false;
    std::uint32_t target = 0;  // branch target when relative, in the same terms as `rva`

    // The immediate as encoded: its width in bytes (1, 2, 4 or 8; 0 for none)
    // and its bits zero-extended from that width. Unlike has_imm and imm, these
    // also describe a relative branch's displacement. enter's two immediates
    // share hde's one slot, so for enter these and `imm` hold its imm16 with the
    // low byte overwritten by the nesting level.
    std::uint8_t imm_size = 0;
    std::uint64_t imm_raw = 0;
    // Displacement bytes are encoded: [reg+disp8], [reg+disp32], [disp32],
    // [rip+disp32], not [reg]. The moffs forms A0 to A3 have none; their
    // address is the immediate.
    bool has_disp = false;
    // A 67 (address-size) prefix. Only DecodeX64 returns one. Outside the 0F 38
    // and 0F 3A maps and popcnt, len, disp, has_disp and the operand are hde64's
    // reading, which uses 16-bit addressing that 64-bit mode does not: it takes
    // a disp16 for mod 0 rm 6 and for mod 2, no disp32 for mod 0 rm 5 (so
    // IsRipRelative is true with a wrong target), and an imm64 for 67 A0 to A3.
    // A caller that needs the length or the operand right refuses these.
    bool addr_size = false;
    // DecodeX64 only: hde64 flags these bytes as an error (F_ERROR). On the
    // forms DecodeX64 hands to hde64 that is an opcode, or an opcode with a
    // prefix, ModRM.reg or FPU form, that hde64's tables reject (lahf, sahf,
    // ud2, the 0F 01 system group, F3 0F 28), which hde64 still lengths. On the
    // forms DecodeX64 lengths itself it is every VEX, 0F 38 and 0F 3A form,
    // and a lock-prefixed popcnt; a plain popcnt hde64 reads as JMPE with no
    // error. A caller that stops where an hde64 walk stopped refuses these.
    bool hde64_error = false;

    bool HasMemory() const { return has_modrm && mod != 3; }
    bool IsRipRelative() const { return HasMemory() && base == kRip; }
    std::uint32_t RipTarget() const {
        return static_cast<std::uint32_t>(static_cast<std::int64_t>(rva) + len + disp);
    }
    // [base + disp] with no index register.
    bool IsMem(int base_reg) const { return HasMemory() && base == base_reg && index == kNone; }
    // [disp32] with neither base nor index: an absolute address in x86.
    bool IsAbsolute() const { return HasMemory() && base == kNone && index == kNone; }
};

// Decodes one x64 instruction at `code`, which sits at `rva` and has `avail`
// readable bytes; never reads past them. False for anything the decoder cannot
// length, which includes EVEX and a REX, VEX or EVEX prefix after another
// prefix; callers treat that as the end of what they can trust. hde64's gaps
// (VEX read as LES/LDS, no 0F 38 or 0F 3A tables, popcnt read as JMPE) are
// lengthed here.
bool DecodeX64(const std::uint8_t* code, std::size_t avail, std::uint32_t rva, Insn& out);

// Decodes one x86 (32-bit mode) instruction the same way. False for anything
// hde32 flags as an error, and for VEX and EVEX (C4, C5 or 62 followed by a
// byte of 0xC0 or more), which hde32 would length as LES, LDS or BOUND, and for
// a 67 prefix and its 16-bit addressing.
bool DecodeX86(const std::uint8_t* code, std::size_t avail, std::uint32_t at, Insn& out);

// hde32's and hde64's length for the instruction at `code`, whether or not
// DecodeX86 or DecodeX64 accepts it: 1 to 15, where hde caps anything longer
// at 15. This is how far a walk built on hde stepped over bytes the decoder
// refuses, so a walk ported onto DecodeX86 or DecodeX64 starts the
// instructions after them where it always did. hde32 reads a 67 prefix with
// 16-bit addressing, so LengthX86 lengths those right, but it lengths the 0F 38
// and 0F 3A maps wrong and VEX and EVEX as LES, LDS and BOUND. hde64 lengths
// VEX, EVEX, 0F 38, 0F 3A, popcnt, 67 forms and a REX before another prefix
// wrong. Both give hde's length all the same. 0 when the instruction runs past
// `avail` (with 15 or more bytes it never does: hde's cap of 15 applies first);
// never reads past it.
unsigned LengthX86(const std::uint8_t* code, std::size_t avail);
unsigned LengthX64(const std::uint8_t* code, std::size_t avail);

}  // namespace cameraunlock::memory
