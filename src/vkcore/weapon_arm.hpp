#pragma once

// The weapon arm (ETERNALVR_WEAPON_ARM, docs/VR_HANDS_HUD.md, "The weapon arm").
//
// The viewmodel hook places the whole arms model so the gun sits at the weapon-hand controller, which
// leaves the arm holding it (the model's right arm, whichever hand holds the weapon: the model is not
// mirrored) in its flat-screen pose relative to the gun: the forearm and upper arm point back toward where
// the flat camera would be, below and behind the hand, out of view in a normal VR grip. With `ik` (the
// default) the layer bends that arm the way it bends the free off hand's (game_arm.hpp): the wrist stays
// exactly where the game animated it (the gun does not move; the attach joint and its modifier stay the
// game's), and the forearm's roll joints, the elbow and the upper arm reach it by two-bone IK from a
// shoulder fixed to the tracked head (the off hand's shoulder point and elbow direction, mirrored to the
// weapon hand's side), bending toward the elbow's pole (features/arm/arm_solve.hpp, solveArmToWrist). A
// wrist further from that shoulder than the arm reaches pulls the shoulder along instead.
//
// The arm policy (features/input/offhand_policy.hpp) hands the arm back to the game, blended over
// ETERNALVR_OFFHAND_BLEND, for glory and sync kills, melee, throws, weapon switches, custom animations,
// hidden hands and forced views, and whenever the head and weapon hand are not tracked or the viewmodel
// hook did not place the arms at the hand this frame. Without the viewmodel hook (ETERNALVR_VIEWMODEL=0)
// nothing is installed and the arm is the game's.
//
// The weapon's mesh kit hides this arm for most weapons ("ArmLeft"), so while the layer poses it the arm's
// surface is shown, and hidden again when the arm goes back to the game (hands_surfaces.hpp).
//
// Fail closed: the right attach joint is the one InitJointMods stores at idHands+0x28E6 for
// "righthandattach" (checked at install, offhand_mods.hpp); the arm's joints are found by shape from it and
// their names checked with the game's name handles, every tick the same checks as the off hand's arm
// (game_arm.hpp), and a result that is not plausible leaves the arm the game's for that tick. Off-hand
// hook thread (the game thread running idHands::Update) only.

#include "features/input/offhand_policy.hpp"
#include "vkcore/controllers_impl.hpp"

#include <cstddef>

namespace evr::vkcore::controllers::weapon_arm {

// Install time, after the arm's checks passed: logs the settings.
void logSettings();

// Every tick, from the off-hand hook: the arm solved and written, or set back to the game's. `signals`
// are the hands' and player's (offHandTracked is set here); `dt` the seconds since the last tick.
void tick(const std::byte* hands,
          input::ArmSignals signals,
          const WorldHand& world,
          const ModelPlacement& model,
          float dt);

// Sets the arm's modifiers back to no change and hides again the arm's surface if the layer showed it.
void release(const std::byte* hands);

// After a multiplayer guard trip (the guard already refuses every other write), once: the same give-back,
// on the hands the arm was posed on (another hands' tick passes). Nothing when the arm was never posed.
void releaseOnTrip(const std::byte* hands);

} // namespace evr::vkcore::controllers::weapon_arm
