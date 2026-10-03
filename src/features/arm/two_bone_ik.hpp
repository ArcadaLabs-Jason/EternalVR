#pragma once

// Two-bone inverse kinematics for the off-hand arm (docs/VR_HANDS_HUD.md, "Off hand"): where the elbow
// goes for a shoulder, a wrist target and the two bone lengths.
//
// The elbow lies in the plane through the shoulder, the wrist and the pole direction, on the pole's side
// of the shoulder-wrist line (the arm bends toward the pole). A target beyond reach is pulled back along
// the shoulder-target line to just short of the full length (`kMaxReachFraction`), and one too close for
// the bones to fold to is pushed out to the shortest reach they allow: the hand stops short instead of the
// skin stretching. Pure; positions in any consistent units.

#include "common/vector.hpp"

#include <optional>

namespace evr::arm {

// The wrist never gets further than this fraction of the arm's full length from the shoulder, so the
// elbow keeps a small bend and its plane stays defined.
inline constexpr float kMaxReachFraction = 0.999f;
// Nor closer than this fraction of the full length (or the difference of the lengths, if longer).
inline constexpr float kMinReachFraction = 0.05f;

struct TwoBoneInput {
    Vec3 root;          // the shoulder
    Vec3 target;        // where the wrist should be
    float upper = 0.0f; // shoulder to elbow
    float lower = 0.0f; // elbow to wrist
    Vec3 pole;          // the direction the elbow bends toward (need not be unit or perpendicular)
};

struct TwoBoneSolution {
    Vec3 joint; // the elbow
    Vec3 end;   // the wrist: the target, or where it was clamped to
    Vec3 bend;  // the unit direction from the shoulder-wrist line toward the elbow
    bool clamped = false;
    float reach = 0.0f; // the target's distance from the shoulder over the full length (before clamping)
};

// nullopt when a length is not positive and finite, or an input is not finite.
std::optional<TwoBoneSolution> solveTwoBone(const TwoBoneInput& in);

// For an end that must not move (the weapon arm's wrist under the gun): `root` moved along the line to
// `target` just inside the reach solveTwoBone meets without clamping. Unchanged when the target is within
// it already, when root and target coincide, or when the lengths are unusable.
Vec3 rootWithinReach(Vec3 root, Vec3 target, float upper, float lower);

} // namespace evr::arm
