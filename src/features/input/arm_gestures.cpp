#include "features/input/arm_gestures.hpp"

#include "common/finite.hpp"

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

ThrowSettings sanitized(ThrowSettings s) {
    const ThrowSettings d;
    s.speed = speedOr(s.speed, d.speed);
    s.windupMinHeight = finiteInRangeOr(s.windupMinHeight, -0.5f, 0.5f, d.windupMinHeight);
    s.windupMaxForward = finiteInRangeOr(s.windupMaxForward, -0.3f, 0.4f, d.windupMaxForward);
    s.primeSeconds = finiteInRangeOr(s.primeSeconds, 0.1f, 2.0f, d.primeSeconds);
    s.cooldownSeconds = finiteInRangeOr(s.cooldownSeconds, 0.0f, 5.0f, d.cooldownSeconds);
    return s;
}

SwingSettings sanitized(SwingSettings s) {
    const SwingSettings d;
    s.speed = speedOr(s.speed, d.speed);
    s.raiseMinHeight = finiteInRangeOr(s.raiseMinHeight, -0.1f, 0.6f, d.raiseMinHeight);
    s.primeSeconds = finiteInRangeOr(s.primeSeconds, 0.1f, 2.0f, d.primeSeconds);
    s.cooldownSeconds = finiteInRangeOr(s.cooldownSeconds, 0.0f, 5.0f, d.cooldownSeconds);
    return s;
}

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// The head's heading: its forward on the floor, or with the head looking straight up or down, its up
// direction (which points along the heading looking down and against it looking up). Empty for a head
// that is not tracked.
std::optional<Vec3> heading(const HeadState& head) {
    if (!head.poseValid || !finite(head.pose.position)) {
        return std::nullopt;
    }
    const Vec3 forward = transformDirection(head.pose, {0.0f, 0.0f, -1.0f});
    Vec3 flat{forward.x, 0.0f, forward.z};
    if (!(length(flat) >= 0.2f)) {
        const Vec3 up = transformDirection(head.pose, {0.0f, 1.0f, 0.0f});
        flat = Vec3{up.x, 0.0f, up.z} * (forward.y < 0.0f ? 1.0f : -1.0f);
    }
    if (!(length(flat) >= 1e-3f) || !finite(flat)) {
        return std::nullopt;
    }
    return normalize(flat);
}

bool tracked(const HandState& hand) {
    return hand.poseValid && hand.velocityValid && finite(hand.aimPose.position) &&
           finite(hand.linearVelocity);
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

ArmGestures::ArmGestures(ThrowSettings throwSettings, SwingSettings swingSettings)
    : throw_(sanitized(throwSettings)), swing_(sanitized(swingSettings)) {}

ArmGestureOutput ArmGestures::update(const InputFrame& frame, Hand weaponHand, float dtSeconds) {
    ArmGestureOutput out;
    if (weaponHand != weaponHand_) {
        // A changed weapon hand swaps which hand does what: nothing primed carries over.
        weaponHand_ = weaponHand;
        throwPrimer_.reset();
        swingPrimer_.reset();
        bothRaised_ = false;
    }
    const std::optional<Vec3> forward = heading(frame.head);
    const Hand offHand = otherHand(weaponHand);
    const HandState& off = frame.hand(offHand);
    const HandState& weapon = frame.hand(weaponHand);
    const Vec3 eyes = forward ? frame.head.pose.position : Vec3{};

    if (!throw_.enabled || !forward || !tracked(off)) {
        throwPrimer_.reset();
    } else {
        const Vec3 fromEyes = off.aimPose.position - eyes;
        const bool woundUp =
            fromEyes.y >= throw_.windupMinHeight && dot(fromEyes, *forward) <= throw_.windupMaxForward;
        const bool fast = dot(off.linearVelocity, *forward) >= throw_.speed;
        out.thrown = throwPrimer_.step(woundUp, fast, dtSeconds, throw_.primeSeconds, throw_.cooldownSeconds);
        out.heldBack[index(offHand)] = out.thrown || throwPrimer_.primed(throw_.primeSeconds);
    }

    if (!swing_.enabled || !forward || !tracked(weapon)) {
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
