#include "xr_math/weapon_pose.hpp"

#include "xr_math/hand_aim.hpp"

#include <cmath>

namespace evr::xr_math {

namespace {

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// A vector given in `axis`'s own frame (forward, left, up components), in the world.
Vec3 inFrame(const IdViewAxis& axis, Vec3 local) {
    return axis.forward * local.x + axis.left * local.y + axis.up * local.z;
}

} // namespace

EyeRelativePose controllerRelativeToEye(const IdViewAxis& body,
                                        Vec3 headOffsetWorld,
                                        const Pose& head,
                                        const Pose& controller,
                                        float unitsPerMetre) {
    EyeRelativePose pose;
    pose.offset = headOffsetWorld + trackingToWorld(body, controller.position - head.position, unitsPerMetre);
    pose.axis = composeHeadAxis(body, openXrToIdTech(normalize(controller.orientation)));
    return pose;
}

std::optional<IdViewAxis> axisFromDirection(Vec3 direction) {
    if (!finite(direction) || length(direction) <= 1e-6f) {
        return std::nullopt;
    }
    IdViewAxis axis;
    axis.forward = normalize(direction);
    const Vec3 worldUp{0.0f, 0.0f, 1.0f};
    Vec3 left = cross(worldUp, axis.forward);
    if (length(left) < 1e-4f) {
        left = {0.0f, 1.0f, 0.0f};
    }
    axis.left = normalize(left);
    axis.up = cross(axis.forward, axis.left);
    return axis;
}

EyeRelativePose applyLocalOffset(const EyeRelativePose& pose,
                                 Vec3 translation,
                                 const IdAngles& rotation,
                                 float unitsPerMetre) {
    EyeRelativePose out;
    out.offset = pose.offset + inFrame(pose.axis, translation * unitsPerMetre);
    // The rotation's rows are directions in the pose's own frame.
    const IdViewAxis local = axisFromAngles(rotation);
    out.axis = {inFrame(pose.axis, local.forward), inFrame(pose.axis, local.left),
                inFrame(pose.axis, local.up)};
    return out;
}

std::optional<Shot>
shotFromHand(Vec3 eye, Vec3 rayOffset, Vec3 direction, float maxReach, float muzzleDistance) {
    if (!finite(eye) || !finite(rayOffset) || !std::isfinite(maxReach) || maxReach < 0.0f ||
        !std::isfinite(muzzleDistance)) {
        return std::nullopt;
    }
    const auto axis = axisFromDirection(direction);
    if (!axis) {
        return std::nullopt;
    }
    Vec3 offset = rayOffset;
    const float reach = length(offset);
    if (reach > maxReach) {
        offset = reach > 0.0f ? offset * (maxReach / reach) : Vec3{};
    }
    return Shot{eye + offset + axis->forward * muzzleDistance, *axis};
}

Vec3 atEye(Vec3 eye, const EyeRelativePose& pose) {
    return eye + pose.offset;
}

} // namespace evr::xr_math
