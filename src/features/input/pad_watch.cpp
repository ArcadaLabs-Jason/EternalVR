#include "features/input/pad_watch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace evr::input {

namespace {

constexpr float kAxisMax = 32767.0f;
constexpr std::uint16_t kBothSticks = pad_button::kLeftThumb | pad_button::kRightThumb;

} // namespace

float stickDeflection(int x, int y, int deadzone) {
    const float zone = std::clamp(static_cast<float>(deadzone), 0.0f, kAxisMax - 1.0f);
    const float length = std::min(std::hypot(static_cast<float>(x), static_cast<float>(y)), kAxisMax);
    if (length <= zone) {
        return 0.0f;
    }
    return (length - zone) / (kAxisMax - zone);
}

void PadWatch::update(unsigned user, const std::optional<PadState>& pad, double seconds) {
    if (user >= kPadUsers || !std::isfinite(seconds)) {
        return;
    }
    Slot& slot = slots_[user];
    slot.connected = pad.has_value();
    slot.pad = pad.value_or(PadState{});
    slot.seconds = seconds;
}

bool PadWatch::fresh(const Slot& slot, double seconds) {
    return slot.connected && slot.seconds >= 0.0 && seconds - slot.seconds <= kPadStaleSeconds;
}

bool PadWatch::bothSticks(double seconds) const {
    return std::any_of(slots_.begin(), slots_.end(), [&](const Slot& slot) {
        return fresh(slot, seconds) && (slot.pad.buttons & kBothSticks) == kBothSticks;
    });
}

PadMotion PadWatch::motion(double seconds) const {
    PadMotion largest;
    for (const Slot& slot : slots_) {
        if (!fresh(slot, seconds)) {
            continue;
        }
        largest.move =
            std::max(largest.move, stickDeflection(slot.pad.thumbLX, slot.pad.thumbLY, kLeftStickDeadzone));
        largest.turn = std::max(largest.turn, stickDeflection(slot.pad.thumbRX, 0, kRightStickDeadzone));
    }
    return largest;
}

} // namespace evr::input
