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
// A roll quiet this long starts the strokes over from where the hand is.
constexpr float kSettleSeconds = 0.5f;

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
    previousHeadYaw_.reset();
    previousSample_ = 0.0;
    roll_ = 0.0f;
    swing_ = 0.0f;
    armed_ = true;
    quiet_ = 0.0f;
    restartStrokes();
}

void WristFlick::restartStrokes() {
    clock_ = 0.0f;
    angle_ = 0.0f;
    stroke_ = {};
    over_ = {};
    previousStroke_.reset();
    previousOver_ = {};
}

void WristFlick::turnStroke() {
    const float size = std::fabs(stroke_.end - stroke_.start);
    if (size < kMinStrokeDegrees && previousStroke_) {
        // A wobble: the stroke before it goes on, and so does what came before that.
        stroke_ = *previousStroke_;
        over_ = previousOver_;
        previousStroke_.reset();
        return;
    }
    previousStroke_ = stroke_;
    previousOver_ = over_;
    over_ = {stroke_.overFast && size >= kFlickOverDegrees, size, stroke_.endTime};
    Stroke next;
    next.start = stroke_.end;
    next.end = angle_;
    next.heldEnd = angle_;
    next.endTime = clock_;
    next.direction = -stroke_.direction;
    stroke_ = next;
}

bool WristFlick::strokes(float roll, float step, bool still, FlickOutput& out) {
    if (quiet_ >= kSettleSeconds) {
        restartStrokes();
    }
    const float before = angle_;
    angle_ += roll * step;
    clock_ += step;
    Stroke& s = stroke_;
    if (s.direction == 0.0f) {
        if (std::fabs(angle_ - s.start) > kStrokeDegrees) {
            s.direction = angle_ > s.start ? 1.0f : -1.0f;
            s.end = angle_;
            s.heldEnd = angle_;
            s.endTime = clock_;
        } else if (quiet_ > 0.0f) {
            // Still: the first stroke starts from here, this frame's turn included (its rate may not have
            // reached the quiet line yet).
            s.start = before;
        }
    } else if ((angle_ - s.end) * s.direction > 0.0f) {
        s.end = angle_;
        if ((angle_ - s.heldEnd) * s.direction >= kHoldTremorDegrees) {
            s.heldEnd = angle_;
            s.endTime = clock_;
        }
    } else if ((s.end - angle_) * s.direction > kStrokeDegrees) {
        turnStroke(); // this stroke ends where it went furthest, and the next one starts there
    }
    if (over_.done && clock_ - over_.endTime > kFlickBackSeconds) {
        over_.done = false; // not back far enough in time: a hand turned over and held, or turned back slowly
    }
    const float rate = std::fabs(roll_);
    const float sign = roll_ < 0.0f ? -1.0f : 1.0f;
    if (stroke_.direction != 0.0f && sign == stroke_.direction && rate >= settings_.minRollDegrees) {
        // The back's test: the rate, and more roll than swing. The over's: twice the swing, the hand still.
        stroke_.backFast = stroke_.backFast || rate >= swing_;
        stroke_.overFast = stroke_.overFast || (rate >= settings_.dominance * swing_ && still);
    }
    const float back = std::fabs(angle_ - stroke_.start);
    if (!over_.done || !stroke_.backFast || back < kFlickBackDegrees || !armed_ || cooldown_ > 0.0f) {
        return false;
    }
    out.overDegrees = over_.degrees;
    out.backDegrees = back;
    over_.done = false;
    return true;
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
    const Quat now = normalize(hand.aimPose.orientation);
    const std::optional<Quat> before = previous_;
    if (before && !(step > 1e-4f)) {
        // A gap too short (or backwards) to read a rate from: the last pose stays, so the turn since it is
        // read with the next frame's time.
        return out;
    }
    previousSample_ = frame.sampleSeconds;
    previous_ = now;
    const std::optional<float> headYawBefore = previousHeadYaw_;
    previousHeadYaw_.reset();
    if (frame.head.poseValid && finiteQuat(frame.head.pose.orientation)) {
        const Vec3 face = rotate(normalize(frame.head.pose.orientation), {0.0f, 0.0f, -1.0f});
        previousHeadYaw_ = std::atan2(-face.x, -face.z);
    }
    if (!before) {
        return out; // the first frame
    }
    if (step >= kMaxRateGapSeconds) {
        // A stall: the turn across it is no rate, and the smoothing and the strokes start over from here.
        roll_ = 0.0f;
        swing_ = 0.0f;
        restartStrokes();
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
    out.pitchDegrees = std::asin(std::clamp(pointing.y, -1.0f, 1.0f)) * kRadiansToDegrees;
    out.handSpeed = length(hand.linearVelocity);
    if (headYawBefore && previousHeadYaw_) {
        float turn = *previousHeadYaw_ - *headYawBefore;
        turn = std::remainder(turn, 2.0f * std::numbers::pi_v<float>);
        out.headTurnDegrees = turn / step * kRadiansToDegrees;
    }

    quiet_ = std::fabs(roll_) < settings_.minRollDegrees * 0.25f ? quiet_ + step : 0.0f;
    armed_ = armed_ || quiet_ >= kRearmQuietSeconds;
    const bool still = length(hand.linearVelocity) <= settings_.maxHandSpeed;
    if (strokes(roll, step, still, out)) {
        out.flicked = true;
        armed_ = false;
        quiet_ = 0.0f;
        cooldown_ = settings_.cooldownSeconds;
    }
    return out;
}

} // namespace evr::input
