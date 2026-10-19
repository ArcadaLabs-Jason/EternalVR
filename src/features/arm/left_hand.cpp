// The arms with the weapon in the left hand (left_hand.hpp).

#include "features/arm/left_hand.hpp"

#include <cmath>
#include <cstddef>

namespace evr::arm {

namespace {

constexpr float kDegreesPerRadian = 180.0f / 3.14159265f;

// Reflected across the x-z plane.
Vec3 reflect(Vec3 v) {
    return {v.x, -v.y, v.z};
}

// A joint's axis reflected, each axis negated: a right-handed frame again.
IdViewAxis reflectJointAxis(const IdViewAxis& axis) {
    return {reflect(axis.forward) * -1.0f, reflect(axis.left) * -1.0f, reflect(axis.up) * -1.0f};
}

// A controller's frame reflected: forward and up reflected, left the other way, a right-handed frame again
// (the left controller held as the mirror image of the right one).
IdViewAxis reflectGripAxis(const IdViewAxis& axis) {
    return {reflect(axis.forward), reflect(axis.left) * -1.0f, reflect(axis.up)};
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

Vec3 inFrame(const IdViewAxis& axis, Vec3 v) {
    return axis.forward * v.x + axis.left * v.y + axis.up * v.z;
}

} // namespace

xr_math::ModelPose mirroredInRoot(const xr_math::ModelPose& local) {
    return {reflect(local.position), reflectJointAxis(local.axis)};
}

xr_math::ModelPose mirroredLocal(const xr_math::ModelPose& local) {
    return {local.position * -1.0f, local.axis};
}

GripPose mirroredGrip(const GripPose& grip) {
    GripPose out;
    out.wrist = mirroredInRoot(grip.wrist);
    for (std::size_t i = 0; i < kHandJointCount; ++i) {
        out.hand[i] = mirroredLocal(grip.hand[i]);
    }
    return out;
}

xr_math::ModelPose rightWristFromGrip(const xr_math::ModelPose& grip, const HandOffset& offset) {
    const xr_math::ModelPose leftGrip{reflect(grip.position), reflectGripAxis(grip.axis)};
    return mirroredInRoot(wristFromGrip(leftGrip, offset));
}

bool sameRootPose(const xr_math::ModelPose& a, const xr_math::ModelPose& b, float maxApart) {
    const Quat q = conjugate(toQuat(a.axis)) * toQuat(b.axis);
    const float turn = 2.0f * std::atan2(length(Vec3{q.x, q.y, q.z}), std::fabs(q.w)) * kDegreesPerRadian;
    return length(a.position - b.position) <= maxApart && turn <= kSameRootDegrees;
}

std::optional<xr_math::ModelPose> onGunRoot(const xr_math::ModelPose& root,
                                            const xr_math::ModelPose& wrist,
                                            const xr_math::ModelPose& gunRoot,
                                            float maxApart) {
    if (!sameRootPose(root, gunRoot, maxApart)) {
        return std::nullopt;
    }
    return relative(gunRoot, compose(root, wrist));
}

bool SteadyPose::update(const xr_math::ModelPose& pose, std::uint32_t serial, float maxMove) {
    if (last_ && serial == serial_) {
        return count_ >= kSteadyReads;
    }
    bool still = false;
    if (last_ && serial == serial_ + 1) {
        const Quat q = conjugate(toQuat(last_->axis)) * toQuat(pose.axis);
        const float turn = 2.0f * std::atan2(length(Vec3{q.x, q.y, q.z}), std::fabs(q.w)) * kDegreesPerRadian;
        still = length(pose.position - last_->position) <= maxMove && turn <= kSteadyDegrees;
    }
    count_ = still ? count_ + 1 : 1;
    last_ = pose;
    serial_ = serial;
    return count_ >= kSteadyReads;
}

void SteadyPose::reset() {
    last_.reset();
    count_ = 0;
}

xr_math::IdAngles leftHandTurn(const xr_math::IdAngles& rowTurn, float weight) {
    return {rowTurn.pitch, lerp(rowTurn.yaw, -rowTurn.yaw, weight),
            lerp(rowTurn.roll, -rowTurn.roll, weight)};
}

Vec3 leftGloveShift(const xr_math::EyeRelativePose& placed,
                    Vec3 wristInModel,
                    Vec3 grip,
                    const IdViewAxis& gripAxis,
                    Vec3 wristInGrip,
                    float unitsPerMetre) {
    const Vec3 target = grip + inFrame(gripAxis, wristInGrip) * unitsPerMetre;
    return target - (placed.offset + inFrame(placed.axis, wristInModel));
}

} // namespace evr::arm
