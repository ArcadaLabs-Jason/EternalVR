#include "vkcore/pickup_view.hpp"

#include "vkcore/controllers.hpp"
#include "vkcore/log.hpp"
#include "vkcore/presenter_types.hpp"
#include "xr_math/camera_anim.hpp"

namespace evr::vkcore {

namespace {

// Pickups logged at their start and end.
constexpr unsigned long long kLoggedEpisodes = 30;

} // namespace

PickupView::PickupView(comfort::PickupCamera camera) : camera_(camera) {}

void PickupView::frame(const std::byte* player,
                       bool cutscene,
                       bool menu,
                       const xr_math::IdViewAxis& gameAxis) {
    if (camera_ != comfort::PickupCamera::Play) {
        return;
    }
    const double seconds = qpcSeconds(qpcNow());
    const bool pickup = !cutscene && controllers::syncPickupActive(player);
    const comfort::GloryEpisode::Step step =
        episode_.update(pickup, controllers::forcedView() && !cutscene, seconds, menu);
    if (step.started) {
        frames_ = 0;
        if (episode_.episodes() <= kLoggedEpisodes) {
            EVR_LOG(
                "pickup: camera turn %llu starts (a pickup's animation): its motion goes on top of the head",
                episode_.episodes());
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
