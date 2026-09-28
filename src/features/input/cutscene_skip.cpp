#include "features/input/cutscene_skip.hpp"

namespace evr::input {

CutsceneSkipOutput CutsceneSkip::update(bool cutscene, bool dash) {
    CutsceneSkipOutput out;
    if (cutscene != wasCutscene_) {
        wasCutscene_ = cutscene;
        armed_ = false;
        heldThisCutscene_ = false;
    }
    if (!cutscene) {
        keyDown_ = false;
        return out;
    }
    if (!dash) {
        armed_ = true;
    }
    keyDown_ = armed_ && dash;
    out.keyDown = keyDown_;
    if (keyDown_ && !heldThisCutscene_) {
        heldThisCutscene_ = true;
        out.firstHold = true;
    }
    return out;
}

} // namespace evr::input
