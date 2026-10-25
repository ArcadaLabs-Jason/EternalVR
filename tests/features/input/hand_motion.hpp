#pragma once

// Smooth hand motions for the gesture tests: a tracked head, standing or seated and facing any yaw, and both
// hands moved along paths given in the head's frame, sampled at 90 Hz with their velocities. Each move
// follows a minimum-jerk profile (the shape of a quick, deliberate arm movement): it starts and ends at rest.

#include "common/pose.hpp"
#include "common/quat.hpp"
#include "common/vector.hpp"
#include "features/input/controller_state.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace evr::test {

inline constexpr float kMotionFrame = 1.0f / 90.0f;
inline constexpr float kStandingEyes = 1.7f;
inline constexpr float kSeatedEyes = 1.2f;

// A point in the head's frame, metres from the eyes: to the head's right, above, and ahead along its heading.
struct HeadPoint {
    float right = 0.0f;
    float up = 0.0f;
    float forward = 0.0f;
};

class HandMotion {
public:
    HandMotion(float eyeHeight, float yawRadians, HeadPoint left, HeadPoint right)
        : eyes_{0.0f, eyeHeight, 0.0f}, yaw_(yawRadians), at_{left, right} {}

    // `hand` moves to `to` over `seconds`; the other hand stays where it is.
    HandMotion& move(input::Hand hand, HeadPoint to, float seconds) {
        std::array<HeadPoint, 2> target = at_;
        target[index(hand)] = to;
        return both(target[0], target[1], seconds);
    }

    // Both hands move at once over `seconds`.
    HandMotion& both(HeadPoint left, HeadPoint right, float seconds) {
        const std::array<HeadPoint, 2> from = at_;
        const std::array<HeadPoint, 2> to{left, right};
        const int count = std::max(1, static_cast<int>(std::lround(seconds / kMotionFrame)));
        for (int i = 1; i <= count; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(count);
            const float s = ((6.0f * t - 15.0f) * t + 10.0f) * t * t * t;
            const float rate = 30.0f * t * t * (1.0f - t) * (1.0f - t) / seconds; // ds/dt
            input::InputFrame frame = head();
            for (std::size_t h = 0; h < 2; ++h) {
                const Vec3 a = world(from[h]);
                const Vec3 b = world(to[h]);
                input::HandState& hand = h == 0 ? frame.left : frame.right;
                hand = tracked(a + (b - a) * s, (b - a) * rate);
            }
            frames.push_back(frame);
        }
        at_ = to;
        return *this;
    }

    // Both hands still for `seconds`.
    HandMotion& hold(float seconds) { return both(at_[0], at_[1], seconds); }

    // The frames so far, one per kMotionFrame.
    std::vector<input::InputFrame> frames;

private:
    static std::size_t index(input::Hand hand) { return static_cast<std::size_t>(hand); }

    static input::HandState tracked(Vec3 position, Vec3 velocity) {
        input::HandState hand;
        hand.poseValid = true;
        hand.aimPose = {Quat::identity(), position};
        hand.velocityValid = true;
        hand.linearVelocity = velocity;
        return hand;
    }

    input::InputFrame head() const {
        input::InputFrame frame;
        frame.head.poseValid = true;
        frame.head.pose = {Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, yaw_), eyes_};
        return frame;
    }

    Vec3 world(HeadPoint p) const {
        // Yaw 0 faces -Z with +X on the right; a positive yaw turns counter-clockwise seen from above.
        const Vec3 forward{-std::sin(yaw_), 0.0f, -std::cos(yaw_)};
        const Vec3 right{std::cos(yaw_), 0.0f, -std::sin(yaw_)};
        return eyes_ + right * p.right + Vec3{0.0f, p.up, 0.0f} + forward * p.forward;
    }

    Vec3 eyes_;
    float yaw_;
    std::array<HeadPoint, 2> at_;
};

} // namespace evr::test
