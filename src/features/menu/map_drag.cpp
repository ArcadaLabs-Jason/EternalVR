#include "features/menu/map_drag.hpp"

#include <algorithm>
#include <cmath>

namespace evr::menu {

namespace {

constexpr double kMaxStepSeconds = 0.1;

bool finiteTuning(float v) {
    return std::isfinite(v) && v >= 0.0f;
}

// One axis with the deadzone taken out and the rest rescaled to 0..1, keeping its sign.
float axisWithDeadzone(float v, float deadzone) {
    const float a = std::fabs(v);
    if (!std::isfinite(v) || a <= deadzone) {
        return 0.0f;
    }
    return std::copysign(std::min(1.0f, (a - deadzone) / (1.0f - deadzone)), v);
}

MapEvent buttonEvent(DragButton button, bool down) {
    MapEvent e;
    e.kind = MapEvent::Kind::Button;
    e.button = button;
    e.down = down;
    return e;
}

MapEvent keyEvent(std::uint8_t key, bool down) {
    MapEvent e;
    e.kind = MapEvent::Kind::Key;
    e.key = key;
    e.down = down;
    return e;
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
    if (!(tuning_.deadzone >= 0.0f && tuning_.deadzone < 0.95f)) {
        tuning_.deadzone = defaults.deadzone;
    }
    if (!(tuning_.releaseZone >= 0.0f && tuning_.releaseZone <= tuning_.deadzone)) {
        tuning_.releaseZone = std::min(defaults.releaseZone, tuning_.deadzone);
    }
    if (!(tuning_.rotateStart >= tuning_.deadzone && tuning_.rotateStart < 1.0f)) {
        tuning_.rotateStart = std::max(defaults.rotateStart, tuning_.deadzone);
    }
}

void MapDrag::sendKeys(MapKeys keys, MapDragOutput& out) {
    const auto change = [&out](bool was, bool now, std::uint8_t key) {
        if (was != now) {
            out.events.push_back(keyEvent(key, now));
        }
    };
    change(keys_.up, keys.up, kMapKeyUp);
    change(keys_.left, keys.left, kMapKeyLeft);
    change(keys_.down, keys.down, kMapKeyDown);
    change(keys_.right, keys.right, kMapKeyRight);
    keys_ = keys;
}

void MapDrag::release(MapDragOutput& out) {
    if (phase_ == Phase::Down) {
        out.events.push_back(buttonEvent(button_, false));
    }
    phase_ = Phase::Idle;
}

std::vector<MapEvent> MapDrag::reset() {
    MapDragOutput out;
    release(out);
    sendKeys({}, out);
    keyPan_.reset();
    panBy_ = MapPanBy::None;
    rotate_ = false;
    last_ = -1.0;
    return out.events;
}

MapDragOutput MapDrag::update(const MapDragInput& in) {
    MapDragOutput out;
    const double now = in.seconds;
    const double dt = last_ < 0.0 ? 0.0 : std::clamp(now - last_, 0.0, kMaxStepSeconds);
    last_ = now;
    const bool usable = in.width > 0 && in.height > 0 && in.cursor.has_value();

    // What the sticks ask for. A pan or rotation going on ends only once its stick is back near the centre,
    // so one held at the edge of the deadzone does not let go and press again.
    const input::Axis2 rawPan = usable && input::isFinite(in.pan) ? in.pan : input::Axis2{};
    const bool panHeld =
        input::magnitude(rawPan) > (panBy_ != MapPanBy::None ? tuning_.releaseZone : tuning_.deadzone);
    const input::Axis2 pan = panHeld ? withDeadzone(rawPan, tuning_.deadzone) : input::Axis2{};
    const input::Axis2 rawTurn = usable && input::isFinite(in.turn) ? in.turn : input::Axis2{};
    const float turnX = std::fabs(rawTurn.x);
    rotate_ =
        rotate_ ? turnX > tuning_.releaseZone : turnX > tuning_.rotateStart && turnX >= std::fabs(rawTurn.y);
    const float rotation = rotate_ ? axisWithDeadzone(rawTurn.x, tuning_.deadzone) : 0.0f;

    // The pan goes onto the keys while a rotation holds the mouse, and stays there until its stick is let go.
    if (!panHeld) {
        panBy_ = MapPanBy::None;
    } else if (panBy_ == MapPanBy::None) {
        panBy_ = rotate_ ? MapPanBy::Keys : MapPanBy::Drag;
    } else if (panBy_ == MapPanBy::Drag && rotate_) {
        panBy_ = MapPanBy::Keys;
    }
    std::optional<DragButton> want;
    if (rotate_) {
        want = DragButton::Right;
    } else if (panBy_ == MapPanBy::Drag) {
        want = DragButton::Left;
    }

    // A button no longer wanted goes up (after its minimum hold; meanwhile nothing moves).
    if (phase_ == Phase::Down && want != button_ && now - downAt_ >= tuning_.minHold) {
        release(out);
    }
    sendKeys(panBy_ == MapPanBy::Keys ? keyPan_.update(pan) : MapKeys{}, out);
    if (panBy_ != MapPanBy::Keys) {
        keyPan_.reset();
    }

    // A wanted button: the cursor to the middle with the buttons up, then the press.
    if (phase_ == Phase::Seeking && !want) {
        phase_ = Phase::Idle;
    }
    if (phase_ == Phase::Idle && want) {
        phase_ = Phase::Seeking;
        seekStart_ = now;
    }
    if (phase_ == Phase::Seeking) {
        button_ = *want;
        const CursorPixel middle{static_cast<std::int32_t>(in.width / 2),
                                 static_cast<std::int32_t>(in.height / 2)};
        out.target = middle;
        const bool there = in.cursorIdle && in.cursor && *in.cursor == middle;
        if (there || now - seekStart_ > tuning_.settleTimeout) {
            // From the press on, only the stick's motion moves the cursor: a move to the middle now would
            // pan.
            out.target.reset();
            out.events.push_back(buttonEvent(button_, true));
            phase_ = Phase::Down;
            downAt_ = now;
            restX_ = 0.0f;
            restY_ = 0.0f;
        }
        return out;
    }

    // The button is down: the stick's motion, every frame, for as long as the stick is held.
    if (phase_ == Phase::Down && want == button_) {
        float vx = 0.0f;
        float vy = 0.0f;
        if (button_ == DragButton::Left) {
            // The map follows the stick, as the owner expected on the Quest 3: pushed right, the drag and the
            // map move right; pushed away, they move up the screen.
            vx = pan.x * tuning_.panSpeed;
            vy = -pan.y * tuning_.panSpeed;
        } else {
            vx = rotation * tuning_.rotateSpeed;
        }
        restX_ += vx * static_cast<float>(dt);
        restY_ += vy * static_cast<float>(dt);
        const auto dx = static_cast<std::int32_t>(std::trunc(restX_));
        const auto dy = static_cast<std::int32_t>(std::trunc(restY_));
        restX_ -= static_cast<float>(dx);
        restY_ -= static_cast<float>(dy);
        if (dx != 0 || dy != 0) {
            MapEvent move;
            move.dx = dx;
            move.dy = dy;
            out.events.push_back(move);
        }
    }
    return out;
}

} // namespace evr::menu
