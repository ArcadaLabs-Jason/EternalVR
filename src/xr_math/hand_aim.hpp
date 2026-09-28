#pragma once

// Hand aim (T-010, T-055): the weapon hand's aim ray in the game world and as game view angles.
//
// Poses come from OpenXR (tracking space: +X right, +Y up, -Z forward, metres) and are converted with
// the head-tracked layer's mapping id = (-z, -x, y) (head_view.hpp). The tracking space sits in the
// world through the body frame: the game's heading without the injected aim (HeadAimStep::bodyYaw),
// so world = body * tracking, the same composition the rendered head uses.
//
// Angles follow id Tech (head_aim.hpp): degrees, yaw counter-clockwise about +Z from +X, pitch positive
// looking down.
//
// Closed-loop aim (T-055): the user command written in frame N feeds the simulation of frame N+1, so
// open-loop deltas lag and drift. Each frame the layer reads the game's view angles back and adds the
// difference to the hand ray's angles, and sends nothing while the game forces the angles (sync and
// glory kills, meathook pull, cutscenes). The convergence fallback sets the view so the eye ray meets
// the point the hand ray hits, which needs no muzzle fields in the game.

#include "common/pose.hpp"
#include "common/quat.hpp"
#include "common/vector.hpp"
#include "xr_math/head_aim.hpp"
#include "xr_math/head_view.hpp"

#include <optional>

namespace evr::xr_math {

// A ray in id Tech world space; `direction` is unit length.
struct WorldRay {
    Vec3 origin;
    Vec3 direction{1.0f, 0.0f, 0.0f};
};

// A vector in tracking space (OpenXR axes, metres) as a world vector: converted to id Tech axes, scaled
// and rotated by the body frame.
Vec3 trackingToWorld(const IdViewAxis& body, Vec3 trackingVector, float unitsPerMetre = 1.0f);

// The aim pose's pointing ray (its -Z axis) in the world. The origin is placed relative to the rendered
// head: `headWorld` is where the rendered eye centre is in the world and `head` its tracking pose, so
// the ray starts where the player sees the controller.
WorldRay handRayInWorld(
    const IdViewAxis& body, Vec3 headWorld, const Pose& head, const Pose& aim, float unitsPerMetre = 1.0f);

// Yaw and pitch of a world direction; roll 0. Straight up or down gives yaw 0. nullopt for a zero or
// non-finite direction.
std::optional<IdAngles> anglesOfDirection(Vec3 direction);

// The aim pose's pointing direction as id Tech angles relative to the tracking space (the body frame
// adds its yaw). The hand's roll about its pointing ray does not change the result.
IdAngles handAimAngles(Quat aimOrientationOpenXr);

struct AimCorrection {
    float deltaPitch = 0.0f; // add to deltaViewAngles.pitch
    float deltaYaw = 0.0f;   // add to deltaViewAngles.yaw
    bool yielded = false;    // the game forces the angles; nothing is sent
};

// The closed-loop step: the delta that takes the game's view angles (read back this frame) to the target
// (the hand ray's world angles). Yaw takes the short way round; the pitch target is clamped to
// +-pitchLimit. Non-plausible input (head_aim.hpp) yields as well, so garbage read from game memory is
// never written back.
AimCorrection
closedLoopAim(const IdAngles& gameView, const IdAngles& target, bool forcedAngles, float pitchLimit = 89.0f);

// The angle in degrees between the forward directions of two angle sets: the aim error the M5 criterion
// measures at fire time (under 0.5 degrees p99).
float aimErrorDegrees(const IdAngles& a, const IdAngles& b);

// Convergence fallback: the view angles that point the eye at `target` (usually where the hand ray hits
// the world). nullopt when the two points coincide or are not finite.
std::optional<IdAngles> convergenceAngles(Vec3 eye, Vec3 target);

// The point `distance` along the ray: the convergence target when the hand ray hits nothing.
Vec3 pointAlong(const WorldRay& ray, float distance);

} // namespace evr::xr_math
