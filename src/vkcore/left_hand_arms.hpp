#pragma once

// The arms with the weapon in the left hand (input::leftHandArms; docs/VR_HANDS_HUD.md, "The weapon in the
// left hand").
//
// The arms model is not mirrored, so the gun stays on the model's right arm's attach joint
// (`righthandattach`, the weapon's root). The left attach joint is posed with it in the idles and the shots
// the hands rig dumped (features/arm/left_hand.hpp), and the left glove, its child, can be held in its frame
// as the right glove is on a grip (parent_frames.hpp): it rides the gun's pose of the frame the game draws.
// Where the game parts them (the chaingun's mod held: 13 to 25 cm) the grip is placed on the gun's root as
// read each tick, so the glove stays on the gun, a frame behind only as they part. So the arms swap roles in
// the gun's frame, each reflected across the gun's middle:
//
// - The left arm holds the gun: its glove (wrist and fingers) in the right glove's grip of the game's
//   animation reflected (the held item's [right_grip] row's grip on the chaingun and the ballista), its
//   forearm and upper arm by IK to a shoulder beside the head on the left, the weapon arm's own code
//   (weapon_arm.hpp, holding()). The game's modifier of the left attach joint (the weapon lag) stays the
//   game's.
// - The right arm is the off hand. With ETERNALVR_OFFHAND=game (no hand aim) its glove holds the gun as the
//   game's left glove does, reflected, held in the gun's frame; with `free` it follows the right controller,
//   held in model space (the gun's kick does not move it), its fingers as the game's right hand has them. Its
//   forearm reaches a shoulder on the right.
// - The model is placed so the left glove's wrist, not the right one's, sits in the left hand
//   (viewmodel_hook.cpp, arm::leftGloveShift): placed by the right glove's row (its turn reflected), then
//   moved, not turned, until the left glove's wrist sits on the left hand's wrist point; the move stays while
//   the game has the arms (a switch, a glory kill), so the gun stays on the left hand. The wrist is the one
//   the game's grip gives, kept for the held item if that grip lapses (arm::LeftWristOnGun); before any is
//   read, the item's [left_hand_wrist] row's (metres, at the world's scale).
//
// The grips are the game's own, read from each arm while the layer's overrides are off it (a clean read,
// game_arm.hpp) once the read has held still for a few ticks (arm::SteadyPose: not while the gun comes up),
// kept per held item: until both arms have one for the held item (the right's only with the free off hand)
// the arms go back to the game, which the first switch to a weapon does anyway. The arm policy
// (offhand_policy.hpp) decides for both as for the weapon arm, and the free off hand as for the off hand.
//
// Off-hand hook thread (the game thread running idHands::Update) only, but placement(), which any thread
// may read.

#include "features/input/offhand_policy.hpp"
#include "vkcore/controllers_impl.hpp"

#include <cstddef>

namespace evr::vkcore::controllers::left_hand_arms {

// Every tick of the player's hands while both arms are the layer's (instead of the weapon arm and the off
// hand): `arms` the tick's arms' item, `weaponSignals` with offHandTracked for the head and the weapon hand,
// `offSignals` for the off hand.
void tick(const std::byte* hands,
          const ArmsTick& arms,
          const input::ArmSignals& weaponSignals,
          const input::ArmSignals& offSignals,
          const WorldHand& world,
          const ModelPlacement& model,
          float dt);

// Both arms given back.
void release(const std::byte* hands);

// For the viewmodel hook: the left glove's wrist on the gun (model space, game units, the gun's root as the
// grip was read) for the held item `decl`, and how far the model is moved for it: 1 once that wrist is known,
// 0 before; it does not follow the arms' hand-backs, so the gun stays on the left hand while the game has the
// arms.
struct Placement {
    Vec3 leftWrist;
    float weight = 0.0f;
    const std::byte* decl = nullptr;
};
Placement placement();

} // namespace evr::vkcore::controllers::left_hand_arms
