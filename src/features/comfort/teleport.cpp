#include "features/comfort/teleport.hpp"

#include <cmath>

namespace evr::comfort {

namespace {

float wrap180(float degrees) {
    float d = std::fmod(degrees + 180.0f, 360.0f);
    if (d < 0.0f) {
        d += 360.0f;
    }
    return d - 180.0f;
}

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

std::optional<Teleport> TeleportWatch::finish() {
    pending_ = false;
    return teleport_;
}

std::optional<Teleport> TeleportWatch::update(
    double seconds, std::optional<Vec3> origin, std::optional<Vec3> velocity, float bodyYaw, bool takeover) {
    started_ = false;
    if (!std::isfinite(seconds) || !std::isfinite(bodyYaw)) {
        hasLast_ = false;
        return pending_ ? finish() : std::nullopt;
    }
    if (origin && !finite(*origin)) {
        origin.reset();
    }
    if (!origin || (velocity && !finite(*velocity))) {
        velocity.reset();
    }
    std::optional<Teleport> done;
    const bool gap = !hasLast_ || seconds - lastSeconds_ > kGapSeconds || seconds < lastSeconds_;
    if (pending_) {
        if (gap) {
            done = finish(); // the count ends with the last view before the gap
        } else {
            const float step = wrap180(bodyYaw - lastYaw_);
            if (std::fabs(step) > std::fabs(teleport_.largestStep)) {
                teleport_.largestStep = step;
            }
            teleport_.turn = wrap180(bodyYaw - startYaw_);
            ++teleport_.views;
        }
    }
    if (!gap && origin && lastOriginKnown_) {
        const Vec3 move = *origin - lastOrigin_;
        const float metres = length(move);
        const double dt = seconds - lastSeconds_;
        // What the player physics' own velocity does not explain of the move.
        Vec3 unexplained = move;
        if (velocity && lastVelocity_) {
            unexplained = move - (*velocity + *lastVelocity_) * (0.5f * static_cast<float>(dt));
        }
        const float off = length(unexplained);
        if (off > (takeover ? kTakeoverMetres : kMetres) && (dt <= 0.0 || off / dt > kSpeed)) {
            if (pending_) {
                // The jump before this one is reported with the views counted so far.
                done = finish();
            }
            pending_ = true;
            started_ = true;
            teleport_ = {};
            teleport_.from = lastOrigin_;
            teleport_.to = *origin;
            teleport_.metres = metres;
            teleport_.seconds = static_cast<float>(dt);
            teleport_.largestStep = wrap180(bodyYaw - lastYaw_);
            teleport_.turn = teleport_.largestStep;
            teleport_.views = 0;
            startYaw_ = lastYaw_;
        }
    }
    hasLast_ = true;
    lastSeconds_ = seconds;
    lastYaw_ = bodyYaw;
    lastOriginKnown_ = origin.has_value();
    lastVelocity_ = velocity;
    if (origin) {
        lastOrigin_ = *origin;
    }
    if (pending_ && teleport_.views >= kSettleViews && !done) {
        done = finish();
    }
    return done;
}

} // namespace evr::comfort
