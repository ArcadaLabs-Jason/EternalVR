#include "stereo_seq/adaptive_eyes.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

namespace evr::stereo_seq {

namespace {

std::string lowerTrimmed(std::string_view value) {
    std::string out;
    for (const char c : value) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

// A whole string as a finite number, or nullopt.
std::optional<double> number(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size() || !std::isfinite(v)) {
        return std::nullopt;
    }
    return v;
}

} // namespace

AlternateMode alternateMode(std::string_view value) {
    const std::string v = lowerTrimmed(value);
    if (v == "auto") {
        return AlternateMode::Auto;
    }
    if (v == "1" || v == "on" || v == "true") {
        return AlternateMode::On;
    }
    return AlternateMode::Off;
}

const char* alternateModeName(AlternateMode mode) {
    switch (mode) {
    case AlternateMode::Auto:
        return "auto";
    case AlternateMode::On:
        return "on";
    case AlternateMode::Off:
        break;
    }
    return "off";
}

AdaptiveEyes::AdaptiveEyes(AdaptiveConfig config)
    : c_(config), hz_(config.defaultHz), share_(config.shareStart), offAfter_(config.offAfter) {}

void AdaptiveEyes::setDisplayPeriod(double seconds) {
    if (seconds >= 1.0 / 1000.0 && seconds <= 1.0 / 20.0) {
        hz_ = 1.0 / seconds;
    }
}

void AdaptiveEyes::resetWindow() {
    winSeconds_ = 0.0;
    winRight_ = 0.0;
    winTicks_ = 0;
}

std::optional<AdaptiveEyes::Switch> AdaptiveEyes::onTick(const TickSample& tick) {
    if (!(tick.seconds > 0.0) || !std::isfinite(tick.seconds)) {
        return std::nullopt;
    }
    now_ += tick.seconds;
    if (!tick.stereo) {
        // A menu, a loading screen, a drain: nothing to measure, and the condition starts again after it.
        stats_.monoSeconds += tick.seconds;
        resetWindow();
        streak_ = 0.0;
        return std::nullopt;
    }
    const int way = tick.pairedTick ? 0 : 1;
    stats_.seconds[way] += tick.seconds;
    ++stats_.ticks[way];
    if (tick.pairedTick != pairs_) {
        // The last tick of the way before a switch (the switch applies at the next pair), or a Route S tick
        // whose eye R did not render: not the way being measured.
        resetWindow();
        return std::nullopt;
    }
    winSeconds_ += tick.seconds;
    winRight_ += tick.pairedTick ? std::clamp(tick.rightSeconds, 0.0, tick.seconds) : 0.0;
    ++winTicks_;
    if (winSeconds_ < c_.window) {
        return std::nullopt;
    }
    const double window = winSeconds_;
    const double rate = static_cast<double>(winTicks_) / window;
    std::optional<Switch> out;
    if (pairs_) {
        // Eye R's share of a Route S tick: what an alternating tick lacks.
        const double rest = window - winRight_;
        if (winRight_ > 0.0 && rest > 0.0) {
            const double measured = std::clamp(winRight_ / rest, c_.shareMin, c_.shareMax);
            share_ += c_.shareWeight * (measured - share_);
        }
        lastRate_ = rate;
        lastEstimate_ = rate;
        streak_ = rate < hz_ * c_.onBelow ? streak_ + window : 0.0;
        if (streak_ >= c_.onAfter) {
            // Back to alternation soon after pairing again: wait longer before the next try.
            offAfter_ =
                now_ - lastPaired_ < c_.flapWithin ? std::min(offAfter_ * 2.0, c_.offAfterMax) : c_.offAfter;
            pairs_ = false;
            out = Switch{true, rate, rate, hz_, streak_, share_};
        }
    } else {
        const double estimate = rate / (1.0 + share_);
        lastRate_ = rate;
        lastEstimate_ = estimate;
        streak_ = estimate > hz_ * c_.offAbove ? streak_ + window : 0.0;
        if (streak_ >= offAfter_) {
            pairs_ = true;
            lastPaired_ = now_;
            out = Switch{false, rate, estimate, hz_, streak_, share_};
        }
    }
    if (out) {
        ++stats_.switches[out->toAlternate ? 1 : 0];
        streak_ = 0.0;
    }
    resetWindow();
    return out;
}

std::optional<CpuLoadSpec> parseCpuLoad(std::string_view value) {
    const std::string v = lowerTrimmed(value);
    std::string parts[3];
    int count = 0;
    for (const char c : v) {
        if (c == ',') {
            if (++count > 2) {
                return std::nullopt;
            }
            continue;
        }
        parts[count].push_back(c);
    }
    if (count == 1) {
        return std::nullopt; // on without off
    }
    const std::optional<double> ms = number(parts[0]);
    if (!ms || *ms <= 0.0 || *ms > 100.0) {
        return std::nullopt;
    }
    CpuLoadSpec spec;
    spec.ms = *ms;
    if (count == 2) {
        const std::optional<double> on = number(parts[1]);
        const std::optional<double> off = number(parts[2]);
        if (!on || !off || *on <= 0.0 || *off < 0.0) {
            return std::nullopt;
        }
        spec.onSeconds = *on;
        spec.offSeconds = *off;
    }
    return spec;
}

double cpuLoadMsAt(const CpuLoadSpec& spec, double seconds) {
    if (spec.onSeconds <= 0.0 || spec.offSeconds <= 0.0) {
        return spec.ms;
    }
    const double period = spec.onSeconds + spec.offSeconds;
    const double phase = std::fmod(std::max(seconds, 0.0), period);
    return phase < spec.onSeconds ? spec.ms : 0.0;
}

} // namespace evr::stereo_seq
