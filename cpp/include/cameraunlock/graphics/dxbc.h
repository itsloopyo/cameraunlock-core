// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cameraunlock::graphics {

struct DxbcChunk {
    std::uint32_t kind;
    std::vector<std::uint8_t> bytes;
};

struct DxbcInput {
    std::string semantic;
    std::uint32_t semanticIndex;
    std::uint32_t componentType;
    std::uint32_t registerIndex;
};

struct DxbcContainer {
    std::vector<DxbcChunk> chunks;
    std::vector<std::uint32_t> code;
    std::vector<DxbcInput> inputs;
    std::size_t codeChunk;
};

struct DxbcOperand {
    std::size_t begin;
    std::size_t indicesBegin;
    std::size_t end;
    std::uint32_t type;
    std::uint32_t dimensions;
    std::array<std::uint32_t, 3> indices{};
    bool direct = true;
};

struct DxbcInstruction {
    std::size_t begin;
    std::size_t end;
    std::uint32_t opcode;
    bool declaration;
    std::vector<DxbcOperand> operands;
};

struct DxbcConstantRedirect {
    std::uint32_t buffer;
    std::uint32_t row;
    std::uint32_t temporary;
};

// Malformed containers and unsupported operand encodings throw invalid_argument.
DxbcContainer ReadDxbc(const void* bytes, std::size_t size);
std::vector<DxbcInstruction> ReadDxbcInstructions(
    const std::vector<std::uint32_t>& code);
std::vector<std::uint32_t> RedirectDxbcConstants(
    const std::vector<std::uint32_t>& code,
    const std::vector<DxbcConstantRedirect>& redirects,
    const std::vector<std::uint32_t>& prefix,
    std::uint32_t extraTemporaries);
std::vector<std::uint32_t> WriteDxbc(
    const DxbcContainer& container, const std::vector<std::uint32_t>& code);

} // namespace cameraunlock::graphics
