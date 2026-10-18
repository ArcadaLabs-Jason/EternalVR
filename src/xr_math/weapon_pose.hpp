#pragma once

// The weapon hand in the game world: where the viewmodel is drawn (T-054) and where shots start and go
// (T-055).
//
// Both are kept relative to the game's eye (the view origin the game renders from, before the layer adds
// the head's tracked offset), because the game moves the player between the frame the controller was
// located for and the moment the weapon is placed or fires: re-adding the offset to the eye the game has
// then keeps the gun on the hand while the player moves, dashes or rides a jump pad.
//
// Conventions as in head_view.hpp and hand_aim.hpp: id Tech world axes (+X forward, +Y left, +Z up), an
// axis given as its forward, left and up rows, angles in degrees with pitch positive down.

#include "common/pose.hpp"
#include "common/vector.hpp"
#include "xr_math/head_aim.hpp"
#include "xr_math/head_view.hpp"

#include <optional>

namespace evr::xr_math {

// A pose in the world relative to the game's eye: `offset` from the eye, and an absolute axis.
struct EyeRelativePose {
    Vec3 offset;
    IdViewAxis axis;
};

// A tracked controller pose (OpenXR, tracking space) relative to the game's eye. `body` is the body frame
// (the game's heading without the injected aim), `headOffsetWorld` the rendered head's offset from the eye
// (headOffsetInWorld, or zero when head position is off) and `head` the head's tracking pose.
EyeRelativePose controllerRelativeToEye(const IdViewAxis& body,
                                        Vec3 headOffsetWorld,
                                        const Pose& head,
                                        const Pose& controller,
                                        float unitsPerMetre);

// The axis whose forward is `direction`, with no roll: left stays horizontal. Straight up or down, left is
// the world's +Y. nullopt for a zero or non-finite direction.
std::optional<IdViewAxis> axisFromDirection(Vec3 direction);

// `pose` moved by an offset given in its own frame: `translation` along its forward, left and up axes
// (metres, scaled to game units), and `rotation` applied in its own frame (idAngles in degrees).
EyeRelativePose applyLocalOffset(const EyeRelativePose& pose,
                                 Vec3 translation,
                                 const IdAngles& rotation,
                                 float unitsPerMetre);

// As applyLocalOffset, then the model turned about the grip so that its sight line runs along the offset's
// forward: parallel to the aim ray when `rotation` is zero, else off it by that rotation on purpose.
// `sight` is the sight line's direction in the model's own frame (pitch positive down, yaw counter-clockwise,
// degrees; roll is ignored). The model's point that applyLocalOffset puts at `pose` (the grip) stays there,
// so the hand stays on the controller; the turn is the one taking the sight line onto the model's forward.
EyeRelativePose applyLocalOffsetWithSight(const EyeRelativePose& pose,
                                          Vec3 translation,
                                          const IdAngles& rotation,
                                          const IdAngles& sight,
                                          float unitsPerMetre);

// The viewmodel's pose (T-054). `hand` is the controller's grip position with the aim ray's axis, `gripAxis`
// the grip's own orientation. The model is placed by `translation` and `rotation` in the aim's frame
// (applyLocalOffset, or applyLocalOffsetWithSight with a `sight`), then moved, not turned, so that the point
// the offset puts at `wrist` in the aim's frame lands at `wrist` in the grip's frame: the glove's wrist
// follows the real wrist, which is fixed to the grip, while the model keeps the aim ray's orientation (the
// sights and the shots do not change). With the grip's axis on the aim's (one pose) nothing moves.
EyeRelativePose placeViewmodel(const EyeRelativePose& hand,
                               const IdViewAxis& gripAxis,
                               Vec3 translation,
                               const IdAngles& rotation,
                               const std::optional<IdAngles>& sight,
                               Vec3 wrist,
                               float unitsPerMetre);

// A shot's start and axis (idFireParms start and fireAxis; forward is the direction of fire).
struct Shot {
    Vec3 origin;
    IdViewAxis axis;
};

// The shot along the hand ray: from `eye + rayOffset`, where the offset is first limited to `maxReach`
// (a tracking glitch never throws the start further than an arm reaches), then `muzzleDistance` further
// along `direction`. nullopt when the direction or the inputs are unusable.
std::optional<Shot>
shotFromHand(Vec3 eye, Vec3 rayOffset, Vec3 direction, float maxReach, float muzzleDistance);

// The world pose for the game's current eye: eye + offset, and the axis.
Vec3 atEye(Vec3 eye, const EyeRelativePose& pose);

} // namespace evr::xr_math
