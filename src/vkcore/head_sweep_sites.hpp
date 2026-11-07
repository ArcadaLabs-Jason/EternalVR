#pragma once

// The game code the head sweep calls and hooks, and the fields it reads, found by signature
// (head_sweep.cpp, docs/rig-findings/collision-query.md). Every displacement read out of the code is
// checked against the instruction it belongs to.

#include "vkcore/game_text.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace evr::vkcore {

struct SweepSites {
    const std::byte* translation = nullptr;       // idHavokCollision::Translation (RVA 0x4FC0B0)
    const std::byte* mapInstanceGlobal = nullptr; // the map-instance global (RVA 0x45F7370)
    const std::byte* postStep = nullptr;          // the end of the step's post-step job (RVA 0x502F9C)
    std::int32_t weaponShapeDisp = 0;             // the map instance's weapon-trace shape field; 0: unknown
    std::size_t stepFlagDisp = 0;                 // physics object: the step-in-flight byte (0x21089)
    std::size_t taskDisp = 0;      // physics object: the pending task the post-step waits for (0x21070)
    std::size_t taskStateDisp = 0; // that task: its state word (0x24)
};

// Logs under `tag` what is missing; nullopt when the sweep cannot be made.
std::optional<SweepSites> findSweepSites(const GameImage& image, const char* tag);

} // namespace evr::vkcore
