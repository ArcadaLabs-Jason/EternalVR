#include "features/input/stick_directions.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace evr::input {

namespace {

constexpr float kEighthRadians = std::numbers::pi_v<float> / 4.0f;

constexpr std::array<const char*, kStickDirectionCount> kNames{
    "right", "up_right", "up", "up_left", "left", "down_left", "down", "down_right",
};

// Clockwise from up, as the log lists them.
constexpr std::array<StickDirection, kStickDirectionCount> kClockwiseFromUp{
    StickDirection::Up,   StickDirection::UpRight,  StickDirection::Right, StickDirection::DownRight,
    StickDirection::Down, StickDirection::DownLeft, StickDirection::Left,  StickDirection::UpLeft,
};

constexpr std::size_t indexOf(StickDirection direction) {
    return static_cast<std::size_t>(direction);
}

bool owned(const StickLayout& layout, int eighth) {
    return layout.owners[static_cast<std::size_t>(((eighth % 8) + 8) % 8)] != DirectionOwner::None;
}

// Eighths from `eighth` to the next owned direction, counter-clockwise (`step` 1) or clockwise (-1); 8 when
// there is none other.
int gapTo(const StickLayout& layout, int eighth, int step) {
    for (int k = 1; k < 8; ++k) {
        if (owned(layout, eighth + step * k)) {
            return k;
        }
    }
    return 8;
}

} // namespace

StickDirection gestureDirection(StickGesture gesture) {
    switch (gesture) {
    case StickGesture::Up:
        return StickDirection::Up;
    case StickGesture::Down:
    case StickGesture::DownTap:
    case StickGesture::DownHold:
        return StickDirection::Down;
    case StickGesture::Left:
        return StickDirection::Left;
    case StickGesture::Right:
        return StickDirection::Right;
    case StickGesture::UpLeft:
        return StickDirection::UpLeft;
    case StickGesture::UpRight:
        return StickDirection::UpRight;
    case StickGesture::DownLeft:
        return StickDirection::DownLeft;
    case StickGesture::DownRight:
        return StickDirection::DownRight;
    }
    return StickDirection::Up;
}

bool isSideGesture(StickGesture gesture) {
    const StickDirection direction = gestureDirection(gesture);
    return direction != StickDirection::Up && direction != StickDirection::Down;
}

StickLayout stickLayoutOf(const std::vector<StickGestureBinding>& gestures) {
    StickLayout layout;
    for (const StickGestureBinding& binding : gestures) {
        layout.owners[indexOf(gestureDirection(binding.gesture))] = DirectionOwner::Gesture;
        layout.directions = layout.directions || isSideGesture(binding.gesture);
        layout.downPress = layout.downPress || binding.gesture == StickGesture::Down;
    }
    for (const StickDirection side : {StickDirection::Left, StickDirection::Right}) {
        if (layout.owners[indexOf(side)] == DirectionOwner::None) {
            layout.owners[indexOf(side)] = DirectionOwner::Turn;
        }
    }
    return layout;
}

std::optional<StickDirection> ownedDirectionAt(Axis2 stick, const StickLayout& layout) {
    if (!isFinite(stick) || (stick.x == 0.0f && stick.y == 0.0f)) {
        return std::nullopt;
    }
    const float angle = std::atan2(stick.y, stick.x);
    std::optional<StickDirection> best;
    float bestOffset = 0.0f;
    for (std::size_t i = 0; i < kStickDirectionCount; ++i) {
        if (layout.owners[i] == DirectionOwner::None) {
            continue;
        }
        // The direction's angle from the stick's, -pi to pi: positive when it lies counter-clockwise of it.
        const float offset =
            std::remainder(static_cast<float>(i) * kEighthRadians - angle, 2.0f * std::numbers::pi_v<float>);
        const bool nearer = !best || std::fabs(offset) < std::fabs(bestOffset) ||
                            (std::fabs(offset) == std::fabs(bestOffset) && offset > 0.0f);
        if (nearer) {
            best = static_cast<StickDirection>(i);
            bestOffset = offset;
        }
    }
    return best;
}

float partDegrees(const StickLayout& layout, StickDirection direction) {
    const int eighth = static_cast<int>(direction);
    if (!owned(layout, eighth)) {
        return 0.0f;
    }
    return static_cast<float>(gapTo(layout, eighth, 1) + gapTo(layout, eighth, -1)) * 22.5f;
}

const char* stickDirectionName(StickDirection direction) {
    return kNames[indexOf(direction)];
}

std::string stickLayoutText(const StickLayout& layout) {
    if (!layout.directions) {
        return {};
    }
    std::string text;
    for (const StickDirection direction : kClockwiseFromUp) {
        const DirectionOwner owner = layout.owners[indexOf(direction)];
        if (owner == DirectionOwner::None) {
            continue;
        }
        std::array<char, 16> degrees{};
        std::snprintf(degrees.data(), degrees.size(), "%g",
                      static_cast<double>(partDegrees(layout, direction)));
        text += text.empty() ? "" : ", ";
        text += std::string(stickDirectionName(direction)) + " " + degrees.data() + " deg";
        text += owner == DirectionOwner::Turn ? " (turn)" : "";
    }
    return text;
}

} // namespace evr::input
