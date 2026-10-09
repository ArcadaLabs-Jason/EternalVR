#include "features/pacing/frame_clock_watch.hpp"

#include <algorithm>
#include <cmath>

namespace evr::pacing {

std::optional<ClockStall> FrameClockWatch::onFrame(std::int64_t displayTimeNs,
                                                   std::int64_t periodNs,
                                                   double nowSeconds,
                                                   std::uint64_t presents,
                                                   bool shown) {
    if (lastDisplayTime_ && periodNs > 0 && displayTimeNs - *lastDisplayTime_ < periodNs / 2) {
        ++stuckFrames_;
    } else {
        stuckFrames_ = 0;
    }
    lastDisplayTime_ = displayTimeNs;
    if (stuckFrames_ >= kStuckFrames) {
        reset();
        return ClockStall{true, 0.0, 0.0};
    }
    if (!shown || !std::isfinite(nowSeconds)) {
        windowStart_.reset();
        return std::nullopt;
    }
    if (!windowStart_) {
        windowStart_ = nowSeconds;
        windowFrames_ = 0;
        windowPresents_ = presents;
        return std::nullopt;
    }
    ++windowFrames_;
    const double elapsed = nowSeconds - *windowStart_;
    if (elapsed < kSlowSeconds) {
        return std::nullopt;
    }
    const double frames = static_cast<double>(windowFrames_) / elapsed;
    const double gamePresents =
        presents >= windowPresents_ ? static_cast<double>(presents - windowPresents_) / elapsed : 0.0;
    windowStart_ = nowSeconds;
    windowFrames_ = 0;
    windowPresents_ = presents;
    if (frames < kSlowFramesPerSecond && gamePresents >= kSlowFramesPerSecond) {
        reset();
        return ClockStall{false, frames, gamePresents};
    }
    return std::nullopt;
}

void FrameGaps::onFrame(std::int64_t displayTimeNs, std::int64_t periodNs) {
    if (lastDisplayTime_ && displayTimeNs > *lastDisplayTime_) {
        const std::int64_t interval = displayTimeNs - *lastDisplayTime_;
        longestNs_ = std::max(longestNs_, interval);
        if (periodNs > 0 && static_cast<double>(interval) > kGapPeriods * static_cast<double>(periodNs)) {
            ++gaps_;
        }
    }
    lastDisplayTime_ = displayTimeNs;
}

std::uint32_t copyWaitMs(std::int64_t periodNs) {
    const double periodMs = periodNs > 0 ? static_cast<double>(periodNs) / 1e6 : 1000.0 / 72.0;
    const double ms = std::ceil(kCopyWaitPeriods * periodMs);
    return static_cast<std::uint32_t>(
        std::clamp(ms, static_cast<double>(kMinCopyWaitMs), static_cast<double>(kMaxCopyWaitMs)));
}

} // namespace evr::pacing
