#pragma once

// The weapon hand's index finger on the trigger (features/arm/trigger_finger.hpp; docs/VR_HANDS_HUD.md, "The
// index finger on the trigger"), for the weapon arm (weapon_arm.cpp): while the layer poses the arm that
// holds the gun, the share its index finger opens from the game's grip by the weapon-hand controller's
// trigger and the held gun's [trigger_finger] row, on the New arms path (the free off hand and the bind
// solve, arm::followsTrigger). An item without a row, another path, or no controller frame leaves the finger
// the game's. ETERNALVR_TEST_ARM_FAULT=trigger (the rig's planted fault) turns it past the game's grip
// instead, into the trigger guard. Off-hand hook thread (the game thread running idHands::Update) only.

#include "features/arm/arm_joints.hpp"
#include "features/arm/hand_pose.hpp"
#include "features/input/controller_settings.hpp"

#include <cstddef>
#include <optional>

namespace evr::vkcore::controllers::weapon_finger {

// The share the planted fault writes: the finger's turn 1.6 times the grip's, its tip about 2.5 cm toward the
// palm on the Heavy Cannon's grip.
inline constexpr float kFaultOpen = -0.6f;

// This tick's share for the arm on `side` holding the arms' item `decl` (eased over `dt` seconds), or nullopt
// for the game's finger.
std::optional<float>
update(arm::ArmSide side, const std::byte* decl, float dt, const input::ControllerSettings& cfg);

// The arm given back: the share starts again from the sensors when the layer next poses it.
void release(arm::ArmSide side);

// The rig dump's record (arm_dump.hpp) of the finger written this tick: `open` the share written (the arm's
// weight in it), `game` the grip it opened from and `written` the hand as written (each joint in its parent's
// frame), with the hand as read this tick.
void noteDump(arm::ArmSide side,
              const std::byte* hands,
              float open,
              const arm::HandLocal& game,
              const arm::HandLocal& written);

// The same record when the arm that holds the gun is posed but its finger is the game's this tick: "posed" 0,
// the finger as read, and `lastGrip` (the game's grip as last read clean, for any item; nullptr for none) as
// "game".
void noteGame(arm::ArmSide side, const std::byte* hands, const arm::HandLocal* lastGrip);

} // namespace evr::vkcore::controllers::weapon_finger
