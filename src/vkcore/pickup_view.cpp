#include "vkcore/pickup_view.hpp"

#include "vkcore/controllers.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/presenter_types.hpp"
#include "xr_math/camera_anim.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>

namespace evr::vkcore {

namespace {

// Pickups logged at their start and end.
constexpr unsigned long long kLoggedEpisodes = 30;

} // namespace

PickupView::PickupView(comfort::PickupCamera camera) : camera_(camera) {}

void PickupView::frame(const std::byte* player,
                       bool cutscene,
                       bool menu,
                       const xr_math::IdViewAxis& gameAxis,
                       const Vec3& eye,
                       float unitsPerMetre) {
    const bool pickup = !cutscene && controllers::syncPickupActive(player);
    const double seconds = qpcSeconds(qpcNow());
    if (camera_ != comfort::PickupCamera::Play) {
        stillFrame(player, pickup, gameAxis, seconds);
        return;
    }
    const bool forced = controllers::forcedView() && !cutscene; // the gate as the last game view left it
    const comfort::GloryEpisode::Step step = episode_.update(pickup, forced, seconds, menu);
    endSet_.step(step.ended, forced, seconds);
    comfort::PickupHeading::Frame f;
    f.started = step.started;
    f.ended = step.ended;
    f.seconds = seconds;
    f.eye = eye;
    f.unitsPerMetre = unitsPerMetre;
    f.mapLoads = mp_guard::mapLoads();
    f.dead = controllers::playerDead(player);
    f.menu = menu;
    heading_.step(f);
    if (step.started) {
        frames_ = 0;
        loggedRestore_ = false;
        if (episode_.episodes() <= kLoggedEpisodes) {
            char held[48] = "no heading held (not head aim)";
            if (const std::optional<float> yaw = heading_.held()) {
                std::snprintf(held, sizeof(held), "the view keeps the body yaw %.1f", *yaw);
            }
            EVR_LOG("pickup: camera turn %llu starts (a pickup's animation): its motion goes on top of the "
                    "head; %s",
                    episode_.episodes(), held);
        }
    }
    turn_ = follow_.update(episode_.active(), gameAxis, seconds);
    if (episode_.active()) {
        ++frames_;
    }
    if (step.ended && episode_.episodes() <= kLoggedEpisodes) {
        const xr_math::IdAngles& most = follow_.largest();
        EVR_LOG(
            "pickup: camera turn %llu ends after %llu game frame(s)%s; largest turn played pitch %.1f, yaw "
            "%.1f, roll %.1f deg, %u cut(s) left out; %.1f deg of pitch left eases out",
            episode_.episodes(), frames_, step.timedOut ? " (still running at the time limit)" : "",
            most.pitch, most.yaw, most.roll, follow_.cuts(), turn_ ? turn_->pitch : 0.0f);
        using Dropped = comfort::PickupHeading::Dropped;
        if (const Dropped why = heading_.dropped(); why != Dropped::None) {
            EVR_LOG("pickup: camera turn %llu: the view takes the game's heading after it (%s; moved %.1f m)",
                    episode_.episodes(),
                    why == Dropped::Moved     ? "the player was moved away"
                    : why == Dropped::MapLoad ? "a map loaded"
                                              : "the player died",
                    heading_.movedMetres());
        }
    }
}

void PickupView::stillFrame(const std::byte* player,
                            bool pickup,
                            const xr_math::IdViewAxis& gameAxis,
                            double seconds) {
    // The player's view angles, outside pickups too (the hold keeps them for a pickup's first view), only
    // from the idPlayer.
    std::optional<float> anglesYaw;
    float view[3];
    if (controllers::isPlayerSafe(player) &&
        controllers::safeCopy(view, player + kPlayerViewAngles, sizeof(view))) {
        anglesYaw = view[1];
    }
    heldTurn_ = hold_.update(xr_math::anglesFromAxis(gameAxis).yaw, anglesYaw, seconds, pickup);
    if (!std::isfinite(heldTurn_)) {
        heldTurn_ = 0.0f;
    }
    if (hold_.started() && ++holds_ <= kLoggedEpisodes) {
        EVR_LOG("pickup: a pickup's camera jumped %.1f deg in one game frame; the view's follow keeps its "
                "heading "
                "until it comes back (two game views at most)",
                -heldTurn_);
    }
    if (hold_.timedOut()) { // once per pickup
        EVR_LOG(
            "pickup: a pickup's sync has run %.0f s; its camera's jumps are followed as they come until it "
            "ends",
            xr_math::CameraHeadingHold::kMaxPickupSeconds);
    }
}

xr_math::IdViewAxis PickupView::followed(const xr_math::IdViewAxis& gameAxis) const {
    return heldTurn_ == 0.0f ? gameAxis : xr_math::turnYaw(gameAxis, heldTurn_);
}

bool PickupView::endSetView(bool forcedNow, double seconds) const {
    return camera_ == comfort::PickupCamera::Play && endSet_.covers(forcedNow, seconds);
}

void PickupView::noteRestored(float degrees) {
    if (!loggedRestore_ && episode_.episodes() <= kLoggedEpisodes) {
        loggedRestore_ = true;
        EVR_LOG("pickup: the aim turned back %.1f deg to the heading the view kept through camera turn %llu",
                degrees, episode_.episodes());
    }
}

std::optional<float>
keptBodyYaw(const GloryKills& glory, const PickupView& pickup, bool headAimed, double seconds) {
    // The pickup's first, also as the game turns the player's angles to the camera's at its end (a kill and a
    // pickup are not expected to overlap); then a kill shown steady's; then the restore heading on the game's
    // forced view of the set that ends a pickup without a forced view of its own (the Sentinel Crystal's),
    // which would otherwise turn the body to the game's yaw for its few frames before head aim turns the aim
    // back (turnBackYaw).
    comfort::KeptHeading k;
    k.pickupHeld = pickup.heldYaw();
    k.gloryHeld = glory.steadyYaw();
    k.pickupRestore = pickup.restoreYaw(seconds);
    k.headAimed = headAimed;
    k.endSetView = pickup.endSetView(controllers::forcedView(), seconds);
    return comfort::keptHeading(k);
}

std::optional<float> turnBackYaw(const GloryKills& glory, const PickupView& pickup, double seconds) {
    const std::optional<float> steady = glory.restoreYaw(seconds);
    return steady ? steady : pickup.restoreYaw(seconds);
}

void noteTurnedBack(GloryKills& glory, PickupView& pickup, double seconds, float degrees) {
    if (glory.restoreYaw(seconds)) {
        glory.noteRestored(degrees);
    } else {
        pickup.noteRestored(degrees);
    }
}

std::optional<Quat> headWithGameCamera(const PickupView& pickup,
                                       const camera_anim::Frame& anim,
                                       const xr_math::IdViewAxis& body,
                                       Quat headInIdTech,
                                       const xr_math::IdViewAxis& gameAxis,
                                       bool headAimed,
                                       bool forcedView,
                                       const Vec3& eye) {
    std::optional<Quat> head;
    if (pickup.turn()) {
        // The camera's whole motion, the hands animation's part in it included.
        head =
            xr_math::headWithCameraAnim(body, headInIdTech, xr_math::turnOnBody(*pickup.turn(), headAimed));
    } else if (anim.active) {
        head = xr_math::headWithCameraAnim(body, headInIdTech, anim.applied);
    }
    if (camera_anim::tracing()) {
        camera_anim::traceView(forcedView, gameAxis,
                               xr_math::composeHeadAxis(body, head.value_or(headInIdTech)), eye);
    }
    return head;
}

} // namespace evr::vkcore
