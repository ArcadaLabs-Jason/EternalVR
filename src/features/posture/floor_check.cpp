#include "features/posture/floor_check.hpp"

#include "features/posture/posture_detector.hpp"

#include <cmath>

namespace evr::posture {

std::optional<float>
FloorCheck::update(std::optional<float> headAboveFloor, float headLocalY, double seconds) {
    if (!headAboveFloor || !std::isfinite(*headAboveFloor) || !std::isfinite(headLocalY) ||
        !std::isfinite(seconds) || pending_) {
        return std::nullopt;
    }
    const float floorY = headLocalY - *headAboveFloor;
    // A floor that moves steps the reading too; LOCAL moving steps only the floor's LOCAL height.
    const bool readingStepped = prev_ && std::fabs(*headAboveFloor - *prev_) > kFloorMoveMetres;
    const bool floorStepped = prevFloorY_ && std::fabs(floorY - *prevFloorY_) > kFloorMoveMetres;
    prev_ = headAboveFloor;
    prevFloorY_ = floorY;
    if (watchUntil_ >= 0.0 && (seconds > watchUntil_ || seconds < watchUntil_ - kFloorWatchSeconds)) {
        watchUntil_ = -1.0; // the watch is over (or time went backwards)
    }
    if (watchUntil_ >= 0.0 && !moved_ && std::fabs(floorY - floorBefore_) > kFloorMoveMetres) {
        if (floorStepped && readingStepped) {
            moved_ = true;
            movedInLocal_ = true;
            move_ = floorY - floorBefore_;
            watchUntil_ = -1.0;
        } else {
            floorBefore_ = floorY; // LOCAL moved, the floor with it
        }
    }
    if (moved_) {
        const bool back = movedInLocal_ ? std::fabs(floorY - floorBefore_) <= kFloorMoveMetres
                                        : std::fabs(*headAboveFloor - before_) <= kFloorMoveMetres;
        if (back) {
            moved_ = false;
        }
    }
    if (moved_ || !plausibleHeadHeight(*headAboveFloor)) {
        return std::nullopt;
    }
    last_ = headAboveFloor;
    lastFloorY_ = floorY;
    return headAboveFloor;
}

void FloorCheck::localMoved() {
    watchUntil_ = -1.0;
    lastFloorY_.reset();
    prev_.reset();
    prevFloorY_.reset();
    if (moved_ && movedInLocal_) {
        // The floor's LOCAL height can no longer say when it is back; the reading can.
        movedInLocal_ = false;
        moved_ = last_.has_value();
        before_ = last_.value_or(0.0f);
    }
}

void FloorCheck::spaceChangePending() {
    localMoved();
    pending_ = true;
}

std::optional<float>
FloorCheck::afterSpaceChange(std::optional<float> headAboveFloor, float headLocalY, double seconds) {
    pending_ = false;
    if (moved_ || !headAboveFloor || !std::isfinite(*headAboveFloor)) {
        return std::nullopt;
    }
    const float move = last_ ? *headAboveFloor - *last_ : 0.0f;
    if (std::fabs(move) <= kFloorMoveMetres) {
        // No move yet; a floor the runtime moves a moment later is still seen.
        if (plausibleHeadHeight(*headAboveFloor) && std::isfinite(headLocalY) && std::isfinite(seconds)) {
            floorBefore_ = headLocalY - *headAboveFloor;
            watchUntil_ = seconds + kFloorWatchSeconds;
            prev_ = headAboveFloor;
            prevFloorY_ = floorBefore_;
        }
        return std::nullopt;
    }
    moved_ = true;
    movedInLocal_ = false;
    before_ = *last_;
    move_ = -move; // the head reads `move` higher above it: the floor went down by that much
    return move_;
}

void FloorCheck::floorChangePending(double seconds) {
    if (pending_ || moved_ || !lastFloorY_ || !std::isfinite(seconds)) {
        return;
    }
    floorBefore_ = *lastFloorY_;
    watchUntil_ = seconds + kFloorWatchSeconds;
}

void FloorCheck::sessionRestarted() {
    localMoved();
    pending_ = false;
}

void FloorCheck::trustAgain() {
    moved_ = false;
    watchUntil_ = -1.0;
    pending_ = false;
}

} // namespace evr::posture
