#pragma once

// The hands animation's camera on the head-tracked view (docs/VR_HEAD_TRACKED.md, "Camera animations").
//
// Some first-person hands animations carry a `camera` joint (a melee punch on the rig; a pickup animation
// that moves the camera would use it too). With
// p_applyAnimatedCamera on, idPlayer::CalculateViewWithoutUpdates adds that joint's rotation to the
// first-person view as angles: view = ToMat3(ToAngles(view) + added), pitch clamped to +-89 degrees, and
// moves the eye by the joint's offset in the view frame. The eye offset reaches the head-tracked view with
// the game's origin; the rotation would be lost, because the head replaces the view's rotation.
//
// Here the added angles come off the game's view (the body and head aim see the view without them) and go
// on top of the head-tracked view the same way the game adds them, so the head stays tracked and the
// animation turns the view relative to it. Small camera motion is left out (on the rig: under a degree on
// weapon raises and landings, up to 4 degrees in a melee punch), with a smooth ramp to the full animation so
// nothing jumps.

#include "xr_math/head_aim.hpp"
#include "xr_math/head_view.hpp"

#include <optional>

namespace evr::xr_math {

struct CameraAnimRamp {
    float startDegrees = 5.0f; // below this the animation is left out
    float fullDegrees = 10.0f; // from this on it plays in full
};

// The largest of the three added angles, in degrees.
float cameraAnimSize(const IdAngles& added);

// 0 to 1: how much of the animation plays, a smoothstep of its size between the ramp's two ends.
float cameraAnimWeight(const IdAngles& added, const CameraAnimRamp& ramp = {});

// The angles scaled by `weight`.
IdAngles scaleAngles(const IdAngles& angles, float weight);

// The game's rule: `view` turned by `added` as angles, pitch clamped to +-pitchLimit.
IdViewAxis addCameraAnim(const IdViewAxis& view, const IdAngles& added, float pitchLimit = 89.0f);

// The orientation (id Tech axes) whose view axis is `axis` (rows forward, left, up; orthonormal).
Quat quatFromViewAxis(const IdViewAxis& axis);

// The head, in the body frame (id Tech axes), that composeHeadAxis turns into the head-tracked view with
// the animation added: composeHeadAxis(body, result) == addCameraAnim(composeHeadAxis(body, head), added).
// The stereo eyes are built from the body and this head, so they turn with the animation too.
Quat headWithCameraAnim(const IdViewAxis& body, Quat headInIdTech, const IdAngles& added);

// The game's view without the animation: the axis whose angles plus `added` give `gameAxis`. nullopt when
// no such axis gives `gameAxis` back within `toleranceDegrees` (the game's view does not hold this
// animation, or the pitch clamp took part of it).
std::optional<IdViewAxis>
removeCameraAnim(const IdViewAxis& gameAxis, const IdAngles& added, float toleranceDegrees = 0.5f);

} // namespace evr::xr_math
