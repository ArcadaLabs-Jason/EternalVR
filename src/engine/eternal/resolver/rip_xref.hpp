#pragma once

// Finds code that references a known address through RIP-relative addressing.
//
// x64 code reaches .rdata strings and globals as [rip + disp32], where the displacement is relative
// to the address of the NEXT instruction. Given the RVA of an anchor string, this finds the
// instructions that load or take its address, which leads into the functions using it.
//
// Recognised encodings (7 bytes each):
//     REX   opcode  ModRM          disp32
//     48|4C 8D|8B   00 reg 101     xx xx xx xx
// i.e. `lea r64, [rip+disp32]` and `mov r64, [rip+disp32]` with REX.W, optionally REX.R for r8-r15.
// That covers how MSVC takes the address of a string literal or loads a global pointer.
//
// Limitations: this is a byte-pattern scan, not a disassembler.
//   - Other forms are not found: 32-bit operands (no REX.W), REX bytes with X or B set (legal but not
//     emitted by MSVC here), other opcodes (cmp, call, jmp, SSE moves), and prefixed instructions.
//   - Every byte offset is tried, so in principle a match can start inside another instruction. The
//     displacement must resolve exactly to the target, which makes accidental hits unlikely, but
//     callers verifying a critical anchor should confirm the hit by other means.

#include "engine/eternal/resolver/pe_image.hpp"

#include <cstdint>
#include <vector>

namespace evr::resolver {

enum class RipInstruction : std::uint8_t {
    Lea, // 8D: the target's address is the result.
    Mov, // 8B: the target is dereferenced.
};

struct RipReference {
    std::uint32_t instructionRva = 0;
    RipInstruction kind = RipInstruction::Lea;
};

// Encoded length of every recognised instruction.
inline constexpr std::uint32_t kRipInstructionLength = 7;

// References to `targetRva` from within `codeSection`, in ascending order of instruction RVA.
std::vector<RipReference>
findRipReferences(const PeImage& image, const Section& codeSection, std::uint32_t targetRva);

} // namespace evr::resolver
