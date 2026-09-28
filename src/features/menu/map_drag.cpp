#include "features/menu/map_drag.hpp"

#include <algorithm>
#include <cmath>

namespace evr::menu {

namespace {

constexpr double kMaxStepSeconds = 0.1;

struct Want {
    std::optional<DragButton> button;
    float vx = 0.0f; // pixels per second
    float vy = 0.0f;
};

Want wanted(const MapDragInput& in, const MapDragTuning& t) {
    Want w;
    const float height = static_cast<float>(in.height);
    const input::Axis2 pan = withDeadzone(in.pan, t.deadzone);
    if (pan.x != 0.0f || pan.y != 0.0f) {
        // The map follows the stick, as the owner expected on the Quest 3 (the first build had both axes the
        // other way): pushed right, the drag and the map move right; pushed away, they move up the screen.
        w.button = DragButton::Left;
        w.vx = pan.x * t.panSpeed * height;
        w.vy = -pan.y * t.panSpeed * height;
        return w;
    }
    const input::Axis2 turn = withDeadzone(in.turn, t.deadzone);
    if (turn.x != 0.0f && std::fabs(in.turn.x) > std::fabs(in.turn.y)) {
        w.button = DragButton::Right;
        w.vx = turn.x * t.rotateSpeed * height;
    }
    return w;
}

bool finiteTuning(float v) {
    return std::isfinite(v) && v >= 0.0f;
}

} // namespace

input::Axis2 withDeadzone(input::Axis2 stick, float deadzone) {
    if (!input::isFinite(stick)) {
        return {};
    }
    const float m = input::magnitude(stick);
    const float dz = std::clamp(deadzone, 0.0f, 0.95f);
    if (m <= dz) {
        return {};
    }
    const float scaled = std::min(1.0f, (m - dz) / (1.0f - dz));
    return stick * (scaled / m);
}

MapDrag::MapDrag(MapDragTuning tuning) : tuning_(tuning) {
    const MapDragTuning defaults;
    if (!finiteTuning(tuning_.panSpeed)) {
        tuning_.panSpeed = defaults.panSpeed;
    }
    if (!finiteTuning(tuning_.rotateSpeed)) {
        tuning_.rotateSpeed = defaults.rotateSpeed;
    }
    if (!(tuning_.strokeLength > 0.0f && tuning_.strokeLength < 0.5f)) {
        tuning_.strokeLength = defaults.strokeLength;
    }
}

std::optional<DragEvent> MapDrag::reset() {
    std::optional<DragEvent> up;
    if (buttonDown()) {
        up = DragEvent{button_, false};
    }
    phase_ = Phase::Idle;
    last_ = -1.0;
    return up;
}

MapDragOutput MapDrag::update(const MapDragInput& in) {
    MapDragOutput out;
    const double dt = last_ < 0.0 ? 0.0 : std::clamp(in.seconds - last_, 0.0, kMaxStepSeconds);
    last_ = in.seconds;
    const Want want = in.width > 0 && in.height > 0 && in.cursor ? wanted(in, tuning_) : Want{};
    const auto at = [&](CursorPixel p) {
        return in.cursorIdle && in.cursor && *in.cursor == p;
    };

    if (phase_ == Phase::Idle) {
        if (!want.button) {
            return out;
        }
        button_ = *want.button;
        anchor_ = {static_cast<std::int32_t>(in.width / 2), static_cast<std::int32_t>(in.height / 2)};
        phase_ = Phase::Seeking;
        phaseStart_ = in.seconds;
    }
    switch (phase_) {
    case Phase::Idle:
        break;
    case Phase::Seeking:
        if (!want.button) {
            phase_ = Phase::Idle;
            return out;
        }
        button_ = *want.button;
        target_ = anchor_;
        if (at(anchor_) || in.seconds - phaseStart_ > tuning_.settleTimeout) {
            out.event = DragEvent{button_, true};
            phase_ = Phase::Dragging;
            downAt_ = in.seconds;
            x_ = static_cast<float>(anchor_.x);
            y_ = static_cast<float>(anchor_.y);
        }
        break;
    case Phase::Dragging: {
        if (want.button != button_) {
            phase_ = Phase::Releasing;
            phaseStart_ = in.seconds;
            break;
        }
        x_ += want.vx * static_cast<float>(dt);
        y_ += want.vy * static_cast<float>(dt);
        const float limit = tuning_.strokeLength * static_cast<float>(std::min(in.width, in.height));
        const float dx = x_ - static_cast<float>(anchor_.x);
        const float dy = y_ - static_cast<float>(anchor_.y);
        const float d = std::hypot(dx, dy);
        if (d >= limit) {
            x_ = static_cast<float>(anchor_.x) + dx * (limit / d);
            y_ = static_cast<float>(anchor_.y) + dy * (limit / d);
            phase_ = Phase::Releasing;
            phaseStart_ = in.seconds;
        }
        target_ = {std::clamp(static_cast<std::int32_t>(std::lround(x_)), 0,
                              static_cast<std::int32_t>(in.width) - 1),
                   std::clamp(static_cast<std::int32_t>(std::lround(y_)), 0,
                              static_cast<std::int32_t>(in.height) - 1)};
        break;
    }
    case Phase::Releasing:
        if ((at(target_) || in.seconds - phaseStart_ > tuning_.settleTimeout) &&
            in.seconds - downAt_ >= tuning_.minHold) {
            out.event = DragEvent{button_, false};
            if (want.button) {
                button_ = *want.button;
                phase_ = Phase::Seeking;
                phaseStart_ = in.seconds;
            } else {
                phase_ = Phase::Idle;
            }
        }
        break;
    }
    // The cursor stays where the stroke left it until the button is up (the frame of the release
    // included); after that it goes to the next stroke's start, or back to the ray.
    if (phase_ != Phase::Idle || out.event) {
        out.target = target_;
    }
    return out;
}

} // namespace evr::menu
