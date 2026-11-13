#include "ui_layer/gui_hold.hpp"

namespace evr::ui_layer {

bool guiImageShown(double ageSeconds, bool withShownImage) {
    return ageSeconds <= (withShownImage ? kGuiStallHoldSeconds : kGuiFreshSeconds);
}

std::optional<GuiGapTally::Gap>
GuiGapTally::onFrame(bool newImage, double secondsSinceImage, double guiAgeSeconds, bool guiShown) {
    if (newImage) {
        std::optional<Gap> ended;
        if (armed_ && gap_.shown + gap_.left > 0) {
            ended = gap_;
            ended->gapMs = secondsSinceImage * 1000.0;
        }
        armed_ = guiShown;
        gap_ = Gap{};
        return ended;
    }
    if (!armed_ || guiAgeSeconds <= kGuiFreshSeconds) {
        return std::nullopt;
    }
    if (guiShown) {
        ++gap_.shown;
        gap_.lastShownAgeMs = guiAgeSeconds * 1000.0;
    } else {
        ++gap_.left;
    }
    return std::nullopt;
}

} // namespace evr::ui_layer
