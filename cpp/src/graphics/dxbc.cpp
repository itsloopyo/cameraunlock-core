// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "cameraunlock/graphics/dxbc.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace cameraunlock::graphics::detail {
void ComputeHashRetail(const unsigned char*, unsigned, unsigned char*);
}

namespace cameraunlock::graphics {
namespace {

constexpr std::uint32_t kDxbc = 0x43425844;
constexpr std::uint32_t kShex = 0x58454853;
constexpr std::uint32_t kShdr = 0x52444853;
constexpr std::uint32_t kIsgn = 0x4e475349;
constexpr std::uint32_t kIsg1 = 0x31475349;
constexpr std::uint32_t kStat = 0x54415453;
constexpr std::uint32_t kCustomData = 53;
constexpr std::uint32_t kDclTemps = 104;

void Require(bool condition, const char* diagnostic) {
    if (!condition) throw std::invalid_argument(diagnostic);
}

std::uint32_t Word(const std::uint8_t* bytes, std::size_t size, std::size_t at) {
    Require(at <= size && size - at >= 4, "DXBC: truncated word");
    std::uint32_t result;
    std::memcpy(&result, bytes + at, sizeof(result));
    return result;
}

DxbcOperand Operand(const std::vector<std::uint32_t>& code,
                    std::size_t& at, std::size_t end, unsigned depth = 0) {
    Require(depth < 16 && at < end, "DXBC: truncated or recursive operand");
    DxbcOperand result{};
    result.begin = at;
    auto extension = code[at++];
    const auto token = extension;
    result.type = (token >> 12) & 255;
    result.dimensions = (token >> 20) & 3;
    while (extension & 0x80000000u) {
        Require(at < end, "DXBC: truncated operand extension");
        extension = code[at++];
    }
    result.indicesBegin = at;
    for (unsigned i = 0; i < result.dimensions; ++i) {
        const auto representation = (token >> (22 + i * 3)) & 7;
        Require(representation <= 4, "DXBC: unsupported index representation");
        if (representation == 0 || representation == 1 ||
            representation == 3 || representation == 4) {
            Require(at < end, "DXBC: truncated operand index");
            result.indices[i] = code[at++];
            if (representation == 1 || representation == 4) {
                Require(at < end, "DXBC: truncated 64-bit index");
                ++at;
                result.direct = false;
            }
        }
        if (representation >= 2) {
            Operand(code, at, end, depth + 1);
            result.direct = false;
        }
    }
    if (result.type == 4 || result.type == 5) {
        const auto components = token & 3;
        Require(components == 1 || components == 2,
                "DXBC: invalid immediate component count");
        const std::size_t count = (components == 1 ? 1 : 4) *
                                  (result.type == 5 ? 2 : 1);
        Require(count <= end - at, "DXBC: truncated immediate operand");
        at += count;
    }
    result.end = at;
    return result;
}

bool Declaration(std::uint32_t opcode) {
    return (opcode >= 88 && opcode <= 106) ||
           (opcode >= 143 && opcode <= 162) || opcode == 206;
}

} // namespace

DxbcContainer ReadDxbc(const void* data, std::size_t size) {
    Require(data && size >= 32 && size <= std::numeric_limits<std::uint32_t>::max(),
            "DXBC: invalid container size");
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    Require(Word(bytes, size, 0) == kDxbc, "DXBC: invalid signature");
    Require(Word(bytes, size, 20) == 1, "DXBC: unsupported container version");
    Require(Word(bytes, size, 24) == size, "DXBC: size mismatch");
    const auto count = Word(bytes, size, 28);
    Require(count <= (size - 32) / 4, "DXBC: truncated chunk table");
    const std::size_t headerEnd = 32 + static_cast<std::size_t>(count) * 4;
    DxbcContainer result{};
    result.codeChunk = count;
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto offset = Word(bytes, size, 32 + i * 4);
        Require(offset % 4 == 0 && offset >= headerEnd,
                "DXBC: invalid chunk offset");
        const auto kind = Word(bytes, size, offset);
        const auto length = Word(bytes, size, static_cast<std::size_t>(offset) + 4);
        const std::size_t begin = static_cast<std::size_t>(offset) + 8;
        Require(begin <= size && length <= size - begin, "DXBC: truncated chunk");
        ranges.emplace_back(offset, begin + length);
        result.chunks.push_back({kind, {bytes + begin, bytes + begin + length}});
        if (kind == kShex || kind == kShdr) {
            Require(result.codeChunk == count, "DXBC: multiple shader programs");
            Require(length >= 8 && length % 4 == 0, "DXBC: invalid program size");
            result.codeChunk = i;
            result.code.resize(length / 4);
            std::memcpy(result.code.data(), bytes + begin, length);
            Require(result.code[1] == result.code.size(), "DXBC: program size mismatch");
        }
        if (kind == kIsgn || kind == kIsg1) {
            const auto* signature = bytes + begin;
            const auto entries = Word(signature, length, 0);
            const std::size_t stride = kind == kIsgn ? 24 : 32;
            Require(length >= 8 && entries <= (length - 8) / stride,
                    "DXBC: truncated input signature");
            for (std::uint32_t j = 0; j < entries; ++j) {
                const auto entry = 8 + j * stride + (kind == kIsgn ? 0 : 4);
                const auto nameAt = Word(signature, length, entry);
                Require(nameAt < length, "DXBC: invalid semantic offset");
                const auto* name = reinterpret_cast<const char*>(signature + nameAt);
                const auto* terminator = static_cast<const char*>(
                    std::memchr(name, 0, length - nameAt));
                Require(terminator != nullptr, "DXBC: unterminated semantic name");
                result.inputs.push_back({std::string(name, terminator),
                    Word(signature, length, entry + 4),
                    Word(signature, length, entry + 12),
                    Word(signature, length, entry + 16)});
            }
        }
    }
    std::sort(ranges.begin(), ranges.end());
    for (std::size_t i = 1; i < ranges.size(); ++i)
        Require(ranges[i - 1].second <= ranges[i].first, "DXBC: overlapping chunks");
    return result;
}

std::vector<DxbcInstruction> ReadDxbcInstructions(
    const std::vector<std::uint32_t>& code) {
    Require(code.size() >= 2 && code[1] == code.size(), "DXBC: invalid program length");
    Require(((code[0] >> 4) & 15) <= 5, "DXBC: unsupported shader model");
    std::vector<DxbcInstruction> result;
    for (std::size_t at = 2; at < code.size();) {
        const auto begin = at;
        const auto token = code[at++];
        const auto opcode = token & 2047;
        std::size_t length = (token >> 24) & 127;
        if (opcode == kCustomData) {
            Require(at < code.size(), "DXBC: truncated custom data header");
            length = code[at];
            Require(length >= 2, "DXBC: invalid custom data length");
        }
        Require(length && length <= code.size() - begin, "DXBC: invalid instruction length");
        const auto end = begin + length;
        DxbcInstruction instruction{begin, end, opcode,
                                    Declaration(opcode) || opcode == kCustomData, {}};
        if (opcode != kCustomData) {
            auto extension = token;
            while (extension & 0x80000000u) {
                Require(at < end, "DXBC: truncated opcode extension");
                extension = code[at++];
            }
            if (!instruction.declaration) {
                Require(opcode != 120 && opcode < 209,
                        "DXBC: unsupported executable opcode");
                while (at < end) instruction.operands.push_back(Operand(code, at, end));
            }
        }
        result.push_back(std::move(instruction));
        at = end;
    }
    return result;
}

std::vector<std::uint32_t> RedirectDxbcConstants(
    const std::vector<std::uint32_t>& code,
    const std::vector<DxbcConstantRedirect>& redirects,
    const std::vector<std::uint32_t>& prefix,
    std::uint32_t extraTemporaries) {
    const auto instructions = ReadDxbcInstructions(code);
    std::vector<std::uint32_t> prefixProgram{code[0],
        static_cast<std::uint32_t>(prefix.size() + 2)};
    prefixProgram.insert(prefixProgram.end(), prefix.begin(), prefix.end());
    for (const auto& instruction : ReadDxbcInstructions(prefixProgram))
        Require(!instruction.declaration, "DXBC: prefix contains a declaration");
    std::uint32_t temporaries = 0;
    bool haveTemps = false;
    for (const auto& instruction : instructions) {
        if (instruction.opcode != kDclTemps) continue;
        Require(!haveTemps && instruction.end - instruction.begin == 2,
                "DXBC: invalid temporary declaration");
        haveTemps = true;
        temporaries = code[instruction.begin + 1];
    }
    Require(temporaries <= 4096 && extraTemporaries <= 4096 - temporaries,
            "DXBC: temporary register limit exceeded");
    for (const auto& redirect : redirects)
        Require(redirect.temporary >= temporaries &&
                redirect.temporary < temporaries + extraTemporaries,
                "DXBC: redirect does not target a new temporary");
    std::vector<std::uint32_t> output{code[0], 0};
    bool inserted = false;
    for (const auto& instruction : instructions) {
        if (!instruction.declaration && !inserted) {
            if (!haveTemps) output.insert(output.end(), {0x02000068, extraTemporaries});
            output.insert(output.end(), prefix.begin(), prefix.end());
            inserted = true;
        }
        const auto start = output.size();
        if (instruction.declaration) {
            Require(!inserted, "DXBC: declaration after executable code");
            output.insert(output.end(), code.begin() + instruction.begin,
                          code.begin() + instruction.end);
            if (instruction.opcode == kDclTemps) output.back() += extraTemporaries;
            continue;
        }
        auto cursor = instruction.begin;
        for (const auto& operand : instruction.operands) {
            if (operand.type != 8 || operand.dimensions != 2 || !operand.direct) continue;
            const auto redirect = std::find_if(redirects.begin(), redirects.end(),
                [&](const DxbcConstantRedirect& r) {
                    return r.buffer == operand.indices[0] && r.row == operand.indices[1];
                });
            if (redirect == redirects.end()) continue;
            output.insert(output.end(), code.begin() + cursor, code.begin() + operand.begin);
            output.push_back((code[operand.begin] & ~0x7ffff000u) | 0x00100000u);
            output.insert(output.end(), code.begin() + operand.begin + 1,
                          code.begin() + operand.indicesBegin);
            output.push_back(redirect->temporary);
            cursor = operand.end;
        }
        output.insert(output.end(), code.begin() + cursor, code.begin() + instruction.end);
        Require(output.size() - start <= 127, "DXBC: instruction exceeds length limit");
        output[start] = (output[start] & ~0x7f000000u) |
                        (static_cast<std::uint32_t>(output.size() - start) << 24);
    }
    Require(inserted, "DXBC: shader has no executable code");
    output[1] = static_cast<std::uint32_t>(output.size());
    return output;
}

std::vector<std::uint32_t> WriteDxbc(
    const DxbcContainer& container, const std::vector<std::uint32_t>& code) {
    Require(container.codeChunk < container.chunks.size(), "DXBC: no tokenized program");
    ReadDxbcInstructions(code);
    std::uint32_t count = 0;
    for (const auto& chunk : container.chunks) if (chunk.kind != kStat) ++count;
    std::vector<std::uint32_t> output(8 + count);
    output[0] = kDxbc;
    output[5] = 1;
    output[7] = count;
    std::size_t entry = 8;
    for (std::size_t i = 0; i < container.chunks.size(); ++i) {
        const auto& chunk = container.chunks[i];
        if (chunk.kind == kStat) continue;
        const void* bytes = chunk.bytes.data();
        std::size_t length = chunk.bytes.size();
        if (i == container.codeChunk) { bytes = code.data(); length = code.size() * 4; }
        Require(output.size() * 4 + length + 12 <= std::numeric_limits<std::uint32_t>::max(),
                "DXBC: rewritten container too large");
        output[entry++] = static_cast<std::uint32_t>(output.size() * 4);
        output.push_back(chunk.kind);
        output.push_back(static_cast<std::uint32_t>(length));
        const auto payload = output.size();
        output.resize(payload + (length + 3) / 4);
        if (length) std::memcpy(output.data() + payload, bytes, length);
    }
    output[6] = static_cast<std::uint32_t>(output.size() * 4);
    auto* bytes = reinterpret_cast<unsigned char*>(output.data());
    detail::ComputeHashRetail(bytes + 20, output[6] - 20, bytes + 4);
    return output;
}

} // namespace cameraunlock::graphics
