#include "features/menu/enter_tick.hpp"

namespace evr::menu {

bool EnterTick::update(std::optional<bool> hit, double seconds) {
    if (!hit) {
        return false;
    }
    if (!*hit) {
        onPanel_ = false;
        return false;
    }
    const bool tick = !onPanel_ && (!lastOn_ || seconds - *lastOn_ >= offSeconds_);
    onPanel_ = true;
    lastOn_ = seconds;
    return tick;
}

void EnterTick::reset() {
    onPanel_ = false;
    lastOn_.reset();
}

} // namespace evr::menu
