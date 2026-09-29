#include "stereo_seq/alternate_prev.hpp"

namespace evr::stereo_seq {

PrevPlan planPrevious(const PrevHold& held, std::uint64_t render) {
    if (held.valid && held.render == render) {
        // The first commit in this render already left the matrix of render - 2; the hold stays the one
        // it took.
        return PrevPlan{PrevAction::Keep, false};
    }
    if (held.valid && held.render + 1 == render) {
        // Committed in the render just before: the matrix that commit replaced is the end of render - 2.
        return PrevPlan{PrevAction::Restore, true};
    }
    // Not committed since render - 2 (or never, or out of order): the current matrix is that render's.
    return PrevPlan{PrevAction::Engine, true};
}

} // namespace evr::stereo_seq
