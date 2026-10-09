#include "vkcore/glory_view.hpp"

#include "vkcore/controllers.hpp"
#include "vkcore/debug_commands.hpp"
#include "vkcore/log.hpp"
#include "xr_math/head_aim.hpp"

#include <algorithm>
#include <cmath>

namespace evr::vkcore {

namespace {

// After a kill shown steady, how long head aim keeps turning the game's aim back to the steady heading.
constexpr double kRestoreSeconds = 0.5;
// Episodes logged at their start and end.
constexpr unsigned long long kLoggedEpisodes = 30;
// Kills ended at the episode's maximum duration (comfort::GloryTiming::maxSeconds) that are logged.
constexpr unsigned kLoggedTimeouts = 5;

} // namespace

GloryKills::GloryKills(comfort::GloryView view, double testStart, double testDuration) : view_(view) {
    if (testStart >= 0.0 && testDuration > 0.0) {
        testStart_ = testStart;
        testEnd_ = testStart + testDuration;
        EVR_LOG("glory: test: a glory kill from %.1f s to %.1f s on the schedule's clock", testStart_,
                testEnd_);
    }
}

comfort::GloryEpisode::Step
GloryKills::frame(const std::byte* player, bool forcedView, bool menu, double seconds) {
    bool sync = controllers::syncKillActive(player);
    if (testStart_ >= 0.0) {
        const double inMap = secondsInMap();
        sync = sync || (inMap >= testStart_ && inMap < testEnd_);
    }
    // On the flat screen the view is the game's own, so the kill ends with the sync flag: the gate, updated
    // only for head-tracked views, would hold a stale value.
    const comfort::GloryEpisode::Step step =
        episode_.update(sync, forcedView && view_ != comfort::GloryView::Screen, seconds, menu);
    if (step.started) {
        latched_ = view_ == comfort::GloryView::Steady ? lastBody_ : std::nullopt;
        killStartBody_ = lastBody_; // the turn is measured from the body before the kill
        killTurn_ = 0.0f;
        restoreUntil_ = -1.0;
        loggedRestore_ = false;
        if (episode_.episodes() <= kLoggedEpisodes) {
            if (latched_) {
                EVR_LOG("glory: kill %llu starts (%s: the view keeps the body yaw %.1f)", episode_.episodes(),
                        comfort::gloryViewName(view_), *latched_);
            } else {
                EVR_LOG("glory: kill %llu starts (%s)", episode_.episodes(), comfort::gloryViewName(view_));
            }
        }
    }
    if (step.ended) {
        if (latched_) {
            restoreUntil_ = seconds + kRestoreSeconds;
        }
        if (step.timedOut) {
            if (++timeouts_ <= kLoggedTimeouts) {
                EVR_LOG(
                    "glory: kill %llu still running after %.0f s; taken as ended (%s), the next starts once "
                    "the game ends it",
                    episode_.episodes(), episode_.timing().maxSeconds, comfort::gloryViewName(view_));
            }
        } else if (episode_.episodes() <= kLoggedEpisodes) {
            EVR_LOG("glory: kill %llu ends; the view's body turned up to %.1f deg during it",
                    episode_.episodes(), killTurn_);
        }
    }
    if (!episode_.active() && latched_ && seconds >= restoreUntil_) {
        latched_.reset();
    }
    flat_.store(onScreen(), std::memory_order_release);
    return step;
}

std::optional<float> GloryKills::steadyYaw() const {
    return episode_.active() ? latched_ : std::nullopt;
}

std::optional<float> GloryKills::restoreYaw(double seconds) const {
    return !episode_.active() && seconds < restoreUntil_ ? latched_ : std::nullopt;
}

void GloryKills::noteRestored(float degrees) {
    if (!loggedRestore_ && episode_.episodes() <= kLoggedEpisodes) {
        loggedRestore_ = true;
        EVR_LOG("glory: the aim turned back %.1f deg to the steady heading after kill %llu", degrees,
                episode_.episodes());
    }
}

void GloryKills::noteBody(float yaw) {
    if (!episode_.active()) {
        lastBody_ = yaw;
        return;
    }
    if (!killStartBody_) {
        killStartBody_ = yaw;
    }
    killTurn_ = std::max(killTurn_, std::fabs(xr_math::normalize180(yaw - *killStartBody_)));
}

} // namespace evr::vkcore
