#pragma once

// The wrist's twist and swing for the arm solve (arm_solve.hpp, docs/VR_HANDS_HUD.md, "The whole arm"),
// measured from the arms skeleton's bind pose.
//
// The bind pose [static: arms.md6skl and marine.md6skl, build 25216728; tests/features/arm/arm_fixture.hpp]:
// the wrist (LeftHand), the four roll joints and the elbow (LeftForeArm) share one axis, whose y runs along
// the forearm (from the elbow to the wrist on the left arm, the other way on the right) and whose z is the
// elbow's bend normal turned around (-cross(upper arm, forearm)); the shoulder joint (LeftArm) has the same z
// and its y along the upper arm. The fingers run along the hand's y and the palm faces its z (the thumb
// side is x), so a hand turned about x flexes toward or away from the palm and one turned about z deviates
// toward the thumb or the little finger. A bone's "neutral" frame is that relation rebuilt on a solved
// bone: the twist and swing below are the wrist's turn away from its forearm's neutral frame.
//
//   twist     the wrist's turn about the forearm, swing-twist split, kept continuous from tick to tick
//             (unwrapped): a twist past half a turn keeps its way round instead of jumping to the other
//             side, until it is more than kHysteresis past half a turn from the centre
//   split     the forearm's roll joints take the twist up to kForearmTwist either way from the centre; the
//             rest, up to kUpperTwist, goes to the upper arm (the elbow joint and, as far as its axis lines
//             up with the forearm, the shoulder joint turn with it); beyond both the wrist takes the rest
//   swing     the wrist's bend away from its forearm, limited to an ellipse of kFlex (flexion and
//             extension) by kDeviation (radial and ulnar deviation), the twist kept
//
// Pure: no game memory, tested on its own.

#include "features/arm/arm_frames.hpp"

#include <numbers>
#include <optional>

namespace evr::arm {

inline constexpr float kRadiansPerDegree = std::numbers::pi_v<float> / 180.0f;

struct TwistLimits {
    // The middle of the forearm's range: the twist of a hand that holds a controller level (roll 0) in
    // the rig's neutral pose, -32 degrees on both arms with the default wrist offset [rig: hands rig
    // sweep hr-base3, the solved hand against its forearm's neutral frame], about a real forearm's
    // mid-turn (thumb up).
    float centre = -30.0f * kRadiansPerDegree;
    // A real forearm turns about 80 to 90 degrees either way from thumb up (pronation, supination).
    float forearm = 90.0f * kRadiansPerDegree;
    // A real shoulder adds about as much (internal and external rotation of the upper arm).
    float upper = 90.0f * kRadiansPerDegree;
    // How far past half a turn from the centre the twist keeps its way round before it flips.
    float hysteresis = 30.0f * kRadiansPerDegree;
    // A real wrist flexes about 70 to 80 degrees and extends about 60 to 70; it deviates about 20 degrees
    // toward the thumb and 30 to 35 toward the little finger (one limit both ways here). Skinned as the
    // game skins it, the base arms mesh keeps 0.71 of its wrist ring at 90 degrees of swing and 0.50 at
    // 120 [static: base arms.md6mesh, linear blend skinning], so 70 keeps clear of a collapse.
    float flex = 70.0f * kRadiansPerDegree;
    float deviation = 35.0f * kRadiansPerDegree;
};

inline constexpr TwistLimits kTwistLimits{};

// A bone's neutral frame: y along `bone` times `side` (+1 on the left arm, -1 on the right), z the bend
// `normal` made perpendicular to it and turned around, x completing a right-handed frame. False for a zero
// bone or a normal along it.
bool neutralFrame(Vec3 bone, Vec3 normal, float side, IdViewAxis& out);

// `raw` (radians, in (-pi, pi]) on the branch nearest `last` (the twist of the last tick), flipped by a
// whole turn when that is more than half a turn plus the hysteresis from the centre. Without `last`, the
// branch within half a turn of the centre.
float unwrapTwist(float raw, std::optional<float> last, const TwistLimits& limits = kTwistLimits);

struct TwistSplit {
    float forearm = 0.0f; // from the elbow joint to the wrist, shared by the roll joints
    float upper = 0.0f;   // the elbow joint's own turn, carried up the arm
    float wrist = 0.0f;   // what neither takes: between the last roll joint and the wrist
};

// How a wrist twist (radians, from the bind pose) is shared out.
TwistSplit splitTwist(float twist, const TwistLimits& limits = kTwistLimits);

struct SwingResult {
    IdViewAxis hand;        // the hand's axis with its swing limited, its twist kept
    float flex = 0.0f;      // the swing asked for (radians): about the neutral frame's x
    float deviation = 0.0f; // and about its z
    bool clamped = false;
};

// `hand`'s swing away from `neutral` (the forearm's neutral frame) limited to the ellipse of the limits.
SwingResult
limitSwing(const IdViewAxis& neutral, const IdViewAxis& hand, const TwistLimits& limits = kTwistLimits);

} // namespace evr::arm
