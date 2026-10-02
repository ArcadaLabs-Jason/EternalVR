#include "features/pacing/display_period_watch.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace evr::pacing {

namespace {

// A period longer than this is no display period (a runtime's placeholder or garbage).
constexpr double kMaxPeriodMs = 1000.0;

std::string format(const char* pattern, double a, double b = 0.0, double c = 0.0) {
    char text[160];
    std::snprintf(text, sizeof(text), pattern, a, b, c);
    return text;
}

} // namespace

bool samePeriod(double a, double b) {
    return std::abs(a - b) <= DisplayPeriodWatch::kSameTolerance * std::max(a, b);
}

int multipleOf(double periodMs, double referenceMs) {
    if (!(periodMs > 0.0) || !(referenceMs > 0.0)) {
        return 0;
    }
    const double ratio = periodMs / referenceMs;
    const double k = std::round(ratio);
    if (k < 1.0 || std::abs(ratio - k) > kMultipleTolerance || k > 1e6) {
        return 0;
    }
    return static_cast<int>(k);
}

double hertz(double periodMs) {
    return periodMs > 0.0 ? 1000.0 / periodMs : 0.0;
}

double summaryBase(double measuredBaseMs, double runtimeMs) {
    if (!(runtimeMs > 0.0)) {
        return measuredBaseMs;
    }
    if (!(measuredBaseMs > 0.0)) {
        return runtimeMs;
    }
    return runtimeMs < measuredBaseMs && multipleOf(measuredBaseMs, runtimeMs) >= 2 ? runtimeMs
                                                                                    : measuredBaseMs;
}

std::optional<PeriodChange> DisplayPeriodWatch::onFrame(double periodMs, double nowSeconds) {
    if (lastFrame_ >= 0.0 && settled_ > 0.0) {
        const double gap = nowSeconds - lastFrame_;
        if (gap > 0.0 && gap <= kMaxFrameGapSeconds) {
            addTime(gap);
        }
    }
    lastFrame_ = nowSeconds;
    if (!std::isfinite(periodMs) || periodMs <= 0.0 || periodMs > kMaxPeriodMs) {
        return std::nullopt;
    }
    if (settled_ > 0.0 && samePeriod(periodMs, settled_)) {
        candidateFrames_ = 0; // an odd frame or a few, then back: no change
        return std::nullopt;
    }
    if (candidateFrames_ > 0 && samePeriod(periodMs, candidate_)) {
        ++candidateFrames_;
        candidateSum_ += periodMs;
    } else {
        candidate_ = periodMs;
        candidateSum_ = periodMs;
        candidateStart_ = nowSeconds;
        candidateFrames_ = 1;
    }
    if (candidateFrames_ < kHoldFrames) {
        return std::nullopt;
    }
    PeriodChange change;
    change.fromMs = settled_;
    change.toMs = candidateSum_ / static_cast<double>(candidateFrames_);
    change.atSeconds = candidateStart_;
    change.referenceMs = referenceMs();
    settled_ = change.toMs;
    runSeconds_ = 0.0;
    candidateFrames_ = 0;
    if (change.fromMs > 0.0) {
        ++changes_;
    }
    if (shortest_ <= 0.0 || settled_ < shortest_) {
        shortest_ = settled_;
    }
    return change;
}

double DisplayPeriodWatch::referenceMs() const {
    return base_ > 0.0 ? base_ : shortest_;
}

void DisplayPeriodWatch::addTime(double seconds) {
    auto it = std::find_if(times_.begin(), times_.end(),
                           [this](const PeriodTime& t) { return samePeriod(t.periodMs, settled_); });
    if (it == times_.end() && times_.size() >= kMaxPeriods) {
        it = std::min_element(times_.begin(), times_.end(), [this](const PeriodTime& a, const PeriodTime& b) {
            return std::abs(a.periodMs - settled_) < std::abs(b.periodMs - settled_);
        });
    }
    if (it == times_.end()) {
        times_.push_back({settled_, seconds});
    } else {
        it->seconds += seconds;
    }
    runSeconds_ += seconds;
    // The base only ever gets shorter: a period that held long enough and is shorter than the base.
    if (runSeconds_ >= kBaseHoldSeconds &&
        (base_ <= 0.0 || (settled_ < base_ && !samePeriod(settled_, base_)))) {
        base_ = settled_;
    }
}

std::string changeText(const PeriodChange& change, double runtimeMs) {
    const bool runtime = runtimeMs > 0.0;
    const double reference = runtime ? runtimeMs : change.referenceMs;
    const double hz = hertz(change.toMs);
    std::string text =
        change.fromMs > 0.0
            ? format("display period %.2f -> %.2f ms at %.1f s", change.fromMs, change.toMs, change.atSeconds)
            : format("display period %.2f ms at %.1f s", change.toMs, change.atSeconds);
    const int k = multipleOf(change.toMs, reference);
    if (!(reference > 0.0)) {
        text += format(" (%.0f Hz)", hz);
    } else if (k == 1) {
        if (change.fromMs <= 0.0) {
            text += runtime ? format(" (%.0f Hz, the headset's refresh rate)", hz) : format(" (%.0f Hz)", hz);
        } else if (multipleOf(change.fromMs, reference) >= 2) {
            text += runtime ? format(" (back to the headset's refresh rate: %.0f Hz)", hz)
                            : format(" (back to the base: %.0f Hz)", hz);
        } else {
            text += format(" (a refresh change to %.0f Hz)", hz);
        }
    } else if (k >= 2) {
        text += runtime ? format(" (%.0fx the headset's %.2f ms at %.0f Hz: the runtime is throttling or "
                                 "reprojecting)",
                                 k, reference, hertz(reference))
                        : format(" (%.0fx the base %.2f ms: the runtime is throttling or reprojecting)", k,
                                 reference);
    } else if (change.fromMs <= 0.0 && runtime) {
        text += format(" (%.0f Hz; the runtime reports %.1f Hz)", hz, hertz(reference));
    } else if (change.toMs < reference && !runtime) {
        text += format(" (shorter than any steady period before: %.0f Hz)", hz);
    } else {
        text += format(" (a refresh change to %.0f Hz)", hz);
    }
    return text;
}

double RefreshShares::throttled() const {
    double sum = 0.0;
    for (std::size_t i = 1; i < multiples.size(); ++i) {
        sum += multiples[i];
    }
    return sum;
}

RefreshShares shares(const std::vector<PeriodTime>& times, double baseMs) {
    RefreshShares s;
    s.baseMs = baseMs;
    for (const PeriodTime& t : times) {
        s.totalSeconds += t.seconds;
    }
    if (!(s.totalSeconds > 0.0) || !(baseMs > 0.0)) {
        return s;
    }
    s.multiples.assign(1, 0.0);
    for (const PeriodTime& t : times) {
        const double share = t.seconds / s.totalSeconds;
        const int k = multipleOf(t.periodMs, baseMs);
        if (k >= 1 && k <= kMaxMultiple) {
            if (s.multiples.size() < static_cast<std::size_t>(k)) {
                s.multiples.resize(static_cast<std::size_t>(k), 0.0);
            }
            s.multiples[static_cast<std::size_t>(k - 1)] += share;
        } else {
            s.other += share;
            s.others.push_back(t);
        }
    }
    std::stable_sort(s.others.begin(), s.others.end(),
                     [](const PeriodTime& a, const PeriodTime& b) { return a.seconds > b.seconds; });
    return s;
}

std::string summaryText(const RefreshShares& s, std::uint64_t changes) {
    if (!(s.totalSeconds > 0.0) || !(s.baseMs > 0.0)) {
        return {};
    }
    std::string text = format("base %.2f ms (%.0f Hz); 1x %.1f%%", s.baseMs, hertz(s.baseMs),
                              s.multiples.empty() ? 0.0 : s.multiples[0] * 100.0);
    for (std::size_t i = 1; i < s.multiples.size(); ++i) {
        if (s.multiples[i] > 0.0) {
            text += format(", %.0fx %.1f%%", static_cast<double>(i + 1), s.multiples[i] * 100.0);
        }
    }
    if (!s.others.empty()) {
        text += format(", other %.1f%% (", s.other * 100.0);
        constexpr std::size_t kListed = 3;
        for (std::size_t i = 0; i < s.others.size() && i < kListed; ++i) {
            text += format(i == 0 ? "%.0f Hz %.1f%%" : ", %.0f Hz %.1f%%", hertz(s.others[i].periodMs),
                           s.others[i].seconds / s.totalSeconds * 100.0);
        }
        text += s.others.size() > kListed ? ", ...)" : ")";
    }
    text += format("; %.0f change(s) in %.0f s", static_cast<double>(changes), s.totalSeconds);
    return text;
}

std::string refreshHzValue(double baseMs) {
    return baseMs > 0.0 ? format("%.1f", hertz(baseMs)) : std::string();
}

std::string throttledShareValue(const RefreshShares& s) {
    return s.totalSeconds > 0.0 && s.baseMs > 0.0 ? format("%.3f", s.throttled()) : std::string();
}

} // namespace evr::pacing
