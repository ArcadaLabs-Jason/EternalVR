#include "features/input/controller_fit.hpp"

#include "common/quat.hpp"

#include <numbers>

namespace evr::input {

namespace {

constexpr float kRadiansPerDegree = std::numbers::pi_v<float> / 180.0f;

// The player's SteamVR raw device offset on both controllers (issue #16): the raw frame, metres.
constexpr Vec3 kIndexRawShift{0.0f, 0.06f, -0.06f};
// SteamVR's Index render model: its "grip" and "tip" (the OpenXR grip and aim), turned about the raw x axis.
constexpr float kIndexGripPitchDegrees = 15.392f;
constexpr float kIndexAimPitchDegrees = -40.0f;
// The player's launcher angles with that offset.
constexpr float kIndexWeaponHandTiltDegrees = -20.0f;
constexpr float kIndexOffHandTiltDegrees = -40.0f;

// `raw`, a vector in the raw frame, in the frame of a pose turned `pitchDegrees` about the raw frame's x.
Vec3 inPoseFrame(Vec3 raw, float pitchDegrees) {
    return rotate(Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, -pitchDegrees * kRadiansPerDegree), raw);
}

} // namespace

ControllerFit controllerFit(game::Controller family) {
    if (family != game::Controller::ValveIndex) {
        return {};
    }
    return {kIndexWeaponHandTiltDegrees, kIndexOffHandTiltDegrees,
            inPoseFrame(kIndexRawShift, kIndexGripPitchDegrees),
            inPoseFrame(kIndexRawShift, kIndexAimPitchDegrees)};
}

bool runtimeHasFits(std::string_view runtimeName) {
    return runtimeName.find("SteamVR") != std::string_view::npos;
}

ControllerFit controllerFit(game::Controller family, std::string_view runtimeName) {
    return runtimeHasFits(runtimeName) ? controllerFit(family) : ControllerFit{};
}

bool RefitRetry::take(game::Controller family, double seconds) {
    if (family_ != family) {
        family_ = family;
        attempts_ = 0;
    } else if (attempts_ >= kRefitAttempts || seconds - lastSeconds_ < kRefitRetrySeconds) {
        return false;
    }
    ++attempts_;
    lastSeconds_ = seconds;
    return true;
}

bool RefitRetry::exhausted(game::Controller family) const {
    return family_ == family && attempts_ >= kRefitAttempts;
}

} // namespace evr::input
