#include "features/input/controller_tilt.hpp"

#include "common/quat.hpp"

#include <cmath>
#include <numbers>

namespace evr::input {

Pose controllerTilt(float degrees) {
    if (!std::isfinite(degrees) || degrees == 0.0f) {
        return Pose{};
    }
    return Pose{Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, degrees * std::numbers::pi_v<float> / 180.0f),
                Vec3{}};
}

void tiltController(HandState& h, float degrees) {
    if (!std::isfinite(degrees) || degrees == 0.0f) {
        return;
    }
    const Pose turn = controllerTilt(degrees);
    if (h.gripValid) {
        h.gripPose = compose(h.gripPose, turn);
        h.gripPose.orientation = normalize(h.gripPose.orientation);
    }
    if (h.poseValid) {
        h.aimPose = compose(h.aimPose, turn);
        h.aimPose.orientation = normalize(h.aimPose.orientation);
    }
}

float controllerTiltDegrees(const ControllerSettings& settings, Hand hand, Hand weaponHand) {
    return hand == weaponHand ? settings.weaponHandTiltDegrees : settings.offHandTiltDegrees;
}

void tiltControllers(InputFrame& frame, Hand weaponHand, const ControllerSettings& settings) {
    tiltController(frame.left, controllerTiltDegrees(settings, Hand::Left, weaponHand));
    tiltController(frame.right, controllerTiltDegrees(settings, Hand::Right, weaponHand));
}

} // namespace evr::input
