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
    for (const PaceMode mode : {PaceMode::Off, PaceMode::Headset}) {
        if (equalsNoCase(t, paceModeName(mode))) {
            return mode;
        }
    }
    return std::nullopt;
}

const char* paceModeName(PaceMode mode) {
    return mode == PaceMode::Headset ? "headset" : "off";
}

PaceStep FramePacer::afterHandOver(const HeadsetLoop& loop, double nowSeconds) {
    ++counters_.handOvers;
    if (mode_ == PaceMode::Off) {
        return {};
    }
    const double period = loop.periodSeconds;
    const bool known = loop.frames > 0 && std::isfinite(period) && period > 0.0 &&
                       std::isfinite(loop.lastFrameSeconds) && std::isfinite(nowSeconds);
    const double idleAt = known ? loop.lastFrameSeconds + kIdlePeriods * period : 0.0;
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

void Cadence::shown(std::uint64_t seq, double lateSeconds) {
    if (seq == 0 || seq == lastSeq_ || !std::isfinite(lateSeconds)) {
        return;
    }
    lastSeq_ = seq;
    ++counters_.shownViews;
    counters_.lateSeconds += lateSeconds;
}

} // namespace evr::pacing
