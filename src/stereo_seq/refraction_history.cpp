#include "stereo_seq/refraction_history.hpp"

#include <cstddef>

namespace evr::stereo_seq {

void RefractionHistory::reset() {
    eyes_ = {};
}

void RefractionHistory::invalidate(RefractionRestart why) {
    for (PerEye& p : eyes_) {
        p.lastRender.reset();
        p.previous.reset();
        p.updates = 0;
        p.ran = {};
        p.pending = why;
    }
}

RefractionHistory::PerEye& RefractionHistory::render(Eye eye, std::uint32_t counter, RefractionPlan& out) {
    const int e = eye == Eye::Right ? 1 : 0;
    PerEye& p = eyes_[e];
    if (p.lastRender && *p.lastRender == counter) {
        out = p.last;
        out.repeated = true;
        return p;
    }
    RefractionPlan plan;
    plan.eye = e;
    if (!p.lastRender) {
        plan.restart = p.pending != RefractionRestart::None ? p.pending : RefractionRestart::First;
    } else if (const std::uint32_t since = counter - *p.lastRender; since != 1u && since != 2u) {
        plan.restart = RefractionRestart::Gap;
    } else if (eye == Eye::Mono && since == 1u && p.lastEye == Eye::Left) {
        plan.restart = RefractionRestart::Untagged;
    } else if (eye == Eye::Left && p.afterUntagged) {
        plan.restart = RefractionRestart::AfterUntagged;
    }
    if (plan.restart == RefractionRestart::Untagged) {
        p.afterUntagged = true;
    } else if (eye == Eye::Left || plan.restart != RefractionRestart::None) {
        p.afterUntagged = false;
    }
    plan.write = p.written < 0 ? 0 : p.written ^ 1;
    p.written = plan.write;
    p.previous = p.lastRender;
    p.previousUpdates = p.updates;
    p.updates = 0;
    p.decided = {};
    p.lastRender = counter;
    p.lastEye = eye;
    p.pending = RefractionRestart::None;
    p.last = plan;
    out = plan;
    return p;
}

RefractionPlan RefractionHistory::plan(Eye eye, std::uint32_t counter) {
    RefractionPlan out;
    render(eye, counter, out);
    return out;
}

RefractionPlan RefractionHistory::plan(Eye eye, std::uint32_t counter, int update, int updates) {
    RefractionPlan out;
    PerEye& p = render(eye, counter, out);
    if (update < 0 || update >= kRefractionUpdates) {
        out.updateRestart = RefractionRestart::NotInLast;
        return out;
    }
    const auto u = static_cast<std::size_t>(update);
    if (p.decided[u]) {
        out.updateRestart = *p.decided[u];
        out.updateRepeated = true;
        return out;
    }
    if (p.updates == 0) {
        p.updates = updates;
    }
    if (out.restart == RefractionRestart::None) {
        if (p.previousUpdates != 0 && p.updates != p.previousUpdates) {
            out.updateRestart = RefractionRestart::CountChanged;
        } else if (!p.previous || p.ran[u] != p.previous) {
            out.updateRestart = RefractionRestart::NotInLast;
        }
    }
    p.ran[u] = counter;
    p.decided[u] = out.updateRestart;
    return out;
}

const char* refractionRestartName(RefractionRestart restart) {
    switch (restart) {
    case RefractionRestart::None:
        return "none";
    case RefractionRestart::First:
        return "first render";
    case RefractionRestart::Gap:
        return "after a gap";
    case RefractionRestart::Untagged:
        return "an untagged render after eye L";
    case RefractionRestart::AfterUntagged:
        return "eye L after an untagged render";
    case RefractionRestart::Resumed:
        return "after dynamic resolution";
    case RefractionRestart::Resized:
        return "after the render size changed";
    case RefractionRestart::CountChanged:
        return "the eye's update count changed";
    case RefractionRestart::NotInLast:
        return "an update the eye's last render did not run";
    }
    return "?";
}

const char* refractionNotReady(const RefractionReadiness& r) {
    if (!r.installed) {
        return "its hooks are not installed";
    }
    if (!r.gameTouch) {
        return "the multiplayer guard does not allow it";
    }
    if (r.failedClosed) {
        return "it failed closed";
    }
    if (!r.imagesMade) {
        return "eye R's images were not made (the device context hooks did not run or did not succeed)";
    }
    if (r.dynamicResolution) {
        return "dynamic resolution is on (rs_enable is not 0)";
    }
    return nullptr;
}

bool refractionHeldOff(const RefractionReadiness& r, Eye eye) {
    if (eye == Eye::Mono || !r.installed || !r.gameTouch) {
        return false;
    }
    return refractionNotReady(r) != nullptr;
}

} // namespace evr::stereo_seq
