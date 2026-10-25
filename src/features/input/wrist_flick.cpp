#include "features/input/wrist_flick.hpp"

#include "common/finite.hpp"
#include "features/input/gesture_frame.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace evr::input {

namespace {

constexpr float kRadiansToDegrees = 180.0f / std::numbers::pi_v<float>;
// The rates are averaged over about this many seconds (an exponential filter), so one noisy frame is no
// flick.
constexpr float kSmoothingSeconds = 0.025f;
// A turn faster than this in one frame is no wrist: a tracking jump, which reads nothing. A fast flick of a
// wrist peaks at about 1500 degrees per second.
constexpr float kMaxPlausibleDegrees = 4000.0f;

FlickSettings sanitized(FlickSettings s) {
    const FlickSettings d;
    s.minRollDegrees = finiteInRangeOr(s.minRollDegrees, kMinFlickDegreesPerSecond, kMaxFlickDegreesPerSecond,
                                       d.minRollDegrees);
    s.dominance = finiteInRangeOr(s.dominance, 1.0f, 10.0f, d.dominance);
    s.maxHandSpeed = finiteInRangeOr(s.maxHandSpeed, 0.2f, 6.0f, d.maxHandSpeed);
    s.cooldownSeconds = finiteInRangeOr(s.cooldownSeconds, 0.0f, 5.0f, d.cooldownSeconds);
    return s;
}

bool finiteQuat(Quat q) {
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}

} // namespace

WristFlick::WristFlick(FlickSettings settings) : settings_(sanitized(settings)) {}

void WristFlick::reset() {
    previous_.reset();
    previousSample_ = 0.0;
    roll_ = 0.0f;
    swing_ = 0.0f;
    armed_ = true;
    armedReturn_ = true;
    quiet_ = 0.0f;
    lastSign_ = 0.0f;
}

FlickOutput WristFlick::update(const InputFrame& frame, Hand weaponHand, float dtSeconds, bool blocked) {
    FlickOutput out;
    if (!settings_.enabled) {
        return out;
    }
    if (weaponHand != weaponHand_) {
        weaponHand_ = weaponHand;
        reset();
    }
    const float dt = std::isfinite(dtSeconds) && dtSeconds > 0.0f ? dtSeconds : 0.0f;
    cooldown_ = std::max(0.0f, cooldown_ - dt);
    const HandState& hand = frame.hand(weaponHand);
    if (blocked || !handTracked(hand) || !finiteQuat(hand.aimPose.orientation)) {
        reset();
        return out;
    }
    // The time between the two samples: the poses' own clock when the frames have one, so a frame read twice
    // adds nothing and a stall counts as long as it was; otherwise the mapper's frame time, which it caps at
    // kMaxFrameSeconds (= kMaxRateGapSeconds), so a capped frame reads as a stall.
    const bool sampled = frame.sampleSeconds > 0.0 && previousSample_ > 0.0;
    if (sampled && frame.sampleSeconds == previousSample_) {
        return out; // the same sample as last time: nothing new
    }
    const float step = sampled ? static_cast<float>(frame.sampleSeconds - previousSample_) : dt;
    previousSample_ = frame.sampleSeconds;
    const Quat now = normalize(hand.aimPose.orientation);
    const std::optional<Quat> before = previous_;
    previous_ = now;
    if (!before || !(step > 1e-4f)) {
        return out; // the first frame, or a gap too short (or backwards) to read a rate from
    }
    if (step >= kMaxRateGapSeconds) {
        // A stall: the turn across it is no rate, and the smoothing starts over from here.
        roll_ = 0.0f;
        swing_ = 0.0f;
        return out;
    }

    // The rotation between the frames in the tracking space, as a rotation vector over the frame time.
    Quat delta = now * conjugate(*before);
    if (delta.w < 0.0f) {
        delta = {-delta.x, -delta.y, -delta.z, -delta.w};
    }
    const Vec3 vector{delta.x, delta.y, delta.z};
    const float sine = length(vector);
    const float angle = 2.0f * std::atan2(sine, delta.w);
    const Vec3 omega = sine > 1e-9f ? vector * (angle / sine / step) : Vec3{};
    const Vec3 pointing = rotate(now, {0.0f, 0.0f, -1.0f});
    const float roll = dot(omega, pointing) * kRadiansToDegrees;
    const float swing = length(omega - pointing * dot(omega, pointing)) * kRadiansToDegrees;
    if (std::fabs(roll) > kMaxPlausibleDegrees || swing > kMaxPlausibleDegrees) {
        return out;
    }
    const float alpha = 1.0f - std::exp(-step / kSmoothingSeconds);
    roll_ += (roll - roll_) * alpha;
    swing_ += (swing - swing_) * alpha;
    out.rollDegrees = roll_;
    out.swingDegrees = swing_;

    const float rate = std::fabs(roll_);
    quiet_ = rate < settings_.minRollDegrees * 0.25f ? quiet_ + step : 0.0f;
    armed_ = armed_ || quiet_ >= kRearmQuietSeconds;
    armedReturn_ = armedReturn_ || quiet_ >= kReturnQuietSeconds;
    const float sign = roll_ < 0.0f ? -1.0f : 1.0f;
    const bool returning = lastSign_ != 0.0f && sign != lastSign_;
    const bool still = length(hand.linearVelocity) <= settings_.maxHandSpeed;
    if ((returning ? armedReturn_ : armed_) && cooldown_ <= 0.0f && rate >= settings_.minRollDegrees &&
        rate >= settings_.dominance * swing_ && still) {
        out.flicked = true;
        armed_ = false;
        armedReturn_ = false;
        quiet_ = 0.0f;
        lastSign_ = sign;
        cooldown_ = settings_.cooldownSeconds;
    }
    return out;
}

} // namespace evr::input
