#include "features/pacing/held_spin.hpp"

#include <algorithm>
#include <cmath>

namespace evr::pacing {

HeldWaitPlan planHeldWait(const HeldWaitInput& in) {
    HeldWaitPlan plan;
    const double timeout =
        std::isfinite(in.step.timeoutSeconds) ? std::max(0.0, in.step.timeoutSeconds) : 0.0;
    const bool spin = in.inPlay && !in.low &&
                      (in.mode == HeldSpin::Always || (in.mode == HeldSpin::OnceBehind && in.fellBehind));
    if (!spin) {
        plan.sleepSeconds = timeout;
        return plan;
    }
    plan.spin = true;
    if (in.mode == HeldSpin::Always || !(in.spinSeconds > 0.0)) {
        plan.spinLimitSeconds = timeout; // a rig test: the whole wait
        return plan;
    }
    if (in.loop.periodSeconds > 0.0 && in.loop.frames < in.step.untilFrame) {
        const double due = in.loop.lastFrameSeconds +
                           static_cast<double>(in.step.untilFrame - in.loop.frames) * in.loop.periodSeconds;
        const double sleep = due - in.spinSeconds - in.nowSeconds;
        plan.sleepSeconds = std::isfinite(sleep) ? std::clamp(sleep, 0.0, timeout) : 0.0;
    }
    plan.spinLimitSeconds = std::min(in.spinSeconds + kHeldSpinOverSeconds, timeout - plan.sleepSeconds);
    return plan;
}

} // namespace evr::pacing
