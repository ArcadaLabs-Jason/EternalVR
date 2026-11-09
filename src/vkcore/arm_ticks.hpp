#pragma once

// The hands rig's per-tick record (ETERNALVR_TEST_ARM_TICKS=<file>, docs/rig-findings/hands-rig.md): rig use
// only. One CSV row per event, in the order the events happened, on the arm dump's clock:
//
//   W  the camera hook wrote the hands (endGameView): t, thread, the snapshot's stamp, the game frame (and
//      -1), the eye, the weapon hand's grip and the off hand's grip (offsets from the eye, game units) and
//      whether the off hand is valid
//   V  the viewmodel hook placed the arms model: t, thread, the stamp of the snapshot it placed from, the
//      game frame and the frame the hands were last placed in (idHands::UpdatePosition's own once-a-frame
//      check, -1 when unread), the eye, the placement (offset from the eye and the axis rows forward, left,
//      up)
//   L  the off-hand hook ran for the player's hands: t, thread, the snapshot's stamp, the two frames as in V,
//      the stamps of the placement it read and of the snapshot that placement came from, the eye, the
//      placement read, the off hand's grip read, whether it was valid, the arm's weight and reason, whether
//      the layer posed it, and then (zeros when not posed) the wrist (the LeftHand joint, model space, game
//      units): its target, as written, as read this tick, as animated, and how far the last read landed from
//      the last write; last the arms model's placement as the render model holds it at that moment (world
//      origin, axis rows)
//
// What lands where in the world per tick is worked out from these by the rig's script. Nothing is collected
// or written without the variable. Any thread (the camera hook runs on several): rows are written under one
// lock; the off hand's arm is noted and written on the off-hand hook's thread.

#include "features/input/offhand_policy.hpp"
#include "vkcore/controllers_impl.hpp"
#include "xr_math/offhand_pose.hpp"

#include <cstddef>

namespace evr::vkcore::controllers::arm_ticks {

// The variable names a file.
bool on();

// Camera hook, after the hands were written: a W row.
void noteWorld(const WorldHand& world, Vec3 eye);

// Viewmodel hook, after the placement was written: a V row.
void notePlacement(const std::byte* hands,
                   const WorldHand& world,
                   Vec3 eye,
                   const xr_math::EyeRelativePose& placed);

// Off-hand hook, when the layer posed the free off hand this tick: the wrist's poses for the tick's L row.
struct LeftWrist {
    xr_math::ModelPose target;
    Vec3 written;
    Vec3 read;
    Vec3 animated;
    float landed = -1.0f;
};
void noteLeftWrist(const LeftWrist& wrist);

// Off-hand hook, once per tick of the player's hands, after the arm was posed or given back: the L row.
void leftTick(const std::byte* hands, const TickView& view, Vec3 eye, float weight, input::ArmReason reason);

} // namespace evr::vkcore::controllers::arm_ticks
