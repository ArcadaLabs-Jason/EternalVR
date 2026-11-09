#include "features/pacing/pace_policy.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>

namespace evr::pacing {

namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

bool equalsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<PaceMode> parsePaceMode(std::string_view text) {
    const std::string_view t = trim(text);
    for (const PaceMode mode : {PaceMode::Off, PaceMode::Headset, PaceMode::NewFrames}) {
        if (equalsNoCase(t, paceModeName(mode))) {
            return mode;
        }
    }
    return std::nullopt;
}

const char* paceModeName(PaceMode mode) {
    switch (mode) {
    case PaceMode::Headset:
        return "headset";
    case PaceMode::NewFrames:
        return "new";
    case PaceMode::Off:
        break;
    }
    return "off";
}

double newFrameWaitSeconds(double periodSeconds) {
    const double period = std::isfinite(periodSeconds) && periodSeconds > 0.0 ? periodSeconds : 1.0 / 90.0;
    return std::min(kNewFrameWaitPeriods * period, kNewFrameMaxWaitSeconds);
}

PaceStep FramePacer::afterHandOver(const HeadsetLoop& loop, double nowSeconds, bool heldLow) {
    ++counters_.handOvers;
    if (mode_ != PaceMode::Headset) { // NewFrames holds the headset's frame instead (XR worker)
        return {};
    }
    if (heldLow) {
        ++counters_.unseen;
        released_ = loop.frames;
        return {};
    }
    const double period = loop.periodSeconds;
    const bool known = loop.frames > 0 && std::isfinite(period) && period > 0.0 &&
                       std::isfinite(loop.lastFrameSeconds) && std::isfinite(nowSeconds);
    const double idleAt =
        known ? loop.lastFrameSeconds + std::min(kIdlePeriods * period, kMaxIdleSeconds) : 0.0;
    if (!known || nowSeconds >= idleAt) {
        // Not shown, lost, stopping or not started: nothing to keep in step with.
        ++counters_.idle;
        released_ = loop.frames;
        return {};
    }
    if (loop.frames != released_) {
        // The headset began a frame while the game made this one: the game is not ahead, it goes on at once.
        ++counters_.frameBegun;
        released_ = loop.frames;
        return {};
    }
    PaceStep step;
    step.wait = true;
    step.untilFrame = released_ + 1;
    step.timeoutSeconds = std::min({kTimeoutPeriods * period, kMaxTimeoutSeconds, idleAt - nowSeconds});
    return step;
}

void FramePacer::waited(std::uint64_t framesNow, double waitedSeconds, bool begun) {
    if (begun) {
        ++counters_.waits;
    } else {
        ++counters_.timeouts;
    }
    if (std::isfinite(waitedSeconds) && waitedSeconds > 0.0) {
        counters_.waitSeconds += waitedSeconds;
        counters_.longestWaitSeconds = std::max(counters_.longestWaitSeconds, waitedSeconds);
    }
    released_ = framesNow;
}

double FramePacer::takeLongestWait() {
    const double longest = counters_.longestWaitSeconds;
    counters_.longestWaitSeconds = 0.0;
    return longest;
}

double UnfocusedCap::interval(double periodSeconds) {
    if (!std::isfinite(periodSeconds) || periodSeconds <= 0.0) {
        return kPeriodsPerImage * kDefaultPeriodSeconds;
    }
    return kPeriodsPerImage * std::clamp(periodSeconds, kMinPeriodSeconds, kMaxPeriodSeconds);
}

double UnfocusedCap::afterHandOver(double periodSeconds, double nowSeconds) {
    return afterHandOverEvery(interval(periodSeconds), nowSeconds);
}

void UnfocusedCap::releasedAt(double nowSeconds) {
    if (std::isfinite(nowSeconds)) {
        release_ = nowSeconds;
    }
}

double UnfocusedCap::afterHandOverEvery(double intervalSeconds, double nowSeconds) {
    if (!std::isfinite(nowSeconds) || !std::isfinite(intervalSeconds) || intervalSeconds <= 0.0) {
        return 0.0;
    }
    const double next = release_ ? *release_ + intervalSeconds : nowSeconds;
    if (nowSeconds >= next) {
        release_ = nowSeconds;
        return 0.0;
    }
    release_ = next;
    return next - nowSeconds;
}

void Cadence::frame(std::uint64_t handOvers) {
    if (!lastHandOvers_) {
        lastHandOvers_ = handOvers; // the first frame only sets the baseline
        return;
    }
    const std::uint64_t n = handOvers >= *lastHandOvers_ ? handOvers - *lastHandOvers_ : 0;
    lastHandOvers_ = handOvers;
    ++counters_.frames;
    counters_.handOvers += n;
    if (n == 0) {
        ++counters_.none;
    } else if (n == 1) {
        ++counters_.one;
    } else {
        ++counters_.several;
        counters_.notShown += n - 1;
    }
}

void HoldSwitch::noteGameTime(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0 || !held_) {
        return;
    }
    ++gameTimes_;
    if (lastPeriod_ > 0.0 && seconds > lastPeriod_) {
        ++slowGameTimes_;
    }
}

bool HoldSwitch::frame(double nowSeconds, std::uint64_t handOvers, double periodSeconds, bool inPlay) {
    if (!std::isfinite(nowSeconds)) {
        return false;
    }
    const bool periodKnown = std::isfinite(periodSeconds) && periodSeconds > 0.0;
    if (periodKnown) {
        lastPeriod_ = periodSeconds;
    }
    const bool newImage = !lastHandOvers_ || handOvers > *lastHandOvers_;
    lastHandOvers_ = handOvers;
    const auto restart = [&] {
        windowStart_ = nowSeconds;
        windowHandOvers_ = handOvers;
        frames_ = 0;
        repeats_ = 0;
        gameTimes_ = 0;
        slowGameTimes_ = 0;
    };
    const auto change = [&](Change why) {
        held_ = !held_;
        ++switches_;
        changedAt_ = nowSeconds;
        lastChange_ = why;
        badWindows_ = 0;
    };
    if (!windowStart_) {
        restart();
        changedAt_ = nowSeconds;
        return false;
    }
    if (!inPlay) {
        // A menu or a loading screen: held, nothing decided. Not a hold the back-off judges.
        const bool changed = !held_;
        if (changed) {
            change(Change::LeftPlay);
            holdTried_ = false;
        }
        badWindows_ = 0;
        restart();
        return changed;
    }
    ++frames_;
    if (!newImage) {
        ++repeats_;
    }
    const double elapsed = nowSeconds - *windowStart_;
    if (elapsed < kWindowSeconds) {
        return false;
    }
    bool changed = false;
    if (periodKnown && elapsed <= kMaxWindowSeconds && frames_ >= kMinWindowFrames) {
        lastRepeatShare_ = static_cast<double>(repeats_) / static_cast<double>(frames_);
        lastRate_ = static_cast<double>(handOvers - windowHandOvers_) / elapsed;
        if (held_) {
            bool bad = static_cast<double>(repeats_) > kLetGoRepeatShare * static_cast<double>(frames_);
            if (bad && latencyGate_ && gameTimes_ > 0 && 2 * slowGameTimes_ <= gameTimes_) {
                bad = false; // the game draws its images within the period: the repeats were hitches
            }
            badWindows_ = bad ? badWindows_ + 1 : 0;
            if (badWindows_ >= kBadWindowsToLetGo) {
                if (holdTried_) {
                    const bool lasted = nowSeconds - changedAt_ >= kLastingHoldSeconds;
                    backOff_ = lasted ? kWindowSeconds : std::min(2.0 * backOff_, kMaxBackOffSeconds);
                }
                change(Change::FellBehind);
                changed = true;
            }
        } else if (nowSeconds - changedAt_ >= backOff_ && lastRate_ >= kHoldRateShare / periodSeconds) {
            change(Change::KeptUp);
            holdTried_ = true;
            changed = true;
        }
    } else {
        badWindows_ = 0; // a gap or too few frames: no decision, and no run of bad windows across it
    }
    restart();
    return changed;
}

void HoldSwitch::pause(double nowSeconds, std::uint64_t handOvers) {
    if (!std::isfinite(nowSeconds)) {
        return;
    }
    if (!windowStart_) {
        changedAt_ = nowSeconds; // as frame()'s first frame
    }
    lastHandOvers_ = handOvers;
    windowStart_ = nowSeconds;
    windowHandOvers_ = handOvers;
    frames_ = 0;
    repeats_ = 0;
    gameTimes_ = 0;
    slowGameTimes_ = 0;
    badWindows_ = 0;
}

void Cadence::shown(std::uint64_t seq, double lateSeconds) {
    if (seq == 0 || seq == lastSeq_ || !std::isfinite(lateSeconds)) {
        return;
    }
    lastSeq_ = seq;
    ++counters_.shownViews;
    counters_.lateSeconds += lateSeconds;
}

} // namespace evr::pacing
