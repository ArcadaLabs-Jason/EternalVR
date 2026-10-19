#include "features/input/view_pose_clock.hpp"

namespace evr::input {

void ViewPoseClock::tick(std::int64_t now) {
    previous_ = last_;
    last_ = now;
    if (ticks_ < 2) {
        ++ticks_;
    }
}

bool ViewPoseClock::current(std::int64_t written,
                            std::int64_t now,
                            std::int64_t stale,
                            std::int64_t cap) const {
    // A stamp after `now` was written on another thread after the reader took the time: as new as it gets.
    const std::int64_t age = now - written;
    if (age > cap) {
        return false;
    }
    return (ticks_ >= 2 && written > previous_) || age <= stale;
}

} // namespace evr::input
