#include "features/arm/hand_offset.hpp"

namespace evr::arm {

IdViewAxis wristAxisFromGripAxis(const IdViewAxis& grip) {
    return {grip.up, grip.forward, grip.left};
}

IdViewAxis gripAxisFromWristAxis(const IdViewAxis& wrist) {
    return {wrist.left, wrist.up, wrist.forward};
}

ModelPose wristFromGrip(const ModelPose& grip, const HandOffset& offset) {
    // The offset's rotation, rows in the grip's own coordinates, placed in the grip's frame.
    const ModelPose local{offset.translation, xr_math::axisFromAngles(offset.rotation)};
    const ModelPose moved = compose(grip, local);
    return {moved.position, wristAxisFromGripAxis(moved.axis)};
}

ModelPose gripFromWrist(const ModelPose& wrist, const HandOffset& offset) {
    const ModelPose moved{wrist.position, gripAxisFromWristAxis(wrist.axis)};
    const ModelPose local{offset.translation, xr_math::axisFromAngles(offset.rotation)};
    return parentFor(moved, local);
}

} // namespace evr::arm
