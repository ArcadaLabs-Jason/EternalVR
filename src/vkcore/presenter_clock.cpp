// The runtime's frame clock (features/pacing/frame_clock_watch.hpp, public issue #19): a stall ends the
// session to start a new one, as often as the restart budget allows (features/pacing/restart_budget.hpp).

#include "vkcore/presenter_impl.hpp"

#include <cstdio>

namespace evr::vkcore {

void XrPresenter::Impl::watchFrameClock(const XrFrameState& state) {
    using pacing::RestartBudget;
    const double now = qpcSeconds(qpcNow());
    const bool shown = state.shouldRender != XR_FALSE;
    const auto stall = clock.watch.onFrame(state.predictedDisplayTime, state.predictedDisplayPeriod, now,
                                           gamePresents.load(std::memory_order_relaxed), shown);
    if (loss.lost) {
        return;
    }
    if (!stall) {
        if (shown && clock.budget.onHealthyFrame(now)) {
            clock.loggedKept = false;
            EVR_LOG("xr: frame clock watch: %.0f min without a stall gave a session restart back; %u of %u "
                    "available",
                    RestartBudget::kEarnSeconds / 60.0, clock.budget.available(),
                    RestartBudget::kMaxRestarts);
        }
        return;
    }
    char what[160];
    if (stall->stuck) {
        std::snprintf(what, sizeof(what), "the runtime's display time has not moved on for %d frames",
                      pacing::FrameClockWatch::kStuckFrames);
    } else {
        std::snprintf(
            what, sizeof(what), "the runtime gave %.1f frame(s)/s for %.0f s while the game presented %.1f/s",
            stall->framesPerSecond, pacing::FrameClockWatch::kSlowSeconds, stall->presentsPerSecond);
    }
    const RestartBudget::Verdict verdict = clock.budget.onStall(now);
    if (verdict != RestartBudget::Verdict::Restart) {
        if (!clock.loggedKept) {
            clock.loggedKept = true;
            if (verdict == RestartBudget::Verdict::NoneLeft) {
                EVR_LOG(
                    "xr: %s; the session was started again %llu time(s) already and no restart is left (one "
                    "comes back after %.0f min without a stall), so it is left as it is",
                    what, static_cast<unsigned long long>(clock.budget.taken()),
                    RestartBudget::kEarnSeconds / 60.0);
            } else {
                EVR_LOG(
                    "xr: %s; the session was started again %zu times in the last %.0f min, so it is left as "
                    "it is",
                    what, clock.budget.recent(now), RestartBudget::kWindowSeconds / 60.0);
            }
        }
        return;
    }
    // No call failed and the runtime does not end the session itself (public issue #19): the session is taken
    // as lost, and the worker starts a new one as after a headset that went away (presenter_reconnect.cpp).
    clock.loggedKept = false;
    EVR_LOG("xr: %s; ending the session to start a new one (restart %llu; %u more available, one more back "
            "after each %.0f min without a stall)",
            what, static_cast<unsigned long long>(clock.budget.taken()), clock.budget.available(),
            RestartBudget::kEarnSeconds / 60.0);
    loseOnRuntimeFailure(XR_ERROR_SESSION_LOST, "frame clock watch");
}

} // namespace evr::vkcore
