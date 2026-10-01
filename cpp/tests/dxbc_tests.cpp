// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "cameraunlock/graphics/dxbc.h"

#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace cameraunlock::graphics;

namespace {
int checks = 0;
void Check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class F> void Reject(F operation) {
    bool rejected = false;
    try { operation(); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "malformed input was accepted");
}
std::vector<std::uint32_t> Program(std::initializer_list<std::uint32_t> body) {
    std::vector<std::uint32_t> result{0x00010050, 0, 0x02000068, 1};
    result.insert(result.end(), body);
    result.push_back(0x0100003e);
    result[1] = static_cast<std::uint32_t>(result.size());
    return result;
}
std::vector<std::uint32_t> Container(const std::vector<std::uint32_t>& code) {
    DxbcContainer container{{{0x58454853, {}}, {0x54534554, {1, 2, 3}}}, {}, {}, 0};
    return WriteDxbc(container, code);
}
const std::vector<std::uint32_t> prefix{
    0x08000036, 0x001000f2, 1, 0x00004002, 1, 2, 3, 4};
}

int main() {
    const auto code = Program({
        0x0b000000, 0x001000f2, 0, 0x002081b6, 0, 16,
        0x00004002, 0x00208e46, 0, 16, 0});
    const auto rewritten = RedirectDxbcConstants(code, {{0, 16, 1}}, prefix, 1);
    const auto instructions = ReadDxbcInstructions(rewritten);
    const auto& add = instructions[2];
    Check(rewritten[3] == 2, "temporary declaration not extended");
    Check(add.opcode == 0 && add.operands.size() == 3, "ADD operands lost");
    const auto& source = add.operands[1];
    Check(source.type == 0 && source.dimensions == 1 && source.indices[0] == 1,
          "constant read not redirected");
    Check((rewritten[source.begin] & 0xfff) == (0x002081b6 & 0xfff),
          "source swizzle changed");
    const auto& literal = add.operands[2];
    Check(literal.type == 4 && rewritten[literal.begin + 1] == 0x00208e46 &&
          rewritten[literal.begin + 3] == 16, "register-shaped literal corrupted");

    const auto negated = Program({0x07000036, 0x001000f2, 0,
                                 0x80208e46, 0x41, 0, 16});
    const auto negatedResult = RedirectDxbcConstants(negated, {{0, 16, 1}}, prefix, 1);
    const auto negatedOps = ReadDxbcInstructions(negatedResult);
    const auto& negatedRead = negatedOps[2].operands[1];
    Check(negatedResult[negatedRead.begin] & 0x80000000u, "operand extension dropped");
    Check(negatedResult[negatedRead.begin + 1] == 0x41, "negation modifier changed");

    const auto relative = Program({0x08000036, 0x001000f2, 0,
                                  0x06208e46, 0, 16, 0x0010000a, 0});
    const auto relativeResult = RedirectDxbcConstants(relative, {{0, 16, 1}}, prefix, 1);
    const auto relativeOps = ReadDxbcInstructions(relativeResult);
    Check(relativeOps[2].operands[1].type == 8 && !relativeOps[2].operands[1].direct,
          "relative constant address was rewritten");

    const auto custom = Program({53, 6, 0x00208e46, 0, 16, 0,
                                0x06000036, 0x001000f2, 0, 0x00208e46, 0, 16});
    const auto customResult = RedirectDxbcConstants(custom, {{0, 16, 1}}, prefix, 1);
    Check(customResult[6] == 0x00208e46 && customResult[8] == 16,
          "custom data corrupted");
    Check(ReadDxbcInstructions(customResult)[2].opcode == 54,
          "prefix inserted before custom-data declaration");

    const auto blob = Container(rewritten);
    const auto parsed = ReadDxbc(blob.data(), blob.size() * 4);
    Check(parsed.code == rewritten, "program round trip failed");
    Check(parsed.chunks[1].bytes == std::vector<std::uint8_t>({1, 2, 3}),
          "unaligned opaque payload changed");
    Check(WriteDxbc(parsed, parsed.code) == blob, "container serialization not deterministic");
    Check(blob[1] || blob[2] || blob[3] || blob[4], "container digest absent");
    std::vector<std::uint8_t> unaligned(blob.size() * 4 + 1);
    std::memcpy(unaligned.data() + 1, blob.data(), blob.size() * 4);
    Check(ReadDxbc(unaligned.data() + 1, blob.size() * 4).code == rewritten,
          "unaligned input rejected");

    for (std::size_t size = 0; size < blob.size() * 4; ++size)
        Reject([&] { ReadDxbc(blob.data(), size); });
    for (const auto index : {0u, 5u, 6u, 7u, 8u, 11u}) {
        auto broken = blob;
        broken[index] = 0xffffffff;
        Reject([&] { ReadDxbc(broken.data(), broken.size() * 4); });
    }
    auto overlapping = blob;
    overlapping[9] = overlapping[8];
    Reject([&] { ReadDxbc(overlapping.data(), overlapping.size() * 4); });
    auto badInstruction = code;
    badInstruction[4] = 0;
    Reject([&] { ReadDxbcInstructions(badInstruction); });
    badInstruction[4] = 0x7f000000;
    Reject([&] { ReadDxbcInstructions(badInstruction); });
    auto badExtension = Program({0x04000036, 0x001000f2, 0, 0x80208e46});
    Reject([&] { ReadDxbcInstructions(badExtension); });
    auto shortLiteral = Program({0x05000036, 0x001000f2, 0, 0x00004002, 1});
    Reject([&] { ReadDxbcInstructions(shortLiteral); });
    Reject([&] { RedirectDxbcConstants(code, {{0, 16, 0}}, prefix, 1); });
    Reject([&] { RedirectDxbcConstants(code, {{0, 16, 1}}, prefix, 4096); });
    Reject([&] { RedirectDxbcConstants(code, {{0, 16, 1}}, {0x02000068, 1}, 1); });
    std::cout << checks << " DXBC checks passed\n";
}

