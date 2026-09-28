#pragma once

// Locating code in the loaded game module by signature (docs/rig-findings/engine-facts.md section 5).

#include "engine/eternal/resolver/byte_reader.hpp"
#include "vkcore/game_text.hpp" // readI32

#include <cstddef>
#include <cstdint>

namespace evr::vkcore {

struct GameText {
    const std::byte* base = nullptr; // module base
    resolver::ByteSpan bytes;        // .text in memory
    std::uint32_t timestamp = 0;     // PE header TimeDateStamp
};

// The .text section of the process's main module; false when it cannot be read.
bool findGameText(GameText& text);

// The single match of `signature` in .text, or nullptr (logged under `name`) for none or several.
const std::byte* findUniqueInText(const GameText& text, const char* name, const char* signature);

// The target of the rel32 call or jump whose opcode byte is at `instruction` (E8 / E9), or nullptr for
// another opcode.
const std::byte* relativeTarget(const std::byte* instruction);

// The address a RIP-relative operand refers to: `disp` points at the 32-bit displacement and
// `instructionEnd` is the address of the next instruction.
const std::byte* ripTarget(const std::byte* disp, const std::byte* instructionEnd);

inline std::uint32_t rvaOf(const GameText& text, const void* at) {
    return static_cast<std::uint32_t>(static_cast<const std::byte*>(at) - text.base);
}

} // namespace evr::vkcore
