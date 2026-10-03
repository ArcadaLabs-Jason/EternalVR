#include "features/menu/map_drag.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

using evr::input::Axis2;
using evr::menu::CursorPixel;
using evr::menu::DragButton;
using evr::menu::kMapKeyRight;
using evr::menu::kMapKeyUp;
using evr::menu::MapDrag;
using evr::menu::MapDragInput;
using evr::menu::MapDragOutput;
using evr::menu::MapEvent;
using evr::menu::MapPanBy;
using evr::menu::withDeadzone;

namespace {

constexpr double kFrame = 1.0 / 90.0;
constexpr std::int32_t kWidth = 1000;
constexpr std::int32_t kHeight = 500;

// The game as the automap sees it (docs/rig-findings/menus.md section 6): the cursor follows the motion and
// clamps to the GUI; the left button's motion pans, else the right button's rotates (both held: it pans);
// the pan keys pan on their own.
struct Sim {
    MapDrag drag;
    CursorPixel cursor{10, 10};
    double t = 0.0;
    bool left = false;
    bool right = false;
    bool keyUp = false;
    bool keyRight = false;
    int presses = 0;
    int releases = 0;
    long panX = 0; // motion counted as a left drag
    long panY = 0;
    long rotate = 0; // motion counted as a right drag
    int keyFrames = 0;
    bool pressedAwayFromMiddle = false;

    // One frame: the game's cursor lands on the drag's target at once.
    MapDragOutput step(Axis2 pan, Axis2 turn = {}) {
        MapDragInput in;
        in.seconds = t;
        in.pan = pan;
        in.turn = turn;
        in.cursor = cursor;
        in.cursorIdle = true;
        in.width = kWidth;
        in.height = kHeight;
        const MapDragOutput out = drag.update(in);
        for (const MapEvent& e : out.events) {
            switch (e.kind) {
            case MapEvent::Kind::Button: {
                bool& held = e.button == DragButton::Left ? left : right;
                CHECK(held != e.down); // no second press, no release of a button that is up
                held = e.down;
                if (e.down) {
                    ++presses;
                    pressedAwayFromMiddle = pressedAwayFromMiddle || cursor != CursorPixel{500, 250};
                } else {
                    ++releases;
                }
                break;
            }
            case MapEvent::Kind::Key:
                if (e.key == kMapKeyUp) {
                    keyUp = e.down;
                } else if (e.key == kMapKeyRight) {
                    keyRight = e.down;
                }
                break;
            case MapEvent::Kind::Move:
                CHECK((left || right)); // the drag moves the cursor only with a button down
                if (left) {
                    panX += e.dx;
                    panY += e.dy;
                } else if (right) {
                    rotate += e.dx;
                }
                cursor.x = std::clamp(cursor.x + e.dx, 0, kWidth - 1);
                cursor.y = std::clamp(cursor.y + e.dy, 0, kHeight - 1);
                break;
            }
        }
        if (out.target) {
            CHECK_FALSE((left || right)); // the router moves the cursor only with the buttons up
            cursor = *out.target;
        }
        keyFrames += keyUp || keyRight ? 1 : 0;
        t += kFrame;
        return out;
    }

    void run(int frames, Axis2 pan, Axis2 turn = {}) {
        for (int i = 0; i < frames; ++i) {
            step(pan, turn);
        }
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
        CHECK(out.events.empty());
    }
    CHECK_FALSE(sim.drag.active());
}

TEST_CASE("map drag: a held pan stick presses once in the middle and pans until it is let go") {
    Sim sim;
    sim.run(270, {1.0f, 0.0f}); // 3 s at full right: the cursor reaches the edge after about half a second
    CHECK(sim.presses == 1);
    CHECK(sim.releases == 0);
    CHECK_FALSE(sim.pressedAwayFromMiddle);
    CHECK(sim.left);
    CHECK(sim.cursor.x == kWidth - 1); // held at the edge; the motion still counts
    CHECK(sim.panX > 2800);            // about 1000 counts a second for 3 s, less the press
    CHECK(sim.panX < 3100);
    CHECK(sim.panY == 0);
    CHECK(sim.drag.panBy() == MapPanBy::Drag);
    // Let go: the button goes up, and the ray has the cursor again.
    sim.run(30, {});
    CHECK(sim.releases == 1);
    CHECK_FALSE(sim.left);
    CHECK_FALSE(sim.drag.active());
}

TEST_CASE(
    "map drag: the stick pushed right drags right, pushed away drags up, and it turns without a press") {
    Sim sim;
    sim.run(45, {1.0f, 0.0f});
    CHECK(sim.panX > 0);
    sim.run(45, {0.0f, 1.0f}); // a turn of the stick: the same press
    sim.run(45, {-1.0f, 0.0f});
    CHECK(sim.presses == 1);
    CHECK(sim.panY < -300); // pushed away: the drag goes up the screen
    CHECK(sim.panX < 300);  // as far left as it went right, less the press
}

TEST_CASE("map drag: a stick held part of the way pans slower") {
    Sim full;
    full.run(90, {1.0f, 0.0f});
    Sim half;
    half.run(90, {0.6f, 0.0f}); // half way past the deadzone
    CHECK(half.panX > full.panX * 4 / 10);
    CHECK(half.panX < full.panX * 6 / 10);
}

TEST_CASE("map drag: a stick near the deadzone's edge does not press and let go again") {
    Sim sim;
    sim.run(30, {0.5f, 0.0f});
    for (int i = 0; i < 60; ++i) {
        sim.step({i % 2 == 0 ? 0.19f : 0.21f, 0.0f}); // through the deadzone's edge and back
    }
    CHECK(sim.presses == 1);
    CHECK(sim.releases == 0);
    sim.run(10, {0.1f, 0.0f}); // within the release zone: let go
    CHECK(sim.releases == 1);
}

TEST_CASE("map drag: the turn stick left or right rotates with the right button held; up or down does not") {
    Sim sim;
    sim.run(180, {}, {-1.0f, 0.0f});
    CHECK(sim.presses == 1);
    CHECK(sim.right);
    CHECK(sim.rotate < -1800); // about 1100 counts a second for 2 s, the cursor long at the edge
    CHECK(sim.drag.rotating());
    Sim zoom;
    zoom.run(60, {}, {0.1f, 1.0f});
    CHECK(zoom.presses == 0);
    CHECK_FALSE(zoom.drag.rotating());
}

TEST_CASE("map drag: both sticks at once rotate with the right button and pan with the keys") {
    Sim sim;
    sim.run(180, {0.0f, 1.0f}, {1.0f, 0.0f});
    CHECK(sim.right);
    CHECK_FALSE(sim.left);
    CHECK(sim.rotate > 1800);
    CHECK(sim.keyUp);
    CHECK(sim.keyFrames >= 175);
    CHECK(sim.drag.panBy() == MapPanBy::Keys);
    CHECK(sim.presses == 1);
}

TEST_CASE("map drag: a rotation joining a pan moves the pan onto the keys, where it stays") {
    Sim sim;
    sim.run(45, {1.0f, 0.0f});
    REQUIRE(sim.left);
    const long panned = sim.panX;
    sim.run(90, {1.0f, 0.0f}, {1.0f, 0.0f});
    CHECK_FALSE(sim.left); // the left button went up: with both held every motion would pan
    CHECK(sim.right);
    CHECK(sim.keyRight);
    CHECK_FALSE(sim.pressedAwayFromMiddle); // the right button too was pressed in the middle
    CHECK(sim.panX == panned);
    // The rotation ends: the pan stays on the keys, with no new press, until its stick is let go.
    sim.run(45, {1.0f, 0.0f});
    CHECK_FALSE(sim.right);
    CHECK_FALSE(sim.left);
    CHECK(sim.keyRight);
    CHECK(sim.presses == 2);
    sim.run(10, {});
    CHECK_FALSE(sim.keyRight);
    CHECK_FALSE(sim.drag.active());
    // A new pan alone is a drag again.
    sim.run(20, {1.0f, 0.0f});
    CHECK(sim.left);
    CHECK(sim.drag.panBy() == MapPanBy::Drag);
}

TEST_CASE("map drag: a reset gives the release of the button and the keys still down") {
    Sim sim;
    sim.run(30, {0.0f, 1.0f}, {1.0f, 0.0f});
    REQUIRE(sim.drag.buttonDown());
    REQUIRE(sim.keyUp);
    const std::vector<MapEvent> up = sim.drag.reset();
    const bool rightUp = std::any_of(up.begin(), up.end(), [](const MapEvent& e) {
        return e.kind == MapEvent::Kind::Button && e.button == DragButton::Right && !e.down;
    });
    const bool keyUp = std::any_of(up.begin(), up.end(), [](const MapEvent& e) {
        return e.kind == MapEvent::Kind::Key && e.key == kMapKeyUp && !e.down;
    });
    CHECK(rightUp);
    CHECK(keyUp);
    CHECK_FALSE(sim.drag.active());
    CHECK(sim.drag.reset().empty());
}

TEST_CASE("map drag: no cursor to read, no drag") {
    MapDrag drag;
    MapDragInput in;
    in.pan = {1.0f, 0.0f};
    in.width = 1000;
    in.height = 500;
    CHECK(drag.update(in).events.empty());
    CHECK_FALSE(drag.active());
}
