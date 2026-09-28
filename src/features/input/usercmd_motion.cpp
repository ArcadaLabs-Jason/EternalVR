#include "features/input/usercmd_motion.hpp"

#include <algorithm>
#include <cmath>

namespace evr::input {

MoveAxes quantizeMove(Axis2 move, int maxValue) {
    if (!isFinite(move) || maxValue <= 0) {
        return {};
    }
    const float length = magnitude(move);
    if (length > 1.0f) {
        move = move * (1.0f / length);
    }
    const auto axis = [maxValue](float value) {
        const long rounded = std::lround(value * static_cast<float>(maxValue));
        return static_cast<int>(std::clamp<long>(rounded, -maxValue, maxValue));
    };
    return {axis(move.y), axis(move.x)};
}

std::int32_t AngleUnitAccumulator::add(float degrees) {
    if (!std::isfinite(degrees)) {
        return 0;
    }
    pendingUnits_ += static_cast<double>(degrees) * static_cast<double>(kAngleUnitsPerDegree);
    // Round to nearest, so the units handed out never trail the degrees requested by more than half a
    // unit, and float noise in a sum of steps cannot lose the last unit of a whole turn.
    const double whole = std::round(pendingUnits_);
    pendingUnits_ -= whole;
    return static_cast<std::int32_t>(whole);
}

float AngleUnitAccumulator::pendingDegrees() const {
    return static_cast<float>(pendingUnits_ / static_cast<double>(kAngleUnitsPerDegree));
}

Locomotion::Locomotion(StickResponse response) : response_(sanitizedResponse(response, kMoveStickResponse)) {}

Axis2 Locomotion::update(Axis2 stick,
                         LocomotionFrame frame,
                         const HeadState& head,
                         const HandState& offHand,
                         float viewYawRadians) {
    lastYaw_ = direction_.update(frame, head, offHand);
    const Axis2 shaped = applyStickResponse(stick, response_);
    if (!std::isfinite(viewYawRadians)) {
        return {};
    }
    Axis2 move = rotateIntoViewFrame(shaped, lastYaw_, viewYawRadians);
    const float length = magnitude(move);
    if (length > 1.0f) {
        move = move * (1.0f / length);
    }
    return move;
}

Vec3 moveInTrackingSpace(Axis2 move, float yawRadians) {
    // Right and forward for yaw a are (cos a, 0, -sin a) and (-sin a, 0, -cos a) (locomotion_direction.cpp).
    const float c = std::cos(yawRadians);
    const float s = std::sin(yawRadians);
    return {move.x * c - move.y * s, 0.0f, -move.x * s - move.y * c};
}

} // namespace evr::input
