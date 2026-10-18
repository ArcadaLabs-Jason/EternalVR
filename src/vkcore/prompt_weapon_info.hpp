#pragma once

// The HUD's weapon info row names the equipment, Flame Belch, chainsaw, Crucible and switch-equipment buttons
// in yellow boxes above their icons (rig 2026-10-17: "Left Grip", "Right Stick Up" and "Left Trigger" over
// three icons about 60 px apart in a 2048 px wide GUI, the boxes overlapping and the names cut). The weapon
// info update (0xF0BF60 in build 25216728, 0xF17A50 in Game Pass 1.0.56.0; the one function that loads
// "_quickuse" and calls the HUD helper) asks the HUD helper (0xF0BB50, 0xF17640) for each, which asks
// GetKeysForAction for the bind and LocalizedKeyName (the call at 0xF0BDA1, 0xF17891) for its text, and sizes
// the box to that text. The prompt hooks answer those calls with the short texts (input::hudKeyForm).

#include "features/input/prompt_keys.hpp"
#include "vkcore/game_text.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace evr::vkcore::controllers {

// The helper's LocalizedKeyName call and the weapon info update's calls to the helper, logged under `tag`;
// all 0 when any part does not check out (the row then keeps the full texts).
input::HudKeyCallers findWeaponInfoKeyCalls(const GameImage& image,
                                            const std::byte* helper,
                                            const std::byte* keyText,
                                            const char* tag);

// From the LocalizedKeyName detour, for the VR key in `slot` (< labels.distinct.size()), asked for from
// `returnAddress`: the text in the form input::hudKeyForm gives that call, the return address a frame above
// read from this thread's stack. Logs the first short text given.
const std::string& hudKeyAnswer(std::size_t slot,
                                std::uintptr_t returnAddress,
                                const input::HudKeyCallers& callers,
                                const input::PromptLabelSet& labels,
                                const char* tag);

} // namespace evr::vkcore::controllers
