#include "stereo_seq/ssdo_history.hpp"

namespace evr::stereo_seq {

void SsdoHistory::reset(const SsdoPair& engine, const SsdoPair& eyeR, void* unfiltered) {
    eyes_ = {};
    eyes_[0].pair = engine;
    eyes_[1].pair = eyeR;
    unfiltered_ = unfiltered;
    ready_ = engine.target0 && engine.target1 && eyeR.target0 && eyeR.target1 && unfiltered &&
             engine.target0 != engine.target1 && eyeR.target0 != eyeR.target1 &&
             eyeR.target0 != engine.target0 && eyeR.target0 != engine.target1 &&
             eyeR.target1 != engine.target0 && eyeR.target1 != engine.target1;
}

void SsdoHistory::invalidate(SsdoRestart why) {
    for (PerEye& p : eyes_) {
        p.lastRender.reset();
        p.pending = why;
    }
}

SsdoPlan SsdoHistory::beforeRender(Eye eye, std::uint32_t counter) {
    SsdoPlan plan;
    const int e = eye == Eye::Right ? 1 : 0;
    PerEye& p = eyes_[e];
    const PerEye& right = eyes_[1];
    if (!p.lastRender) {
        plan.restart = p.pending != SsdoRestart::None ? p.pending : SsdoRestart::First;
    } else if (const std::uint32_t since = counter - *p.lastRender; since != 1u && since != 2u) {
        plan.restart = SsdoRestart::Gap;
    } else if (eye == Eye::Mono && since == 1u && p.lastEye == Eye::Left) {
        // Eye L's tick partner without its tag: most likely eye R's picture, which must not take eye L's
        // history. Eye L's next render then finds eye R missing and starts over too.
        plan.restart = SsdoRestart::Untagged;
    } else if (eye == Eye::Left && (!right.lastRender || counter - *right.lastRender != 1u)) {
        plan.restart = SsdoRestart::EyeRMissed;
    }
    const int read = p.written < 0 ? 0 : p.written;
    const int write = read ^ 1;
    const unsigned parity = counter & 1u;
    void* const targets[2] = {p.pair.target0, p.pair.target1};
    plan.targets[parity] = targets[write];
    plan.targets[parity ^ 1u] = targets[read];
    plan.targets[2] = unfiltered_;
    plan.key = e * 4 + write * 2 + static_cast<int>(parity);
    p.written = write;
    p.lastRender = counter;
    p.lastEye = eye;
    p.pending = SsdoRestart::None;
    return plan;
}

const char* ssdoRestartName(SsdoRestart restart) {
    switch (restart) {
    case SsdoRestart::None:
        return "none";
    case SsdoRestart::First:
        return "first render";
    case SsdoRestart::Resize:
        return "after a resize";
    case SsdoRestart::Gap:
        return "after a gap";
    case SsdoRestart::EyeRMissed:
        return "eye R missed a tick";
    case SsdoRestart::Untagged:
        return "an untagged render after eye L";
    case SsdoRestart::Resumed:
        return "after dynamic resolution";
    }
    return "?";
}

const char* ssdoNotReady(const SsdoReadiness& r) {
    if (!r.requested) {
        return "it is off (ETERNALVR_STEREO_SSDO_TAA=0)";
    }
    if (!r.installed) {
        return "its hooks are not installed";
    }
    if (!r.gameTouch) {
        return "the multiplayer guard does not allow it";
    }
    if (r.failedClosed) {
        return "it failed closed";
    }
    if (!r.targetsMade) {
        return "eye R's targets were not made (the device context hook did not run or did not succeed)";
    }
    if (r.dynamicResolution) {
        return "dynamic resolution is on (rs_enable is not 0)";
    }
    return nullptr;
}

} // namespace evr::stereo_seq
