#include "common/pose.hpp"

namespace evr {

Pose compose(const Pose& parent, const Pose& child) {
    return {
        parent.orientation * child.orientation,
        rotate(parent.orientation, child.position) + parent.position,
    };
}

Pose inverse(const Pose& pose) {
    const Quat inverseOrientation = conjugate(pose.orientation);
    return {inverseOrientation, -rotate(inverseOrientation, pose.position)};
}

Vec3 transformPoint(const Pose& pose, Vec3 point) {
    return rotate(pose.orientation, point) + pose.position;
}

Vec3 transformDirection(const Pose& pose, Vec3 direction) {
    return rotate(pose.orientation, direction);
}

Mat4 toMatrix(const Pose& pose) {
    return makeRigidTransform(pose.orientation, pose.position);
}

Mat4 toViewMatrix(const Pose& pose) {
    return toMatrix(inverse(pose));
}

} // namespace evr
