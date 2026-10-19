// The weapon arm's modifiers in each joint's parent's frame (parent_frames.hpp).

#include "features/arm/parent_frames.hpp"

#include "features/arm/arm_frames.hpp"

#include <algorithm>
#include <cmath>

namespace evr::arm {

namespace {

std::uint16_t withScale(std::uint16_t flags, bool zero) {
    return static_cast<std::uint16_t>(zero ? flags | kModScale : flags);
}

ModWrite inParentFrame(const xr_math::ModelPose& local, Vec3 scale, bool zero = false) {
    const Vec3 p = local.position;
    return {{{p.x / scale.x, p.y / scale.y, p.z / scale.z}, local.axis},
            withScale(kFlagsParentOverride, zero)};
}

ModWrite inModelSpace(const xr_math::ModelPose& pose, Vec3 scale, Vec3 origin, bool zero = false) {
    const Vec3 p = pose.position - origin;
    return {{{p.x / scale.x, p.y / scale.y, p.z / scale.z}, pose.axis}, withScale(kFlagsModelOverride, zero)};
}

} // namespace

bool uniformScale(Vec3 scale) {
    const float lo = std::min({scale.x, scale.y, scale.z});
    const float hi = std::max({scale.x, scale.y, scale.z});
    return lo > 0.0f && hi - lo <= kUniformScale * hi;
}

bool heldInParents(const GripOverrides& glove, Vec3 scale) {
    return uniformScale(scale) && !glove.modelSpace;
}

std::array<ModWrite, kGloveWrites> gloveWrites(
    const ArmPoses& arm, const GripOverrides& glove, Vec3 scale, Vec3 origin, std::uint32_t zeroScale) {
    std::array<ModWrite, kGloveWrites> out{};
    std::size_t k = 0;
    if (!heldInParents(glove, scale)) {
        out[k++] = inModelSpace(glove.wrist, scale, origin);
        for (const ArmJoint joint : kLayerModJoints) {
            out[k++] =
                inModelSpace(arm[index(joint)], scale, origin, (zeroScale & (1u << index(joint))) != 0);
        }
        for (const xr_math::ModelPose& finger : glove.fingers) {
            out[k++] = inModelSpace(finger, scale, origin);
        }
        return out;
    }
    out[k++] = inParentFrame(glove.wristLocal, scale);
    const xr_math::ModelPose* parent = &glove.wrist;
    for (const ArmJoint joint : kLayerModJoints) {
        const xr_math::ModelPose& pose = arm[index(joint)];
        out[k++] = inParentFrame(relative(*parent, pose), scale, (zeroScale & (1u << index(joint))) != 0);
        parent = &pose;
    }
    for (const xr_math::ModelPose& finger : glove.fingersLocal) {
        out[k++] = inParentFrame(finger, scale);
    }
    return out;
}

ArmPoses carriedBy(const xr_math::ModelPose& then, const xr_math::ModelPose& now, const ArmPoses& poses) {
    ArmPoses out{};
    for (std::size_t i = 0; i < kArmJointCount; ++i) {
        out[i] = compose(now, relative(then, poses[i]));
    }
    return out;
}

} // namespace evr::arm
