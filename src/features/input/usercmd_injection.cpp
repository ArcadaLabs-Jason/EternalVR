#include "features/input/usercmd_injection.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace evr::input {

namespace {

constexpr float kYawWrapLimit = 3600.0f;

} // namespace

std::uint64_t mergeButtons(std::uint64_t game, std::uint64_t injected, bool suppressed) {
    return suppressed ? game : (game | injected);
}

std::int8_t addMoveAxis(std::int8_t game, int injected) {
    if (injected == 0) {
        return game;
    }
    const int sum = static_cast<int>(game) + std::clamp(injected, -kMaxMoveAxis, kMaxMoveAxis);
    return static_cast<std::int8_t>(std::clamp(sum, -kMaxMoveAxis, kMaxMoveAxis));
}

ActionHold::ActionHold(float minSeconds, int minCommands)
    : minSeconds_(std::isfinite(minSeconds) && minSeconds >= 0.0f ? minSeconds : 0.05f),
      minCommands_(std::max(1, minCommands)) {}

game::GameActionSet ActionHold::update(const game::GameActionSet& down, float dtSeconds) {
    const float dt = std::isfinite(dtSeconds) && dtSeconds > 0.0f ? dtSeconds : 0.0f;
    game::GameActionSet out;
    for (std::size_t i = 0; i < game::kGameActionCount; ++i) {
        Held& h = held_[i];
        if (h.holding) {
            h.age += dt;
        }
        if (down.test(i) && !previous_.test(i)) {
            h = {0.0f, 0, true};
        }
        if (h.holding && !down.test(i) && h.age >= minSeconds_ && h.commands >= minCommands_) {
            h.holding = false;
        }
        const bool active = down.test(i) || h.holding;
        if (active) {
            ++h.commands;
        }
        out.set(i, active);
    }
    previous_ = down;
    return out;
}

void ActionHold::reset() {
    previous_.reset();
    held_.fill({});
}

void ViewDeltaQueue::add(ViewDelta delta) {
    if (std::isfinite(delta.pitch)) {
        pending_.pitch += delta.pitch;
    }
    if (std::isfinite(delta.yaw)) {
        pending_.yaw += delta.yaw;
    }
}

ViewDelta ViewDeltaQueue::drain() {
    const ViewDelta out = pending_;
    pending_ = {};
    return out;
}

float addAccumulatedYaw(float accumulated, float delta) {
    if (!std::isfinite(accumulated) || !std::isfinite(delta)) {
        return accumulated;
    }
    float sum = accumulated + delta;
    if (std::fabs(sum) > kYawWrapLimit) {
        sum = std::fmod(sum, 360.0f);
    }
    return sum;
}

} // namespace evr::input
