#pragma once

// The free off hand's fingers from its controller (features/arm/hand_pose.hpp; docs/VR_HANDS_HUD.md, "Hand
// poses"): with the bind solve the off hand's arm has room for its fifteen finger bones too
// (offhand_mods::fingerCount), and while the layer poses that arm they are written each tick, in their
// parents' frames, between the open hand and the game's own grip of a handle by the controller's grip,
// trigger and thumb sensors. The bones keep the model's own lengths, read from the game's hand while none of
// the layer's overrides show. Until such a read, without a controller frame, or while the game has the arm,
// the fingers are the game's; they leave with the arm's weight, and ease back to the game's hand (as last
// read clean) once the controller is lost or off. Hook thread (the game thread running idHands::Update) only.

#include "features/arm/grip_hand.hpp"
#include "features/input/controller_settings.hpp"
#include "xr_math/offhand_pose.hpp"

#include <array>
#include <cstddef>

namespace evr::vkcore::controllers::offhand_fingers {

using FingerWrites = std::array<xr_math::ModelPose, arm::kFingerModCount>;

// The off hand's finger overrides for this tick (each in its parent's frame, game units), or nullptr when the
// fingers stay the game's. `clean`: this tick's read of the arm had none of the layer's overrides in it;
// `armWeight`: the arm's share of the layer's pose this tick (0 to 1), the fingers' share too.
const FingerWrites*
pose(const std::byte* hands, bool clean, float armWeight, const input::ControllerSettings& cfg);

// The arm given back to the game: the curls start again from the sensors when the layer next poses it.
void giveBack();

// Logs the curls of the last pose and how the middle finger's second bone landed (ETERNALVR_OFFHAND_TRACE).
void trace(const std::byte* hands, const char* tag);

} // namespace evr::vkcore::controllers::offhand_fingers
