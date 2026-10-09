#include "features/pacing/restart_budget.hpp"

#include <cmath>
#include <cstddef>

namespace evr::pacing {

bool RestartBudget::onHealthyFrame(double seconds) {
    if (!std::isfinite(seconds)) {
        return false;
    }
    const double gap = lastFrame_ < 0.0 ? 0.0 : seconds - lastFrame_;
    lastFrame_ = seconds;
    if (gap <= 0.0 || gap > kMaxGapSeconds) {
        return false;
    }
    if (available_ >= kMaxRestarts) {
        healthy_ = 0.0; // nothing to earn: the time starts when a restart is used
        return false;
    }
    healthy_ += gap;
    if (healthy_ < kEarnSeconds) {
        return false;
    }
    healthy_ = 0.0;
    ++available_;
    return true;
}

std::size_t RestartBudget::recent(double seconds) const {
    std::size_t n = 0;
    for (std::size_t i = 0; i < times_.size() && i < taken_; ++i) {
        if (seconds - times_[i] < kWindowSeconds) {
            ++n;
        }
    }
    return n;
}

RestartBudget::Verdict RestartBudget::onStall(double seconds) {
    healthy_ = 0.0;
    lastFrame_ = -1.0;
    if (available_ == 0) {
        return Verdict::NoneLeft;
    }
    if (recent(seconds) >= kMaxPerWindow) {
        return Verdict::RateLimited;
    }
    --available_;
    times_[taken_ % times_.size()] = seconds;
    ++taken_;
    return Verdict::Restart;
}

} // namespace evr::pacing
