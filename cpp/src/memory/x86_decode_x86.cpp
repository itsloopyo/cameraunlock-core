// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/memory/x86_decode.h>

#include <cstring>

#include "hde_names.h"
#include "../../../vendor/minhook/src/hde/hde32.h"

namespace cameraunlock::memory {

bool DecodeX86(const std::uint8_t* code, std::size_t avail, std::uint32_t at, Insn& out) {
    out = Insn{};
    if (avail == 0) return false;
    // In 32-bit mode C4, C5 and 62 are LES, LDS and BOUND only with a memory
    // operand; followed by a byte of 0xC0 or more they begin VEX or EVEX, which
    // hde32 knows nothing of.
    if ((code[0] == 0xC4 || code[0] == 0xC5 || code[0] == 0x62) && (avail < 2 || code[1] >= 0xC0)) return false;

    // hde32 reads up to sixteen prefixes and then an instruction of at most a
    // few more bytes; it decodes from a zero-padded copy, so it never reads past
    // the range.
    std::uint8_t buf[48] = {};
    std::memcpy(buf, code, avail < sizeof(buf) ? avail : sizeof(buf));
    hde32s hs;
    const unsigned len = hde32_disasm(buf, &hs);
    if ((hs.flags & F_ERROR) || len == 0 || len > avail) return false;

    out.rva = at;
    out.len = static_cast<std::uint8_t>(len);
    out.opcode = hs.opcode;
    out.opcode2 = hs.opcode == 0x0F ? hs.opcode2 : 0;
    out.map = hs.opcode == 0x0F ? 1 : 0;
    out.rep = hs.p_rep;
    out.opsize16 = (hs.flags & F_PREFIX_66) != 0;
    if (hs.flags & F_MODRM) {
        out.has_modrm = true;
        out.mod = hs.modrm_mod;
        out.reg = hs.modrm_reg;
        out.rm = hs.modrm_rm;
        if (out.mod != 3) {
            if (hs.modrm_rm == 4) {
                out.base = (hs.sib_base == 5 && out.mod == 0) ? Insn::kNone : hs.sib_base;
                out.index = hs.sib_index == 4 ? Insn::kNone : hs.sib_index;
                out.scale = static_cast<std::uint8_t>(1u << hs.sib_scale);
            } else if (!(hs.modrm_rm == 5 && out.mod == 0)) {
                out.base = hs.modrm_rm;
            }
        }
    }
    if (hs.flags & F_DISP8) out.disp = static_cast<std::int8_t>(hs.disp.disp8);
    if (hs.flags & F_DISP16) out.disp = static_cast<std::int16_t>(hs.disp.disp16);
    if (hs.flags & F_DISP32) out.disp = static_cast<std::int32_t>(hs.disp.disp32);

    std::int64_t imm = 0;
    bool has_imm = true;
    if (hs.flags & F_IMM32) imm = static_cast<std::int32_t>(hs.imm.imm32);
    else if (hs.flags & F_IMM16) imm = static_cast<std::int16_t>(hs.imm.imm16);
    else if (hs.flags & F_IMM8) imm = static_cast<std::int8_t>(hs.imm.imm8);
    else has_imm = false;

    if (hs.flags & F_RELATIVE) {
        out.relative = true;
        out.target = static_cast<std::uint32_t>(static_cast<std::int64_t>(at) + len + imm);
    } else {
        out.has_imm = has_imm;
        out.imm = imm;
    }
    return true;
}

}  // namespace cameraunlock::memory
