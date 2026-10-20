#include "xr_math/launcher_pose.hpp"

#include "xr_math/hand_aim.hpp"

#include <cmath>

namespace evr::xr_math {

namespace {

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool finite(const EyeRelativePose& p) {
    return finite(p.offset) && finite(p.axis.forward) && finite(p.axis.left) && finite(p.axis.up);
}

// `world`'s components along `axis`'s rows.
Vec3 ofFrame(const IdViewAxis& axis, Vec3 world) {
    return {dot(axis.forward, world), dot(axis.left, world), dot(axis.up, world)};
}

// A vector given in `axis`'s own frame, in the world.
Vec3 inFrame(const IdViewAxis& axis, Vec3 local) {
    return axis.forward * local.x + axis.left * local.y + axis.up * local.z;
}

} // namespace

std::optional<EyeRelativePose> launcherAtHead(const EyeRelativePose& arms,
                                              const EyeRelativePose& launcher,
                                              const EyeRelativePose& head,
                                              Vec3 aim) {
    if (!finite(arms) || !finite(launcher) || !finite(head) || !isOrthonormal(arms.axis)) {
        return std::nullopt;
    }
    const std::optional<IdViewAxis> view = axisFromDirection(head.axis.forward);
    const std::optional<IdViewAxis> along = axisFromDirection(aim);
    if (!view || !along) {
        return std::nullopt;
    }
    // The launcher in the arms model's space, then that space at the head: the position in the head's view,
    // the orientation along the aim.
    const Vec3 position = ofFrame(arms.axis, launcher.offset - arms.offset);
    EyeRelativePose out;
    out.offset = head.offset + inFrame(*view, position);
    out.axis.forward = inFrame(*along, ofFrame(arms.axis, launcher.axis.forward));
    out.axis.left = inFrame(*along, ofFrame(arms.axis, launcher.axis.left));
    out.axis.up = inFrame(*along, ofFrame(arms.axis, launcher.axis.up));
    return out;
}

std::optional<Shot> equipmentLaunch(Vec3 eye,
                                    Vec3 gameOrigin,
                                    Vec3 gameDirection,
                                    const std::optional<Vec3>& drawnAlong,
                                    Vec3 weaponHand,
                                    const EyeRelativePose& ray,
                                    float maxReach) {
    const std::optional<Vec3> direction =
        carryAimOffset(gameDirection, drawnAlong ? *drawnAlong : weaponHand, ray.axis.forward);
    if (!direction) {
        return std::nullopt;
    }
    if (!drawnAlong) {
        return shotFromHand(eye, ray.offset, *direction, maxReach, 0.0f);
    }
    const std::optional<IdViewAxis> axis = axisFromDirection(*direction);
    if (!axis || !finite(gameOrigin)) {
        return std::nullopt;
    }
    return Shot{gameOrigin, *axis};
}

} // namespace evr::xr_math
