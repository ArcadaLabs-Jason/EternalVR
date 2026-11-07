#include "xr_math/view_attached.hpp"

namespace evr::xr_math {

namespace {

// A world direction in `from`'s frame, rebuilt in `to`'s frame.
Vec3 carry(Vec3 v, const IdViewAxis& from, const IdViewAxis& to) {
    return to.forward * dot(v, from.forward) + to.left * dot(v, from.left) + to.up * dot(v, from.up);
}

} // namespace

Vec3 toFrame(Vec3 v, const IdViewAxis& frame) {
    return {dot(v, frame.forward), dot(v, frame.left), dot(v, frame.up)};
}

Vec3 fromFrame(Vec3 v, const IdViewAxis& frame) {
    return frame.forward * v.x + frame.left * v.y + frame.up * v.z;
}

IdViewAxis axisToFrame(const IdViewAxis& axis, const IdViewAxis& frame) {
    return {toFrame(axis.forward, frame), toFrame(axis.left, frame), toFrame(axis.up, frame)};
}

IdViewAxis axisFromFrame(const IdViewAxis& axis, const IdViewAxis& frame) {
    return {fromFrame(axis.forward, frame), fromFrame(axis.left, frame), fromFrame(axis.up, frame)};
}

AttachedPose reseatOnView(const AttachedPose& model,
                          Vec3 cameraOrigin,
                          const IdViewAxis& cameraAxis,
                          Vec3 headOffset,
                          const IdViewAxis& viewAxis) {
    AttachedPose placed;
    placed.origin = cameraOrigin + headOffset + carry(model.origin - cameraOrigin, cameraAxis, viewAxis);
    placed.axis.forward = carry(model.axis.forward, cameraAxis, viewAxis);
    placed.axis.left = carry(model.axis.left, cameraAxis, viewAxis);
    placed.axis.up = carry(model.axis.up, cameraAxis, viewAxis);
    return placed;
}

} // namespace evr::xr_math
