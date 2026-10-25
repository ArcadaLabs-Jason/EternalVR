#include "features/input/arm_gestures.hpp"

#include "common/finite.hpp"
#include "features/input/gesture_frame.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>

namespace evr::input {

namespace {

constexpr std::size_t index(Hand hand) {
    return static_cast<std::size_t>(hand);
}

float speedOr(float value, float fallback) {
    return std::isfinite(value) ? std::clamp(value, kMinGestureSpeed, kMaxGestureSpeed) : fallback;
}

SwingSettings sanitized(SwingSettings s) {
    const SwingSettings d;
    s.speed = speedOr(s.speed, d.speed);
    s.raiseMinHeight = finiteInRangeOr(s.raiseMinHeight, -0.1f, 0.6f, d.raiseMinHeight);
    s.primeSeconds = finiteInRangeOr(s.primeSeconds, 0.1f, 2.0f, d.primeSeconds);
    s.cooldownSeconds = finiteInRangeOr(s.cooldownSeconds, 0.0f, 5.0f, d.cooldownSeconds);
    return s;
}

} // namespace

bool ArmGestures::Primer::step(bool inPose, bool fast, float dt, float primeSeconds, float cooldownSeconds) {
    cooldown = std::max(0.0f, cooldown - dt);
    if (inPose) {
        sincePose = 0.0f;
    } else if (sincePose >= 0.0f) {
        sincePose += dt;
    }
    if (!primed(primeSeconds) || !fast || cooldown > 0.0f) {
        return false;
    }
    sincePose = -1.0f;
    cooldown = cooldownSeconds;
    return true;
}

ArmGestures::ArmGestures(SwingSettings swingSettings) : swing_(sanitized(swingSettings)) {}

ArmGestureOutput ArmGestures::update(const InputFrame& frame, Hand weaponHand, float dtSeconds) {
    ArmGestureOutput out;
    if (weaponHand != weaponHand_) {
        // A changed weapon hand swaps which hand does what: nothing primed carries over.
        weaponHand_ = weaponHand;
        swingPrimer_.reset();
        bothRaised_ = false;
    }
    const std::optional<Vec3> forward = headHeading(frame.head);
    const Hand offHand = otherHand(weaponHand);
    const HandState& off = frame.hand(offHand);
    const HandState& weapon = frame.hand(weaponHand);
    const Vec3 eyes = forward ? frame.head.pose.position : Vec3{};

    if (!swing_.enabled || !forward || !handTracked(weapon)) {
        swingPrimer_.reset();
        bothRaised_ = false;
    } else {
        const bool raised = weapon.aimPose.position.y - eyes.y >= swing_.raiseMinHeight;
        const bool otherRaised = off.poseValid && std::isfinite(off.aimPose.position.y) &&
                                 off.aimPose.position.y - eyes.y >= swing_.raiseMinHeight;
        bothRaised_ = raised && (bothRaised_ || otherRaised);
        if (bothRaised_) {
            swingPrimer_.reset();
        }
        const bool fast = -weapon.linearVelocity.y >= swing_.speed;
        out.swung = !bothRaised_ &&
                    swingPrimer_.step(raised, fast, dtSeconds, swing_.primeSeconds, swing_.cooldownSeconds);
        out.heldBack[index(weaponHand)] = out.swung || swingPrimer_.primed(swing_.primeSeconds);
    }
    return out;
}

} // namespace evr::input
