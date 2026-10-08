#include "features/posture/seated_walk.hpp"

#include <cmath>

namespace evr::posture {

bool SeatedWalk::update(bool seatedBlocked, float fromSeatMetres, double seconds) {
    if (!seatedBlocked || !std::isfinite(fromSeatMetres) || fromSeatMetres <= kSeatedWalkMetres ||
        !std::isfinite(seconds)) {
        since_ = -1.0;
        return false;
    }
    if (since_ < 0.0 || seconds < since_) {
        since_ = seconds; // the first far frame, or time went backwards
    }
    if (seconds - since_ < kSeatedWalkSeconds) {
        return false;
    }
    since_ = -1.0;
    return true;
}

} // namespace evr::posture
