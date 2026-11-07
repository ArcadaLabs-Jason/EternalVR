#pragma once

// View-attached models (the piloted Revenant's shoulder cannons and arms): the game places them against its
// own camera, which knows nothing of the headset. Re-seating one keeps its pose relative to that camera but
// puts it on the rendered view instead: the camera's origin plus the room-scale head offset, turned to the
// view axis the eyes are built on. The model then stays fixed in the eyes' frame, as it is on a flat screen.
//
// id Tech axes (head_view.hpp): world units, idMat3 rows forward, left, up.

#include "common/vector.hpp"
#include "xr_math/head_view.hpp"

namespace evr::xr_math {

struct AttachedPose {
    Vec3 origin;
    IdViewAxis axis;
};

// A world vector in `frame`'s axes (its forward, left and up parts), and back. The piloted view is kept in
// the frame of the body yaw it was built on, so it can be put on the next tick's body yaw.
[[nodiscard]] Vec3 toFrame(Vec3 v, const IdViewAxis& frame);
[[nodiscard]] Vec3 fromFrame(Vec3 v, const IdViewAxis& frame);
// The same for each row of an axis.
[[nodiscard]] IdViewAxis axisToFrame(const IdViewAxis& axis, const IdViewAxis& frame);
[[nodiscard]] IdViewAxis axisFromFrame(const IdViewAxis& axis, const IdViewAxis& frame);

// `model` as the game placed it against a camera at `cameraOrigin` with `cameraAxis`, moved onto a view at
// cameraOrigin + headOffset with `viewAxis`. With the view equal to the camera it returns `model`.
[[nodiscard]] AttachedPose reseatOnView(const AttachedPose& model,
                                        Vec3 cameraOrigin,
                                        const IdViewAxis& cameraAxis,
                                        Vec3 headOffset,
                                        const IdViewAxis& viewAxis);

} // namespace evr::xr_math
