#include "features/roomscale/long_press.hpp"

#include <cmath>

namespace evr::roomscale {

LongPress::LongPress(float seconds) : seconds_(std::isfinite(seconds) && seconds > 0.0f ? seconds : 1.0f) {}

bool LongPress::update(bool down, double nowSeconds) {
    if (!down || !std::isfinite(nowSeconds)) {
        down_ = false;
        fired_ = false;
        return false;
    }
    if (!down_ || nowSeconds < since_) {
        down_ = true;
        fired_ = false;
        since_ = nowSeconds;
    }
    if (!fired_ && nowSeconds - since_ >= static_cast<double>(seconds_)) {
        fired_ = true;
        return true;
    }
    return false;
}

} // namespace evr::roomscale
