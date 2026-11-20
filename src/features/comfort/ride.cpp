#include "features/comfort/ride.hpp"

#include "common/finite.hpp"

#include <algorithm>
#include <cmath>

namespace evr::comfort {

namespace {

RideTiming sanitized(RideTiming t) {
    const RideTiming d;
    t.startSpeed = finiteInRangeOr(t.startSpeed, 0.0f, 100.0f, d.startSpeed);
    t.stopSpeed = finiteInRangeOr(t.stopSpeed, 0.0f, 100.0f, d.stopSpeed);
    if (!(t.stopSpeed <= t.startSpeed)) {
        t.startSpeed = d.startSpeed;
        t.stopSpeed = d.stopSpeed;
    }
    t.startSeconds = finiteInRangeOr(t.startSeconds, 0.0f, 10.0f, d.startSeconds);
    t.stopSeconds = finiteInRangeOr(t.stopSeconds, 0.0f, 10.0f, d.stopSeconds);
    t.airSeconds = finiteInRangeOr(t.airSeconds, 0.0f, 10.0f, d.airSeconds);
    t.amountStartSpeed = finiteInRangeOr(t.amountStartSpeed, 0.0f, 100.0f, d.amountStartSpeed);
    t.fullSpeed = finiteInRangeOr(t.fullSpeed, 0.0f, 100.0f, d.fullSpeed);
    if (!(t.fullSpeed > t.amountStartSpeed)) {
        t.amountStartSpeed = d.amountStartSpeed;
        t.fullSpeed = d.fullSpeed;
    }
    return t;
}

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

float CarriedSpeed::add(double seconds, Vec3 origin, Vec3 ownVelocity) {
    if (!std::isfinite(seconds) || !finite(origin) || !finite(ownVelocity)) {
        count_ = 0;
        return -1.0f;
    }
    if (count_ > 0) {
        const Sample& last = samples_[(head_ + kSamples - 1) % kSamples];
        if (seconds < last.t || seconds - last.t > kMaxWindowSeconds ||
            length(origin - last.origin) > kTeleportMetres) {
            count_ = 0;
        }
    }
    samples_[head_] = Sample{seconds, origin, ownVelocity};
    head_ = (head_ + 1) % kSamples;
    count_ = std::min(count_ + 1, kSamples);
    // The oldest sample within the window's longest span.
    std::size_t n = 0;
    double far = seconds;
    std::size_t farIndex = 0;
    for (std::size_t i = 1; i < count_; ++i) {
        const std::size_t index = (head_ + kSamples - 1 - i) % kSamples;
        if (seconds - samples_[index].t > kMaxWindowSeconds) {
            break;
        }
        far = samples_[index].t;
        farIndex = index;
        n = i;
    }
    if (n == 0 || seconds - far < kMinWindowSeconds) {
        return -1.0f;
    }
    // The mean own velocity over the samples after the far one (each stands for the step that ends at it).
    Vec3 sum;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t index = (head_ + kSamples - 1 - i) % kSamples;
        const std::size_t before = (index + kSamples - 1) % kSamples;
        const float dt = static_cast<float>(samples_[index].t - samples_[before].t);
        sum = sum + samples_[index].velocity * dt;
    }
    const float span = static_cast<float>(seconds - far);
    const Vec3 body = (origin - samples_[farIndex].origin) * (1.0f / span); // where the body went
    const Vec3 own = sum * (1.0f / span);                                   // where its own motion says
    const float ownLevel = std::hypot(own.x, own.y);
    float speed = 0.0f;
    if (ownLevel > kOwnWalkSpeed) {
        // Moving under its own power: only level speed beyond the player's own counts. A body slower than
        // its own velocity (into a wall or a corner, held at a stuck spot, the weapon wheel's slow motion, a
        // stalled frame) is never carried, and the steps of stairs (a lift with no upward velocity) never
        // count; a lift carrying a walking player up or down is missed for that.
        speed = std::max(0.0f, std::hypot(body.x, body.y) - ownLevel);
    } else {
        // Standing or nearly: the body's motion less its own, without the part that only falls short of the
        // own velocity (a knockback stopped by a wall, a jump's last push held back).
        Vec3 carried = body - own;
        const float ownSpeed = length(own);
        if (ownSpeed > kOwnGuardSpeed) {
            const Vec3 dir = own * (1.0f / ownSpeed);
            const float along = carried.x * dir.x + carried.y * dir.y + carried.z * dir.z;
            if (along < 0.0f) {
                carried = carried - dir * along;
            }
        }
        speed = length(carried);
    }
    return std::isfinite(speed) ? speed : -1.0f;
}

const char* rideEndName(RideEnd end) {
    switch (end) {
    case RideEnd::None:
        return "none";
    case RideEnd::Stopped:
        return "the ride stopped";
    case RideEnd::OffGround:
        return "off the ground";
    case RideEnd::Excluded:
        return "a cutscene, climb, swim, piloted demon or death";
    case RideEnd::Lost:
        return "no reading";
    }
    return "?";
}

RideDetector::RideDetector(RideTiming timing) : timing_(sanitized(timing)) {}

RideChange RideDetector::end(RideEnd why) {
    fastSince_ = -1.0;
    slowSince_ = -1.0;
    airSince_ = -1.0;
    if (!riding_) {
        return RideChange::None;
    }
    riding_ = false;
    lastEnd_ = why;
    return RideChange::Ended;
}

RideChange RideDetector::update(const RideSample& sample, double now) {
    if (!std::isfinite(now)) {
        return RideChange::None;
    }
    const bool gap = haveLast_ && (now - last_ > kMaxFrameGapSeconds || now < last_);
    haveLast_ = true;
    last_ = now;
    if (gap || !sample.valid) {
        return end(RideEnd::Lost);
    }
    if (sample.excluded) {
        return end(RideEnd::Excluded);
    }
    const float speed = std::isfinite(sample.carriedSpeed) ? std::fabs(sample.carriedSpeed) : 0.0f;
    if (!riding_) {
        if (!sample.onGround || speed < timing_.startSpeed) {
            fastSince_ = -1.0;
            return RideChange::None;
        }
        if (fastSince_ < 0.0) {
            fastSince_ = now;
            firstSpeed_ = speed;
        }
        if (now - fastSince_ < timing_.startSeconds) {
            return RideChange::None;
        }
        riding_ = true;
        rideStart_ = fastSince_;
        startSpeed_ = firstSpeed_;
        peakSpeed_ = std::max(firstSpeed_, speed);
        speed_ = speed;
        slowSince_ = -1.0;
        airSince_ = -1.0;
        lastEnd_ = RideEnd::None;
        return RideChange::Started;
    }
    if (!sample.onGround) {
        slowSince_ = -1.0;
        if (airSince_ < 0.0) {
            airSince_ = now;
        }
        return now - airSince_ >= timing_.airSeconds ? end(RideEnd::OffGround) : RideChange::None;
    }
    airSince_ = -1.0;
    speed_ = speed;
    peakSpeed_ = std::max(peakSpeed_, speed);
    if (speed >= timing_.stopSpeed) {
        slowSince_ = -1.0;
        return RideChange::None;
    }
    if (slowSince_ < 0.0) {
        slowSince_ = now;
    }
    return now - slowSince_ >= timing_.stopSeconds ? end(RideEnd::Stopped) : RideChange::None;
}

float RideDetector::amount() const {
    if (!riding_) {
        return 0.0f;
    }
    return std::clamp((speed_ - timing_.amountStartSpeed) / (timing_.fullSpeed - timing_.amountStartSpeed),
                      0.0f, 1.0f);
}

void RideDetector::reset() {
    *this = RideDetector(timing_);
}

RideChange RideWatch::update(const RideFrame& frame, double now) {
    carried_ = 0.0f;
    if (!frame.valid || frame.excluded) {
        speed_.reset();
    } else {
        carried_ = std::max(0.0f, speed_.add(now, frame.origin, frame.ownVelocity));
    }
    RideSample sample;
    sample.valid = frame.valid;
    sample.excluded = frame.excluded;
    sample.onGround = frame.onGround;
    sample.carriedSpeed = carried_;
    return ride_.update(sample, now);
}

void RideWatch::reset() {
    speed_.reset();
    ride_.reset();
    carried_ = 0.0f;
}

} // namespace evr::comfort
