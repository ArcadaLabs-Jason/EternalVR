#include "features/input/wheel_mouse.hpp"

#include "common/finite.hpp"

#include <cmath>
#include <numbers>

namespace evr::input {

namespace {

constexpr float kDegrees = std::numbers::pi_v<float> / 180.0f;

WheelMouseSettings sanitized(const WheelMouseSettings& s) {
    const bool usable =
        finiteInRange(s.openDelaySeconds, 0.0f, 2.0f) && finiteInRange(s.selectThreshold, 0.05f, 1.0f) &&
        finiteInRange(s.reachPixels, 1.0f, 4096.0f) && finiteInRange(s.turnDegrees, 0.0f, 45.0f) &&
        finiteInRange(s.pinPixels, 0.0f, 4096.0f) && finiteInRange(s.pinSeconds, 0.0f, 10.0f);
    return usable ? s : WheelMouseSettings{};
}

// The offset clamped to `radius` around the centre, as the game clamps the cursor.
Axis2 clamped(Axis2 offset, float radius) {
    const float length = magnitude(offset);
    if (length <= radius || length <= 0.0f) {
        return offset;
    }
    return offset * (radius / length);
}

} // namespace

const char* wheelDirectionName(WheelDirection direction) {
    switch (direction) {
    case WheelDirection::Right:
        return "right";
    case WheelDirection::UpRight:
        return "up-right";
    case WheelDirection::Up:
        return "up";
    case WheelDirection::UpLeft:
        return "up-left";
    case WheelDirection::Left:
        return "left";
    case WheelDirection::DownLeft:
        return "down-left";
    case WheelDirection::Down:
        return "down";
    case WheelDirection::DownRight:
        return "down-right";
    case WheelDirection::None:
        break;
    }
    return "none";
}

WheelDirection wheelDirection(Axis2 stick, float threshold) {
    if (!isFinite(stick) || magnitude(stick) < threshold || magnitude(stick) <= 0.0f) {
        return WheelDirection::None;
    }
    const float eighths = std::atan2(stick.y, stick.x) / (45.0f * kDegrees);
    const int octant = static_cast<int>(std::lround(eighths));
    return static_cast<WheelDirection>(((octant % 8) + 8) % 8);
}

WheelMouse::WheelMouse(WheelMouseSettings settings) : settings_(sanitized(settings)) {}

void WheelMouse::reset() {
    held_ = false;
    open_ = false;
    heldSeconds_ = 0.0f;
    sincePush_ = 0.0f;
    aimed_ = false;
    aim_ = {};
    offset_ = {};
    pointed_ = WheelDirection::None;
    moves_ = 0;
}

WheelMouseOutput WheelMouse::update(bool held, Axis2 pointer, float dtSeconds) {
    const float dt = std::isfinite(dtSeconds) && dtSeconds > 0.0f ? dtSeconds : 0.0f;
    WheelMouseOutput out;
    if (!held) {
        if (open_) {
            out.released = true;
            out.moves = moves_;
        }
        reset();
        return out;
    }
    if (!held_) {
        held_ = true;
        heldSeconds_ = 0.0f;
    } else {
        heldSeconds_ += dt;
    }
    if (!open_) {
        if (heldSeconds_ < settings_.openDelaySeconds) {
            return out;
        }
        // The game centres its cursor on the wheel when it opens it: the model starts there.
        open_ = true;
        out.opened = true;
        offset_ = {};
        aimed_ = false;
        sincePush_ = 0.0f;
    } else {
        sincePush_ += dt;
    }
    out.moves = moves_;

    const WheelDirection direction = wheelDirection(pointer, settings_.selectThreshold);
    if (direction == WheelDirection::None) {
        return out; // near the centre: the highlight stays
    }
    const float length = magnitude(pointer);
    const Axis2 unit{pointer.x / length, -pointer.y / length}; // screen axes: y down
    const bool turned =
        !aimed_ || unit.x * aim_.x + unit.y * aim_.y < std::cos(settings_.turnDegrees * kDegrees);
    Axis2 delta;
    if (turned) {
        // From the modelled offset to the rim in the new direction.
        const Axis2 target = unit * settings_.reachPixels;
        delta = {target.x - offset_.x, target.y - offset_.y};
    } else if (sincePush_ >= settings_.pinSeconds && settings_.pinPixels > 0.0f) {
        // Held: a push outward, absorbed by the game's clamp, keeps the cursor on the rim.
        delta = unit * settings_.pinPixels;
    } else {
        return out;
    }
    const int dx = static_cast<int>(std::lround(delta.x));
    const int dy = static_cast<int>(std::lround(delta.y));
    aim_ = unit;
    aimed_ = true;
    if (dx == 0 && dy == 0) {
        return out;
    }
    offset_ = clamped({offset_.x + static_cast<float>(dx), offset_.y + static_cast<float>(dy)},
                      settings_.reachPixels);
    sincePush_ = 0.0f;
    ++moves_;
    out.move = true;
    out.dx = dx;
    out.dy = dy;
    out.pointed = direction;
    out.directionChanged = direction != pointed_;
    out.moves = moves_;
    pointed_ = direction;
    return out;
}

} // namespace evr::input
