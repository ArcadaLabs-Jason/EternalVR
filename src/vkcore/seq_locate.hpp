#pragma once

// Locating and cross-checking the engine code Route S hooks (docs/VR_STEREO.md, "Fail closed"; every
// address in docs/rig-findings/stereo-routes.md section 2, Steam build 25216728). Nothing here writes to
// the game.

#include "vkcore/game_code.hpp"

#include <cstddef>

namespace evr::vkcore {

struct SeqEngine {
    void** slot = nullptr;                // the .data slot the frame middle job queues the frame-end job from
    const std::byte* frameEnd = nullptr;  // the frame-end job (the slot's value)
    const std::byte* renderOne = nullptr; // render one frame synchronously (vtable slot 0x50)
    std::byte* renderSystem = nullptr;    // the render system object
    std::byte* const* backend = nullptr;  // the render backend pointer (its + 0xB0 counts backend frames)
    const std::byte* const* swapIntervalCvar = nullptr;
    const std::byte* prevHookSite = nullptr; // the instruction after the previous-matrix store call
};

// Every check of docs/VR_STEREO.md "Fail closed"; false (with the reason logged) leaves the game as it
// is.
bool locateSeqEngine(const GameText& text, SeqEngine& out);

} // namespace evr::vkcore
