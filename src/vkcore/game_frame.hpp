#pragma once

// The game's frame number and the frame the player's hands were last placed in, so the viewmodel hook and the
// off-hand hook can tell which game frame a placement and an arm belong to.
//
// idHands' update of its render model (RVA 0x1380F70, build 25216728; docs/rig-findings/input-aim.md section
// 3) runs idHands::UpdatePosition once per game frame: it reads the frame as [[rip + disp] + 0x125A60 + 8]
// (the getter at RVA 0x398510 is `mov eax, [rcx + 8]; ret`), skips the update when idHands +0xFC0 already
// holds it, and otherwise stores it there and calls UpdatePosition [static: the code below, matched once in
// .text at install time; idHands::Update (RVA 0x136D840), which calls UpdateWeaponLagJointMods, reads the
// same global the same way]. So within a game frame, +0xFC0 equals the frame from the moment the viewmodel
// hook (inside UpdatePosition) runs.
//
// Fail closed: without the match nothing is read (nullopt).

#include "vkcore/game_text.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace evr::vkcore::controllers::game_frame {

// Finds the frame's global through the code above (once; logged). False when it does not match.
bool install(const GameImage& image);

// The game frame now, or nullopt (not installed, or not readable now).
std::optional<std::int32_t> now();

// The game frame `hands` were last placed in by UpdatePosition, or nullopt (not installed or unreadable).
std::optional<std::int32_t> placedIn(const std::byte* hands);

} // namespace evr::vkcore::controllers::game_frame
