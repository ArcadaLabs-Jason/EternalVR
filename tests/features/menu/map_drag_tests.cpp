#include "features/menu/map_drag.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>

using evr::input::Axis2;
using evr::menu::CursorPixel;
using evr::menu::DragButton;
using evr::menu::DragEvent;
using evr::menu::MapDrag;
using evr::menu::MapDragInput;
using evr::menu::MapDragOutput;
using evr::menu::withDeadzone;

namespace {

constexpr double kFrame = 1.0 / 90.0;

struct Sim {
    MapDrag drag;
    CursorPixel cursor{10, 10};
    double t = 0.0;
    int downs = 0;
    int ups = 0;
    std::optional<DragButton> held;
    int maxFromMiddle = 0;

    // One frame: the game's cursor lands on the last target at once.
    MapDragOutput step(Axis2 pan, Axis2 turn = {}) {
        MapDragInput in;
        in.seconds = t;
        in.pan = pan;
        in.turn = turn;
        in.cursor = cursor;
        in.cursorIdle = true;
        in.width = 1000;
        in.height = 500;
        const MapDragOutput out = drag.update(in);
        if (out.event) {
            if (out.event->down) {
                ++downs;
                CHECK_FALSE(held);
                CHECK(cursor == CursorPixel{500, 250}); // every stroke starts in the middle
                held = out.event->button;
            } else {
                ++ups;
                CHECK(held == out.event->button);
                held.reset();
            }
        }
        if (out.target) {
            cursor = *out.target;
        }
        maxFromMiddle = std::max(maxFromMiddle, std::abs(cursor.x - 500) + std::abs(cursor.y - 250));
        t += kFrame;
        return out;
    }
};

} // namespace

TEST_CASE("map drag: the deadzone is taken out and the rest rescaled") {
    CHECK(withDeadzone({0.1f, 0.1f}, 0.2f) == Axis2{});
    const Axis2 full = withDeadzone({1.0f, 0.0f}, 0.2f);
    CHECK(full.x == doctest::Approx(1.0f));
    const Axis2 half = withDeadzone({0.0f, 0.6f}, 0.2f);
    CHECK(half.y == doctest::Approx(0.5f));
    CHECK(withDeadzone({std::nanf(""), 0.0f}, 0.2f) == Axis2{});
}

TEST_CASE("map drag: a stick at rest leaves the cursor to the ray") {
    Sim sim;
    for (int i = 0; i < 20; ++i) {
        const MapDragOutput out = sim.step({0.1f, 0.0f});
        CHECK_FALSE(out.target);
        CHECK_FALSE(out.event);
    }
    CHECK_FALSE(sim.drag.active());
}

TEST_CASE("map drag: the pan stick drags with the left button in strokes that never reach the edge") {
    Sim sim;
    for (int i = 0; i < 180; ++i) { // 2 s at full right
        sim.step({1.0f, 0.0f});
    }
    CHECK(sim.downs >= 3);
    CHECK(sim.downs - sim.ups <= 1);
    CHECK(sim.maxFromMiddle <= 101); // 0.2 of the 500 px side
    CHECK(sim.held.value_or(DragButton::Left) == DragButton::Left);
    // Let go: the stroke ends with the button up, and the ray has the cursor again.
    for (int i = 0; i < 30; ++i) {
        sim.step({});
    }
    CHECK(sim.downs == sim.ups);
    CHECK_FALSE(sim.drag.active());
}

TEST_CASE("map drag: the stick pushed right drags right, pushed away drags up") {
    Sim sim;
    CursorPixel last{500, 250};
    bool sawRight = false;
    for (int i = 0; i < 40; ++i) {
        sim.step({1.0f, 0.0f});
        if (sim.held) {
            CHECK(sim.cursor.x >= last.x);
            sawRight = sawRight || sim.cursor.x > 500;
        }
        last = sim.cursor;
    }
    CHECK(sawRight);
    Sim up;
    for (int i = 0; i < 40; ++i) {
        up.step({0.0f, 1.0f});
    }
    CHECK(up.cursor.y <= 250);
}

TEST_CASE("map drag: the turn stick left or right rotates with the right button; up or down does not") {
    Sim sim;
    for (int i = 0; i < 60; ++i) {
        sim.step({}, {-1.0f, 0.0f});
    }
    CHECK(sim.downs >= 1);
    CHECK(sim.held == DragButton::Right);
    Sim zoom;
    for (int i = 0; i < 60; ++i) {
        zoom.step({}, {0.1f, 1.0f});
    }
    CHECK(zoom.downs == 0);
}

TEST_CASE("map drag: switching from rotate to pan releases the right button first") {
    Sim sim;
    for (int i = 0; i < 30; ++i) {
        sim.step({}, {1.0f, 0.0f});
    }
    REQUIRE(sim.held == DragButton::Right);
    for (int i = 0; i < 30; ++i) {
        sim.step({1.0f, 0.0f});
    }
    CHECK(sim.held == DragButton::Left);
}

TEST_CASE("map drag: a reset while the button is down gives its release") {
    Sim sim;
    for (int i = 0; i < 10; ++i) {
        sim.step({1.0f, 0.0f});
    }
    REQUIRE(sim.drag.buttonDown());
    const std::optional<DragEvent> up = sim.drag.reset();
    REQUIRE(up);
    CHECK_FALSE(up->down);
    CHECK(up->button == DragButton::Left);
    CHECK_FALSE(sim.drag.reset());
}

TEST_CASE("map drag: no cursor to read, no drag") {
    MapDrag drag;
    MapDragInput in;
    in.pan = {1.0f, 0.0f};
    in.width = 1000;
    in.height = 500;
    CHECK_FALSE(drag.update(in).event);
    CHECK_FALSE(drag.active());
}
