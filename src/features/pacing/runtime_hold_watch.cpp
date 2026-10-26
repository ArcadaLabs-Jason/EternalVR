#include "features/pacing/runtime_hold_watch.hpp"

#include <algorithm>

namespace evr::pacing {

std::optional<RuntimeHold> RuntimeHoldWatch::onWindow(
    double endFrameMs, std::uint64_t endFrameCalls, double periodMs, std::uint64_t frames, double seconds) {
    const bool timed = lastSeconds_ >= 0.0 && seconds > lastSeconds_ && frames >= lastFrames_;
    const Window window{endFrameMs,
                        timed ? static_cast<double>(frames - lastFrames_) / (seconds - lastSeconds_) : 0.0,
                        lastSeconds_ >= 0.0 ? lastSeconds_ : seconds};
    lastFrames_ = frames;
    lastSeconds_ = seconds;
    if (endFrameCalls == 0 || periodMs <= 0.0) {
        return std::nullopt;
    }
    lowestMs_ = lowestMs_ < 0.0 ? endFrameMs : std::min(lowestMs_, endFrameMs);

    const bool held = endFrameMs >= kShareOfPeriod * periodMs;
    if (held == holding_) {
        streak_ = 0;
        return std::nullopt;
    }
    recent_[streak_] = window;
    if (++streak_ < kWindows) {
        return std::nullopt;
    }
    streak_ = 0;
    holding_ = held;

    RuntimeHold hold;
    hold.began = held;
    hold.periodMs = periodMs;
    hold.lowestMs = lowestMs_;
    for (const Window& w : recent_) {
        hold.endFrameMs += w.endFrameMs / kWindows;
        hold.framesPerSecond += w.framesPerSecond / kWindows;
    }
    if (held) {
        heldSince_ = recent_[0].started;
        ++holds_;
    } else {
        hold.heldSeconds = recent_[0].started - heldSince_;
    }
    if (lines_ >= kMaxLines) {
        return std::nullopt;
    }
    ++lines_;
    return hold;
}

} // namespace evr::pacing
