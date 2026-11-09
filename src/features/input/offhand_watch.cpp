// The free off hand's 10 s line (offhand_watch.hpp).

#include "features/input/offhand_watch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace evr::input {

void OffhandWatch::noteTick(const OffhandWatchTick& tick) {
    ++ticks_;
    const auto r = static_cast<std::size_t>(tick.reason);
    if (r < reasons_.size()) {
        ++reasons_[r];
    }
    minWeight_ = std::min(minWeight_, tick.weight);
    maxWeight_ = std::max(maxWeight_, tick.weight);
    if (!tick.offValid) {
        ++untrackedTicks_;
        drops_ += lastValid_ ? 1U : 0U;
    }
    lastValid_ = tick.offValid;
}

void OffhandWatch::noteRefused(std::uint64_t total) {
    if (!refusedBase_) {
        refusedBase_ = total;
    }
    refusedNow_ = total;
}

void OffhandWatch::noteFrame(const OffhandWatchFrame& frame) {
    offMetres_.push_back(length(frame.drawn - frame.meant));
    maxGapMs_ = std::max(maxGapMs_, std::fabs(frame.snapshotGapMs));
    if (last_ && length(frame.meant - last_->meant) < kStillMetres) {
        maxStep_ = std::max(maxStep_, length(frame.drawn - last_->drawn));
        ++steps_;
    }
    last_ = frame;
}

void OffhandWatch::noteBreak() {
    last_.reset();
}

std::string OffhandWatch::line() const {
    char text[640];
    const std::uint64_t controller = reasons_[static_cast<std::size_t>(ArmReason::Controller)];
    std::string why;
    for (std::size_t r = 0; r < reasons_.size(); ++r) {
        if (reasons_[r] == 0 || r == static_cast<std::size_t>(ArmReason::Controller)) {
            continue;
        }
        std::snprintf(text, sizeof(text), "%s%s %llu", why.empty() ? "" : ", ",
                      armReasonName(static_cast<ArmReason>(r)), static_cast<unsigned long long>(reasons_[r]));
        why += text;
    }
    std::snprintf(
        text, sizeof(text),
        "%llu tick(s), the controller's %llu, the game's %llu%s%s%s; weight %.2f to %.2f; %llu refused; "
        "the off hand not tracked in %llu tick(s) (lost %llu time(s))",
        static_cast<unsigned long long>(ticks_), static_cast<unsigned long long>(controller),
        static_cast<unsigned long long>(ticks_ - controller), why.empty() ? "" : " (", why.c_str(),
        why.empty() ? "" : ")", ticks_ ? static_cast<double>(minWeight_) : 0.0,
        static_cast<double>(maxWeight_),
        static_cast<unsigned long long>(refusedBase_ ? refusedNow_ - *refusedBase_ : 0),
        static_cast<unsigned long long>(untrackedTicks_), static_cast<unsigned long long>(drops_));
    std::string out = text;
    if (offMetres_.empty()) {
        out += "; the glove not measured (no posed frame)";
        return out;
    }
    std::vector<float> sorted = offMetres_;
    std::sort(sorted.begin(), sorted.end());
    std::snprintf(
        text, sizeof(text),
        "; the glove drawn from where it was meant: median %.1f mm, at most %.1f mm (%zu frame(s)); its "
        "largest step in a frame with the controller still %.1f mm (%llu step(s)); posed from hands at "
        "most %.2f ms apart from its placement's",
        static_cast<double>(sorted[sorted.size() / 2]) * 1000.0, static_cast<double>(sorted.back()) * 1000.0,
        sorted.size(), static_cast<double>(maxStep_) * 1000.0, static_cast<unsigned long long>(steps_),
        maxGapMs_);
    out += text;
    return out;
}

void OffhandWatch::reset() {
    const std::optional<OffhandWatchFrame> last = last_;
    const bool lastValid = lastValid_;
    const std::uint64_t refused = refusedNow_;
    const bool hadRefused = refusedBase_.has_value();
    *this = OffhandWatch{};
    last_ = last;
    lastValid_ = lastValid;
    if (hadRefused) {
        refusedBase_ = refused;
        refusedNow_ = refused;
    }
}

} // namespace evr::input
