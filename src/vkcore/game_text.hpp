#pragma once

// The loaded game module, read in place: its .text and .rdata, unique signature matches, RIP-relative
// targets, strings, MSVC RTTI class names and function starts. Shared by the camera hooks
// (view_hook.cpp) and the multiplayer guard (mp_guard.cpp).
//
// Every address handed out or read here is checked against the module's bounds first. Nothing is
// written.

#include "engine/eternal/resolver/pattern.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace evr::vkcore {

struct GameImage {
    const std::byte* base = nullptr; // module base
    std::size_t size = 0;            // SizeOfImage
    std::uint32_t timestamp = 0;     // PE TimeDateStamp
    resolver::ByteSpan text;         // .text in memory
    resolver::ByteSpan rdata;        // .rdata in memory (may be empty)

    [[nodiscard]] bool contains(const std::byte* p, std::size_t n = 1) const {
        return p >= base && n <= size && static_cast<std::size_t>(p - base) <= size - n;
    }
    [[nodiscard]] bool inText(const std::byte* p, std::size_t n = 1) const {
        return p >= text.data() && n <= text.size() &&
               static_cast<std::size_t>(p - text.data()) <= text.size() - n;
    }
    [[nodiscard]] unsigned rva(const std::byte* p) const { return static_cast<unsigned>(p - base); }
};

// The exe's module (GetModuleHandle(nullptr)); false when its headers or .text cannot be read. `tag`
// prefixes the log line that describes it.
bool locateGameImage(GameImage& image, const char* tag);

// The single match of `signature` in .text, or nullptr (logged under `tag` with `name`) for none or
// several.
const std::byte* findUnique(const GameImage& image, const char* tag, const char* name, const char* signature);

std::int32_t readI32(const std::byte* at);

// The target of a RIP-relative operand: `dispAt` holds the disp32 and `nextInstruction` is the address
// after the instruction. nullptr when the target lies outside the module.
const std::byte* ripTarget(const GameImage& image, const std::byte* dispAt, const std::byte* nextInstruction);

// The NUL-terminated string at `p` if it lies in the module and ends within `maxLength` bytes.
std::string_view stringAt(const GameImage& image, const std::byte* p, std::size_t maxLength = 256);

// The MSVC RTTI type name of the class whose vtable starts at `vtable` (".?AVidMenu@@"), or empty when
// the complete-object locator before it does not check out.
std::string_view rttiName(const GameImage& image, const std::byte* vtable);

// The start of the function containing `code`, following chained unwind entries to the primary one.
// nullptr when the address has no unwind entry.
const std::byte* functionStart(const GameImage& image, const std::byte* code);

// The only exact occurrence of `text` (with its terminating NUL, preceded by a NUL) in .rdata, or
// nullptr.
const std::byte* findUniqueString(const GameImage& image, std::string_view text);

// Every `lea r64, [rip+disp32]` in .text (REX.W, opcode 8D, ModRM mod 00 rm 101) whose target is
// `target`: the instruction addresses.
std::vector<const std::byte*> findLeaReferences(const GameImage& image, const std::byte* target);

} // namespace evr::vkcore
