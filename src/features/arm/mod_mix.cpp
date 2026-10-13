// The hands' joint-modifier mix and its inverse (mod_mix.hpp).

#include "features/arm/mod_mix.hpp"

#include "features/arm/arm_frames.hpp"
#include "features/arm/arm_solve.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace evr::arm {

namespace {

constexpr float kMinWeight = 0.5f;
// The furthest a joint is written from its animated pose, turned: the mix goes the shortest way round, so a
// turn written past half a turn would land the other way. Mixed at 0.95 this one lands 173 degrees round.
constexpr float kMaxWrittenTurn = 179.0f * 3.14159265f / 180.0f;

// q's rotation scaled by `t` along the shortest arc.
Quat power(Quat q, float t) {
    if (q.w < 0.0f) {
        q = {-q.x, -q.y, -q.z, -q.w};
    }
    const Vec3 v{q.x, q.y, q.z};
    const float s = length(v);
    if (s < 1e-7f) {
        return Quat::identity();
    }
    return Quat::fromAxisAngle(v, 2.0f * std::atan2(s, q.w) * t);
}

// The game's blend of q into the identity at `t`, the shortest way round: (1 - t) * identity + t * q,
// normalised (nlerp).
Quat nlerpTurn(Quat q, float t) {
    if (q.w < 0.0f) {
        q = {-q.x, -q.y, -q.z, -q.w};
    }
    return normalize(Quat{q.x * t, q.y * t, q.z * t, 1.0f - t + q.w * t});
}

// The turn (radians, 0 to pi) that the blend at `weight` takes `landed` round: the inverse of nlerpTurn's
// angle. With h half the turn written and g half the turn landed, tan g = w sin h / (1 - w + w cos h), so
// h = g + asin((1 - w) / w * sin g).
float turnWrittenFor(float landed, float weight) {
    const float g = 0.5f * landed;
    return 2.0f * (g + std::asin(std::clamp((1.0f - weight) / weight * std::sin(g), -1.0f, 1.0f)));
}

// What to write for `a` so the blend at `weight` takes it to `b`: positions past by 1 / weight, the turn by
// turnWrittenFor, kept short of half a turn (kMaxWrittenTurn); `cut`, when given, says whether it was.
xr_math::ModelPose
writtenPast(const xr_math::ModelPose& a, const xr_math::ModelPose& b, float weight, bool* cut = nullptr) {
    const Quat qa = toQuat(a.axis);
    Quat rel = conjugate(qa) * toQuat(b.axis);
    if (rel.w < 0.0f) {
        rel = {-rel.x, -rel.y, -rel.z, -rel.w};
    }
    const float s = length(Vec3{rel.x, rel.y, rel.z});
    const float turn = 2.0f * std::atan2(s, rel.w);
    const float written = turnWrittenFor(turn, weight);
    const bool limited = written > kMaxWrittenTurn && turn > 0.0f;
    if (cut) {
        *cut = limited;
    }
    const float t = turn <= 0.0f ? 1.0f : (limited ? kMaxWrittenTurn : written) / turn;
    return {a.position + (b.position - a.position) * (1.0f / weight),
            fromQuat(normalize(qa * power(rel, t)))};
}

// The joints after the wrist written past their targets (`out` holds the wrist's modified pose), each placed
// from where the one before lands (`landed`, the wrist's to start with).
void forearmPast(const ArmPoses& animated,
                 const ArmPoses& wanted,
                 float weight,
                 xr_math::ModelPose landed,
                 ArmPoses& out) {
    for (std::size_t j = index(ArmJoint::Hand) + 1; j < kArmJointCount; ++j) {
        const xr_math::ModelPose from = relative(animated[j - 1], animated[j]);
        const xr_math::ModelPose local = writtenPast(from, relative(landed, wanted[j]), weight);
        out[j] = compose(out[j - 1], local);
        landed = compose(landed, mixPose(from, local, weight));
    }
}

} // namespace

bool usableModWeight(float weight) {
    return std::isfinite(weight) && weight > kMinWeight && weight <= 1.0f;
}

xr_math::ModelPose mixPose(const xr_math::ModelPose& a, const xr_math::ModelPose& b, float t) {
    const Quat qa = toQuat(a.axis);
    const Quat turn = nlerpTurn(conjugate(qa) * toQuat(b.axis), t);
    return {a.position + (b.position - a.position) * t, fromQuat(normalize(qa * turn))};
}

xr_math::ModelPose pastPose(const xr_math::ModelPose& a, const xr_math::ModelPose& b, float weight) {
    return writtenPast(a, b, weight);
}

ArmPoses mixedArm(const ArmPoses& animated, const ArmPoses& modified, float weight) {
    ArmPoses out = animated;
    out[0] = mixPose(animated[0], modified[0], weight);
    for (std::size_t j = 1; j < kArmJointCount; ++j) {
        const xr_math::ModelPose local =
            mixPose(relative(animated[j - 1], animated[j]), relative(modified[j - 1], modified[j]), weight);
        out[j] = compose(out[j - 1], local);
    }
    return out;
}

ArmPoses modifiedFor(const ArmPoses& animated,
                     const ArmPoses& wanted,
                     float weight,
                     const xr_math::ModelPose* modifiedHand) {
    const std::size_t hand = index(ArmJoint::Hand);
    ArmPoses out = animated;
    // Where each joint will land: on its target, but for a turn the mix cannot make (writtenPast), when the
    // joints after it are placed from where it lands instead.
    xr_math::ModelPose landed = wanted[hand];
    if (modifiedHand) {
        out[hand] = *modifiedHand;
    } else {
        out[0] = writtenPast(animated[0], wanted[0], weight);
        out[hand] = compose(out[0], relative(animated[0], animated[hand]));
        landed = compose(mixPose(animated[0], out[0], weight), relative(animated[0], animated[hand]));
    }
    forearmPast(animated, wanted, weight, landed, out);
    return out;
}

WristArm modifiedWithWrist(const ArmPoses& animated, const ArmPoses& wanted, float weight) {
    const std::size_t hand = index(ArmJoint::Hand);
    const xr_math::ModelPose fromAttach = relative(animated[0], animated[hand]);
    WristArm w;
    w.modified = animated;
    w.modified[0] = writtenPast(animated[0], wanted[0], weight, &w.wristTurned);
    const xr_math::ModelPose landedAttach = mixPose(animated[0], w.modified[0], weight);
    const xr_math::ModelPose local =
        w.wristTurned ? writtenPast(fromAttach, relative(landedAttach, wanted[hand]), weight) : fromAttach;
    w.modified[hand] = compose(w.modified[0], local);
    const xr_math::ModelPose landed = w.wristTurned ? mixPose(fromAttach, local, weight) : fromAttach;
    forearmPast(animated, wanted, weight, compose(landedAttach, landed), w.modified);
    return w;
}

xr_math::JointMod wristChange(const ArmPoses& animated, const WristArm& written) {
    const std::size_t hand = index(ArmJoint::Hand);
    if (!written.wristTurned) {
        return {};
    }
    return xr_math::jointModToward(compose(written.modified[0], relative(animated[0], animated[hand])),
                                   written.modified[hand]);
}

xr_math::ModelPose
preModifierPose(const xr_math::ModelPose& read, const xr_math::JointMod& mod, float weight) {
    // The change turns the joint on the left (in model space) and moves it: mixed at `weight`, the read is
    // the pose without it turned by the change's rotation scaled by the weight and moved by the weight's
    // share.
    const xr_math::ModelPose unit{}; // at the origin, axes the model's
    const Quat turn = nlerpTurn(toQuat(xr_math::applyJointMod(unit, mod).axis), weight);
    return {read.position - mod.translation * weight,
            fromQuat(normalize(conjugate(turn) * toQuat(read.axis)))};
}

ArmPoses preModifierArm(const ArmPoses& read, const xr_math::JointMod& mod, float weight) {
    return followAttach(read, preModifierPose(read[0], mod, weight));
}

std::optional<xr_math::JointMod> handedTo(const std::optional<HandedMod>& last, const void* hands) {
    if (!last || !hands || last->hands != hands) {
        return std::nullopt;
    }
    return last->mod;
}

ArmPoses gameArmMixed(const ArmPoses& pre, const xr_math::JointMod& mod, float weight) {
    return followAttach(pre, mixPose(pre[0], xr_math::applyJointMod(pre[0], mod), weight));
}

} // namespace evr::arm
