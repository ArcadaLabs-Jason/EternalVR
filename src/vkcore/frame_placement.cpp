// The arms posed under their own frame's placement (frame_placement.hpp).

#include "vkcore/frame_placement.hpp"

#include "vkcore/game_frame.hpp"
#include "vkcore/log.hpp"

#include <windows.h>

#include <cstdint>
#include <mutex>

namespace evr::vkcore::controllers::frame_placement {

namespace {

constexpr const char* kTag = "frame placement";
constexpr ULONGLONG kLineMs = 10000;
constexpr int kMaxDifferLines = 20;

std::mutex g_mutex;
arm::FramePin<ViewmodelPlacement> g_pin;
// Under g_mutex.
arm::PinTally g_tally;
ULONGLONG g_lastLine = 0;
int g_differLines = 0;

// Under g_mutex.
void count(arm::PinCheck check) {
    g_tally.note(check);
    const ULONGLONG now = GetTickCount64();
    if (g_lastLine == 0) {
        g_lastLine = now;
    }
    if (now - g_lastLine >= kLineMs) {
        g_lastLine = now;
        EVR_LOG("%s: last 10 s: %s", kTag, g_tally.line().c_str());
        g_tally.reset();
    }
}

} // namespace

TickView forArms(const std::byte* hands, const ArmsTick& arms, TickView view) {
    const std::optional<std::int32_t> frame = game_frame::now();
    std::optional<ViewmodelPlacement> placement;
    if (arm::placesAhead(frame, game_frame::placedIn(hands)) && view.worldCurrent) {
        placement = viewmodelPlacement(arms, view.world);
    }
    std::lock_guard lock(g_mutex);
    if (!placement || !frame) {
        g_pin.clear();
        return view; // placed in this frame already, or the model stays the game's
    }
    g_pin.pin(hands, *frame, *placement);
    view.model.pose = placement->pose;
    view.model.worldQpc = view.world.qpc;
    return view;
}

PinCheck::PinCheck(const std::byte* hands) {
    const std::optional<std::int32_t> frame = game_frame::now();
    std::lock_guard lock(g_mutex);
    pin_ = g_pin.take(hands, frame);
}

PinCheck::~PinCheck() {
    if (pin_ && !checked_) {
        std::lock_guard lock(g_mutex);
        count(arm::PinCheck::NotPlaced);
    }
}

void PinCheck::placed(const arm::PinInputs& inputs, const xr_math::EyeRelativePose& placement) {
    if (!pin_ || checked_) {
        return;
    }
    checked_ = true;
    const arm::PinCheck check = arm::checkPin(pin_->inputs, pin_->pose, inputs, placement);
    std::lock_guard lock(g_mutex);
    count(check);
    if (check != arm::PinCheck::Same && g_differLines < kMaxDifferLines) {
        ++g_differLines;
        const Vec3 d = placement.offset - pin_->pose.offset;
        EVR_LOG(
            "%s: the arms were posed under a placement the model did not get (%s): %.4f game units apart%s",
            kTag, arm::pinCheckName(check), static_cast<double>(length(d)),
            g_differLines == kMaxDifferLines ? "; no more such lines" : "");
    }
}

} // namespace evr::vkcore::controllers::frame_placement
