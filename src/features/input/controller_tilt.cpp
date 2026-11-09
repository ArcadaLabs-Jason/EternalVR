#include "features/input/controller_tilt.hpp"

#include "common/quat.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
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

float handTiltDegrees(const ControllerFit& fit,
                      const ControllerSettings& settings,
                      Hand hand,
                      Hand weaponHand) {
    const float own = hand == weaponHand ? fit.weaponHandTiltDegrees : fit.offHandTiltDegrees;
    return own + controllerTiltDegrees(settings, hand, weaponHand);
}

HandSpaceOffsets
handSpaceOffsets(const ControllerFit& fit, const ControllerSettings& settings, Hand hand, Hand weaponHand) {
    const Quat turn = controllerTilt(handTiltDegrees(fit, settings, hand, weaponHand)).orientation;
    return {Pose{turn, fit.gripShift}, Pose{turn, fit.aimShift}};
}

Pose scriptedHandPose(const Pose& pose,
                      const ControllerFit& fit,
                      const ControllerSettings& settings,
                      Hand hand,
                      Hand weaponHand) {
    Pose out = compose(pose, handSpaceOffsets(fit, settings, hand, weaponHand).aim);
    out.orientation = normalize(out.orientation);
    return out;
}

void fitScriptedHands(InputFrame& frame,
                      const TestInput& script,
                      Hand weaponHand,
                      const ControllerSettings& settings,
                      const std::array<ControllerFit, 2>& fits) {
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        if (!scriptPosesHand(script, hand)) {
            continue;
        }
        HandState& h = hand == Hand::Left ? frame.left : frame.right;
        const ControllerFit& fit = fits[static_cast<std::size_t>(hand)];
        h.gripPose = scriptedHandPose(h.gripPose, fit, settings, hand, weaponHand);
        h.aimPose = scriptedHandPose(h.aimPose, fit, settings, hand, weaponHand);
    }
}

std::string fitText(Hand hand,
                    std::string_view familyName,
                    const ControllerFit& fit,
                    bool familyHasFit,
                    const ControllerSettings& settings,
                    Hand weaponHand) {
    const bool weapon = hand == weaponHand;
    const float player = controllerTiltDegrees(settings, hand, weaponHand);
    const float total = handTiltDegrees(fit, settings, hand, weaponHand);
    char head[96];
    std::snprintf(head, sizeof(head), "the %s hand's %.*s controller (the %s hand)",
                  hand == Hand::Left ? "left" : "right", static_cast<int>(familyName.size()),
                  familyName.data(), weapon ? "weapon" : "off");
    char text[400];
    if (fit == ControllerFit{}) {
        std::snprintf(text, sizeof(text), "%s: %s; controller angle %.0f deg (the launcher's)", head,
                      familyHasFit ? "no fit on this runtime (the fit is worked out from SteamVR's poses)"
                                   : "no fit of its own",
                      total);
        return text;
    }
    // Centimetres, with a value that rounds to 0 printed as 0.0, not -0.0.
    const auto cm = [](float metres) {
        return std::fabs(metres) < 0.0005f ? 0.0f : metres * 100.0f;
    };
    std::snprintf(
        text, sizeof(text),
        "%s: its fit moves it %.1f cm (grip frame %.1f %.1f %.1f cm, aim frame %.1f %.1f %.1f cm) and "
        "turns it %.0f deg; with the launcher's %.0f deg, controller angle %.0f deg",
        head, cm(length(fit.gripShift)), cm(fit.gripShift.x), cm(fit.gripShift.y), cm(fit.gripShift.z),
        cm(fit.aimShift.x), cm(fit.aimShift.y), cm(fit.aimShift.z),
        weapon ? fit.weaponHandTiltDegrees : fit.offHandTiltDegrees, player, total);
    return text;
}

} // namespace evr::input
