#include "xr_math/cinema_run.hpp"

namespace evr::xr_math {

CinemaPairStep CinemaRun::pair(bool cutByte, double nowSeconds) {
    CinemaPairStep step;
    step.afterGap = active_ && nowSeconds - last_ > kCinemaRunGapSeconds;
    step.started = !active_ || step.afterGap;
    step.cut = cutByte && !step.started;
    active_ = true;
    last_ = nowSeconds;
    if (step.started || cutByte) {
        resetLeft_ = kCinemaResetFrames;
    }
    step.historyReset = resetLeft_ > 0;
    if (resetLeft_ > 0) {
        --resetLeft_;
    }
    return step;
}

bool CinemaRun::end() {
    if (!active_) {
        return false;
    }
    active_ = false;
    resetLeft_ = kCinemaResetFrames;
    return true;
}

bool CinemaRun::headTracked(bool inCutscene, bool& ended) {
    ended = active_ && !inCutscene && end();
    if (!active_ && resetLeft_ > 0) {
        --resetLeft_;
        return true;
    }
    return false;
}

float CinemaNearConfirm::step(float nearestMetres, bool immediate) {
    const bool near = nearestMetres > 0.0f && nearestMetres < kCinemaConfirmNearMetres;
    const bool confirmed = !near || immediate || previousNear_;
    previousNear_ = near;
    return confirmed ? nearestMetres : 0.0f;
}

CinemaLayerPlan cinemaLayerPlan(bool pair, bool guiFresh, double secondsSinceGui) {
    CinemaLayerPlan plan;
    const bool held = guiFresh || (secondsSinceGui >= 0.0 && secondsSinceGui <= kCinemaGuiHoldSeconds);
    if (!pair || !held) {
        // No GUI image, or none for the hold: the game composites its GUI into eye L's image only, so the
        // frame goes on the one mono quad (eye L's half) with the GUI in it.
        return plan;
    }
    plan.eyes = true;
    plan.gui = guiFresh;
    plan.skipComposite = true;
    return plan;
}

} // namespace evr::xr_math
