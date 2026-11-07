#pragma once

// A scripted weapon hand for Roll wrist's tests (features/input/wrist_flick.hpp): the right hand at 90 Hz,
// rolled about its pointing axis, turned about the vertical and pitched, moving at a given velocity.

#include "features/input/input_frames.hpp"
#include "features/input/wrist_flick.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace evr::test::wrist {

inline constexpr float kFrame = 1.0f / 90.0f;

inline float radians(float degrees) {
    return degrees * std::numbers::pi_v<float> / 180.0f;
}

inline input::FlickSettings flickOn() {
    input::FlickSettings s;
    s.enabled = true;
    return s;
}

// The right hand pitched up by `pitchDegrees`, rolled by `rollDegrees` about its pointing axis and turned by
// `yawDegrees` about the vertical, moving at `velocity`.
inline input::InputFrame
handAt(float rollDegrees, float yawDegrees = 0.0f, float pitchDegrees = 0.0f, Vec3 velocity = {}) {
    input::InputFrame frame = restingFrame();
    const Quat pitch = Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, radians(pitchDegrees));
    const Quat roll = Quat::fromAxisAngle({0.0f, 0.0f, -1.0f}, radians(rollDegrees));
    const Quat yaw = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, radians(yawDegrees));
    frame.right.aimPose.orientation = yaw * pitch * roll;
    frame.right.linearVelocity = velocity;
    return frame;
}

// A weapon hand moving continuously: its roll and yaw carry on from where the last run ended.
struct Rig {
    explicit Rig(input::FlickSettings settings = flickOn(), float pitchDegrees = 0.0f)
        : flick(settings), pitch(pitchDegrees) {
        stepBy(0.0f, 0.0f);
    }

    // One frame that rolls `rollDegrees` and turns `yawDegrees`.
    input::FlickOutput stepBy(float rollDegrees, float yawDegrees, Vec3 velocity = {}) {
        roll += rollDegrees;
        yaw += yawDegrees;
        return flick.update(handAt(roll, yaw, pitch, velocity), input::Hand::Right, kFrame);
    }

    input::FlickOutput step(float rollRate, float yawRate, float pitchDegrees = 0.0f, Vec3 velocity = {}) {
        pitch = pitchDegrees;
        return stepBy(rollRate * kFrame, yawRate * kFrame, velocity);
    }

    // `seconds` of a constant roll rate and yaw rate (degrees per second); how many frames flicked.
    int
    run(float rollRate, float seconds, float yawRate = 0.0f, float pitchDegrees = 0.0f, Vec3 velocity = {}) {
        int count = 0;
        const int frames = static_cast<int>(std::lround(seconds / kFrame));
        for (int i = 0; i < frames; ++i) {
            count += step(rollRate, yawRate, pitchDegrees, velocity).flicked ? 1 : 0;
        }
        return count;
    }

    // A roll of exactly `degrees` (signed) at `rate` degrees per second, the last frame shorter, while
    // turning at `yawRate`; how many frames flicked.
    int rollBy(float degrees, float rate, float yawRate = 0.0f) {
        int count = 0;
        const float perFrame = std::fabs(rate) * kFrame;
        const float sign = degrees < 0.0f ? -1.0f : 1.0f;
        for (float left = std::fabs(degrees); left > 1e-4f;) {
            const float turn = std::min(left, perFrame);
            count += stepBy(sign * turn, yawRate * kFrame).flicked ? 1 : 0;
            left -= turn;
        }
        return count;
    }

    // The hand turned over `degrees` at `rollRate` and straight back at the same rate; how many frames
    // flicked.
    int flickOverAndBack(float rollRate = 900.0f, float degrees = 135.0f) {
        const float seconds = degrees / std::fabs(rollRate);
        const int over = run(rollRate, seconds);
        return over + run(-rollRate, seconds);
    }

    input::WristFlick flick;
    float pitch = 0.0f;
    float roll = 0.0f;
    float yaw = 0.0f;
};

} // namespace evr::test::wrist
