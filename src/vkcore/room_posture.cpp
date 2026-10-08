// Posture while playing: the floor's reading, posture re-detection and walking while seated (room_scale.hpp,
// docs/VR_ROOMSCALE.md "Posture and eye height", "Posture re-detection", "Seated").

#include "vkcore/room_scale.hpp"

#include "vkcore/log.hpp"

#include <algorithm>
#include <optional>

namespace evr::vkcore {

namespace {

// A standing head's usual height above the floor: the tracker's reference after walking while seated when
// the floor cannot be read.
constexpr float kStandingHeadMetres = 1.65f;

} // namespace

void RoomScale::updateFloor(std::optional<float> headAboveFloor, float headLocalY, double seconds) {
    const bool wasMoved = floor_.moved();
    floorNow_ = floor_.update(headAboveFloor, headLocalY, seconds);
    if (!wasMoved && floor_.moved() && ++floorMoveLogs_ <= 20) {
        // A move across the runtime's recenter is logged with the re-anchor; this is the floor space's own
        // (or one just after a re-anchor).
        EVR_LOG("room: the floor moved %.2f m; its readings are ignored until it is back",
                static_cast<double>(floor_.move()));
    } else if (wasMoved && !floor_.moved() && headAboveFloor && ++floorBackLogs_ <= 20) {
        EVR_LOG("room: the floor is back (head %.2f m above it); its readings are used again",
                static_cast<double>(*headAboveFloor));
    }
}

void RoomScale::readFloor(const Input& in, float lift) {
    const double seconds = in.seconds;
    const std::optional<float> headAboveFloor = in.positionValid && in.headAboveFloor
                                                    ? std::optional<float>(*in.headAboveFloor + lift)
                                                    : std::nullopt;
    updateFloor(headAboveFloor, in.localHead.position.y + lift, seconds);
    if (!headAboveFloor) {
        return;
    }
    const bool implausible = !posture::plausibleHeadHeight(*headAboveFloor);
    if (implausible == floorImplausible_) {
        return;
    }
    floorImplausible_ = implausible;
    if (implausible) {
        floorImplausibleSince_ = seconds;
        ++floorImplausibleRuns_;
    }
    if (floorImplausibleRuns_ > 20) {
        return;
    }
    if (implausible) {
        EVR_LOG("room: head %.2f m above the floor, too %s to trust; floor ignored, posture stays %s",
                static_cast<double>(*headAboveFloor),
                *headAboveFloor < posture::kMinHeadAboveFloorMetres ? "low" : "high",
                roomscale::postureName(roomPosture()));
    } else {
        EVR_LOG("room: head %.2f m above the floor again; floor used (ignored for %.1f s)",
                static_cast<double>(*headAboveFloor), seconds - floorImplausibleSince_);
    }
}

void RoomScale::trackPosture(const Input& in, double now) {
    if (roomScaleSettings().posture != posture::PostureOverride::Auto || !anchorTaken_ || !in.positionValid ||
        reanchor_.active) {
        return;
    }
    if (const auto change = postureTracker_.update(floorNow_, in.seconds)) {
        if (change->from == posture::Posture::Unknown) {
            // No usable floor at the anchor, one now: the height is re-anchored behind the blink as on a
            // posture change, which also gives `real` eye height its floor without a jump.
            EVR_LOG(
                "room: posture %s (detected once the floor could be read: head %.2f m above it for %.1f s); "
                "re-anchoring the height",
                roomscale::postureName(change->to), static_cast<double>(change->heightMetres),
                static_cast<double>(change->heldSeconds));
            scheduleReanchor(roomscale::RecenterKind::Height, "posture detected", now, false);
            return;
        }
        EVR_LOG("room: posture change: %s -> %s (head %.2f m above the floor for %.1f s); re-anchoring the "
                "height",
                roomscale::postureName(change->from), roomscale::postureName(change->to),
                static_cast<double>(change->heightMetres), static_cast<double>(change->heldSeconds));
        scheduleReanchor(roomscale::RecenterKind::Height, "posture change", now, false);
    }
}

void RoomScale::switchSeatedWalk(double now) {
    if (seatedWalkMetres_ <= 0.0f) {
        return;
    }
    const float metres = seatedWalkMetres_;
    seatedWalkMetres_ = 0.0f;
    // Standing from here; the height re-anchor re-references the tracker when the floor can be read.
    // Without a reading, never a seated anchor's height: the tracker could then not notice sitting down.
    postureTracker_.reset(
        posture::Posture::Standing,
        floorNow_.value_or(std::max(anchorAboveFloor_.value_or(0.0f), kStandingHeadMetres)));
    if (++seatedWalks_ <= 20) {
        EVR_LOG("room: seated, but the head was %.2f m from the seat for %.1f s; switching to standing",
                metres, posture::kSeatedWalkSeconds);
    }
    scheduleReanchor(roomscale::RecenterKind::Height, "walking while seated", now, false);
}

} // namespace evr::vkcore
