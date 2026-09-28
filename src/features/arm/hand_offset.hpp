#pragma once

// The off hand's wrist from the controller (docs/VR_HANDS_HUD.md, "Off hand"): the arms model's
// `LeftHand` joint placed at the off-hand controller's OpenXR grip pose, with a fixed offset.
//
// Frames. The grip arrives as a pose in id Tech axes (rows forward, left, up = the grip's -Z, -X and +Y
// in OpenXR): forward along the tube of the curled fingers, up out of the thumb side, and, for the left
// hand, left out of the back of the hand (the grip's +X is the palm's normal, away from the palm). The
// `LeftHand` joint's rows (arms.md6skl, [static] from its bind pose): x toward the thumb side, y from the
// wrist toward the fingers (the forearm's roll joints and the elbow lie along -y), z out of the back of
// the hand (the fingers and the tucked thumb sit on -z, the palm side). So with no offset
//     hand.x = grip.up    hand.y = grip.forward    hand.z = grip.left
// a proper rotation (the thumb up, the fingers along the grip's forward, the palm facing the grip's +X).
//
// The offset (ETERNALVR_OFFHAND_OFFSET) is applied in the grip's own frame first, as for the weapon: a
// translation along its forward, left and up, then a rotation (idAngles, degrees) about its own axes.
// The default puts the wrist 8 cm behind the grip's centre and 3.5 cm toward the back of the hand; the
// OpenXR grip's position is the centre of the fist [inferred from the OpenXR grip definition, to be
// tuned in the headset].

#include "features/arm/arm_frames.hpp"
#include "xr_math/head_aim.hpp"

namespace evr::arm {

struct HandOffset {
    Vec3 translation;           // along the grip's forward, left, up (game units)
    xr_math::IdAngles rotation; // about the grip's own axes, degrees
};

// The default offset in metres and degrees (ETERNALVR_OFFHAND_OFFSET's default).
inline constexpr Vec3 kDefaultWristOffsetMetres{-0.08f, 0.035f, 0.0f};

// The `LeftHand` joint's axis for a grip axis (the fixed mapping above), and back.
IdViewAxis wristAxisFromGripAxis(const IdViewAxis& grip);
IdViewAxis gripAxisFromWristAxis(const IdViewAxis& wrist);

// The wrist's pose for the grip's pose (both in the same space), and back:
// gripFromWrist(wristFromGrip(g, o), o) == g.
ModelPose wristFromGrip(const ModelPose& grip, const HandOffset& offset);
ModelPose gripFromWrist(const ModelPose& wrist, const HandOffset& offset);

} // namespace evr::arm
