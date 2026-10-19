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

#include "features/arm/grip_hand.hpp"
#include "features/input/offhand_policy.hpp"
#include "vkcore/controllers_impl.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace evr::vkcore::controllers::weapon_arm {

// Install time, after the arm's checks passed: logs the settings.
void logSettings();

// Every tick, from the off-hand hook: the arm solved and written, or set back to the game's. `signals`
// are the hands' and player's, with offHandTracked for the head and the weapon hand (the shoulder's point
// and the gun: the view's poses current, tickView()); `dt` the seconds since the last tick.
void tick(const std::byte* hands,
          input::ArmSignals signals,
          const WorldHand& world,
          const ModelPlacement& model,
          float dt);

// Sets the arm's modifiers back to no change and hides again the arm's surface if the layer showed it.
void release(const std::byte* hands);

// After a multiplayer guard trip (the guard already refuses every other write), once: the same give-back,
// on the hands the arm was posed on (another hands' tick passes), for each arm this file posed. Nothing for
// an arm never posed.
void releaseOnTrip(const std::byte* hands);

// With the weapon in the left hand (left_hand_arms.hpp) this file poses both arms, each holding a glove:
// what the arm holds this tick, and where its shoulder and elbow go (world, relative to the eye, in the
// head's yaw frame; the elbow's bend direction).
struct Hold {
    input::ArmDecision decision;
    const arm::GripPose* grip = nullptr; // none: the glove eases off its last grip
    bool modelSpace = false;             // the glove follows a controller, written in model space
    Vec3 shoulder;
    Vec3 elbow;
    // With `grip`: its wrist placed here instead (model space) each tick, under the attach joint as read.
    std::optional<xr_math::ModelPose> wrist;
};
void holding(arm::ArmSide side,
             const std::byte* hands,
             const Hold& hold,
             const WorldHand& world,
             const ModelPlacement& model,
             float dt);

// The game's own grip of the held item, from the last read of the arm the layer's overrides had not reached
// (taken only with the weapon in the left hand, also on each tick it was given back): the held item's decl it
// was read with, the wrist in the attach joint's frame and the hand's joints in their parents', and the
// attach joint as read (model space, game units).
struct Capture {
    const std::byte* decl = nullptr;
    arm::GripPose grip;
    xr_math::ModelPose root;
    std::uint32_t serial = 0; // numbers the arm's captures, one per read
};
std::optional<Capture> captured(arm::ArmSide side);
// Drops the arm's last capture (new hands, or the arms given back for good).
void forgetCapture(arm::ArmSide side);

// The arm's attach joint as read this tick (model space, game units), or nullopt when the arm was not read.
std::optional<xr_math::ModelPose> attachRead(arm::ArmSide side);

// The held item's [right_grip] row's grip (wrist in the gun's root frame), or nullopt for a weapon without
// one.
std::optional<arm::GripPose> rowGrip(const std::byte* hands);

// The arm on `side` given back (release() above for that arm), its blend and its glove's grip started again
// from the game's.
void release(arm::ArmSide side, const std::byte* hands);

} // namespace evr::vkcore::controllers::weapon_arm
