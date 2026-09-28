#include "stereo_seq/object_prev.hpp"

namespace evr::stereo_seq {

std::uint32_t ObjectPrevSlot::counterFor(Eye eye, std::uint32_t counter) {
    if (eye != Eye::Right) {
        return counter;
    }
    if (!haveRight_ || counter != right_) {
        // A new eye R render: its previous frame is its own render two back only if that one was eye R's
        // (eye L's render in between, nothing else).
        twoBack_ = haveRight_ && counter - right_ == 2u;
        right_ = counter;
        haveRight_ = true;
    }
    // (counter + 2 + 2) % 3 == (counter - 2) % 3: the engine adds 2 for the previous frame itself.
    return twoBack_ ? counter + 2u : counter;
}

} // namespace evr::stereo_seq
