#include "xr_math/cutscene_cuts.hpp"

#include <cmath>

namespace evr::xr_math {

CutStep cutRebaseStep(CutRebaseState& state, const CutFrame& frame) {
    CutStep step;
    step.bodyYaw = frame.bodyYaw;
    if (state.lastSeconds) {
        step.gapSeconds = frame.seconds - *state.lastSeconds;
    }
    state.lastSeconds = frame.seconds;
    if (!frame.cutscene) {
        if (state.inCutscene) {
            step.event = CutEvent::End;
            step.turn = normalize180(-state.offset);
        }
        state = {};
        state.lastSeconds = frame.seconds;
        return step;
    }
    if (state.lastCameraYaw) {
        step.cameraTurn = normalize180(frame.cameraYaw - *state.lastCameraYaw);
    }
    const bool steep = std::fabs(frame.cameraPitch) > kCutPitchDegrees ||
                       std::fabs(state.lastCameraPitch) > kCutPitchDegrees;
    const bool start = !state.inCutscene;
    const bool gap = !start && step.gapSeconds > kCutGapSeconds;
    const bool cut = !start && !gap && !steep && std::fabs(step.cameraTurn) > kCutYawDegrees;
    state.lastCameraYaw = frame.cameraYaw;
    state.lastCameraPitch = frame.cameraPitch;
    if (frame.menu) {
        // Nothing is detected under a menu: the offset stays (the cutscene starts when the menu goes).
        step.bodyYaw = normalize180(frame.bodyYaw + state.offset);
        return step;
    }
    state.inCutscene = true;
    if (start || gap || cut) {
        const float offset = normalize180(frame.cameraYaw - frame.headYaw - frame.bodyYaw);
        step.event = start ? CutEvent::Start : gap ? CutEvent::Gap : CutEvent::Cut;
        step.turn = normalize180(offset - state.offset);
        state.offset = offset;
    }
    step.bodyYaw = normalize180(frame.bodyYaw + state.offset);
    return step;
}

float rebaseHeadYaw(HeadAimState& state, float headYaw) {
    const float held = state.injected ? state.injectedYaw : 0.0f;
    state.injected = true;
    state.injectedYaw = headYaw;
    state.writtenCount = 0;
    state.hasRestored = false;
    state.restoredIsOurs = false;
    return normalize180(held - headYaw);
}

const char* cutEventName(CutEvent event) {
    switch (event) {
    case CutEvent::None:
        return "none";
    case CutEvent::Start:
        return "start";
    case CutEvent::Cut:
        return "cut";
    case CutEvent::Gap:
        return "gap";
    case CutEvent::End:
        return "end";
    }
    return "?";
}

} // namespace evr::xr_math
