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

// `world`'s components along `axis`'s rows (the inverse of inFrame for an orthonormal axis).
Vec3 ofFrame(const IdViewAxis& axis, Vec3 world) {
    return {dot(axis.forward, world), dot(axis.left, world), dot(axis.up, world)};
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

EyeRelativePose applyLocalOffsetWithSight(const EyeRelativePose& pose,
                                          Vec3 translation,
                                          const IdAngles& rotation,
                                          const IdAngles& sight,
                                          float unitsPerMetre) {
    // In the pose's frame: the offset's rotation L (rows), the model's grip point -L t in the model's frame,
    // and the sight line s in the model's frame. K, whose first row is s, takes s onto the model's forward.
    // The model's axis L' = K^T L puts s along L's forward; its origin t' = L^T K L t keeps the grip point at
    // the pose: origin' + L'^T (-L t) = 0 in the pose's frame.
    const IdViewAxis rotated = axisFromAngles(rotation);
    const IdViewAxis turn = axisFromAngles({sight.pitch, sight.yaw, 0.0f});
    const Vec3 columns[3] = {{turn.forward.x, turn.left.x, turn.up.x},
                             {turn.forward.y, turn.left.y, turn.up.y},
                             {turn.forward.z, turn.left.z, turn.up.z}};
    const IdViewAxis local{inFrame(rotated, columns[0]), inFrame(rotated, columns[1]),
                           inFrame(rotated, columns[2])};
    const Vec3 gripOffset = inFrame(rotated, ofFrame(turn, ofFrame(rotated, translation)));
    EyeRelativePose out;
    out.offset = pose.offset + inFrame(pose.axis, gripOffset * unitsPerMetre);
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
