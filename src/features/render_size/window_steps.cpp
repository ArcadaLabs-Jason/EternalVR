#include "features/render_size/window_steps.hpp"

namespace evr::render_size {

WindowStep stepForAnswer(GameSwapchains swapchains) {
    return (swapchains.unscaled || swapchains.unscaledCreating) && !swapchains.scaled
               ? WindowStep::ReadClientArea
               : WindowStep::Move;
}

WindowStep WindowSteps::onAnswer(GameSwapchains swapchains, double seconds) {
    const WindowStep step = stepForAnswer(swapchains);
    waiting_ = step == WindowStep::ReadClientArea;
    since_ = seconds;
    return step;
}

bool WindowSteps::onScaledSwapchain() {
    const bool due = waiting_;
    waiting_ = false;
    return due;
}

bool WindowSteps::overdue(double seconds) {
    if (!waiting_ || seconds - since_ < kMoveWaitSeconds) {
        return false;
    }
    waiting_ = false;
    return true;
}

} // namespace evr::render_size
