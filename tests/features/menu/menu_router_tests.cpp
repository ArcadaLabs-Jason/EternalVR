#include "features/menu/menu_router.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

using evr::input::Hand;
using evr::menu::CursorPixel;
using evr::menu::kKeyCentre;
using evr::menu::kKeyContinue;
using evr::menu::kKeyEscape;
using evr::menu::kKeyNextTab;
using evr::menu::kKeyObjectives;
using evr::menu::kKeyPreviousTab;
using evr::menu::kKeyUse;
using evr::menu::kWheelNotch;
using evr::menu::MenuRouter;
using evr::menu::PanelHit;
using evr::menu::RouterEvent;
using evr::menu::RouterInput;
using evr::menu::RouterOutput;

namespace {

using Kind = RouterEvent::Kind;

constexpr std::size_t kLeft = 0;
constexpr std::size_t kRight = 1;

PanelHit hitAt(float u, float v) {
    PanelHit h;
    h.u = u;
    h.v = v;
    h.distance = 1.5f;
    return h;
}

// A menu on a 1000 x 500 GUI with the game's cursor at `cursor` and the right hand pointing at (u, v).
RouterInput menuFrame(double t, CursorPixel cursor, float u = 0.5f, float v = 0.5f) {
    RouterInput in;
    in.seconds = t;
    in.menuActive = true;
    in.width = 1000;
    in.height = 500;
    in.gameCursor = cursor;
    in.hands[kRight].hit = hitAt(u, v);
    return in;
}

std::vector<Kind> kinds(const RouterOutput& out) {
    std::vector<Kind> k;
    for (const RouterEvent& e : out.events) {
        k.push_back(e.kind);
    }
    return k;
}

bool has(const RouterOutput& out, Kind kind, std::uint8_t key = 0) {
    for (const RouterEvent& e : out.events) {
        if (e.kind == kind && (key == 0 || e.key == key)) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("router: no menu, nothing sent and gameplay free") {
    MenuRouter router;
    RouterInput in;
    in.seconds = 1.0;
    in.hands[kRight].trigger = 1.0f;
    const RouterOutput out = router.update(in);
    CHECK(out.events.empty());
    CHECK_FALSE(out.suppressGameplay);
    CHECK_FALSE(out.pointerVisible);
}

TEST_CASE("router: the cursor moves by the difference to the ray's pixel, one move at a time") {
    MenuRouter router;
    // The ray at (0.25, 0.5) is pixel (250, 250); the game's cursor is at (500, 250).
    RouterOutput out = router.update(menuFrame(0.0, {500, 250}, 0.25f, 0.5f));
    REQUIRE(out.events.size() == 1);
    CHECK(out.events[0].kind == Kind::Move);
    CHECK(out.events[0].dx == -250);
    CHECK(out.events[0].dy == 0);
    CHECK(out.suppressGameplay);
    CHECK(out.pointerVisible);
    CHECK(out.pointerHand == Hand::Right);
    // The game has not moved its cursor yet and the ray moved on: nothing more is sent.
    out = router.update(menuFrame(0.01, {500, 250}, 0.3f, 0.5f));
    CHECK(out.events.empty());
    // The game shows the cursor where the move put it: the next difference goes out.
    out = router.update(menuFrame(0.02, {250, 250}, 0.3f, 0.5f));
    REQUIRE(out.events.size() == 1);
    CHECK(out.events[0].dx == 50);
    // Pointing where the cursor already is sends nothing.
    out = router.update(menuFrame(0.03, {300, 250}, 0.3f, 0.5f));
    CHECK(out.events.empty());
}

TEST_CASE("router: a move the game never shows is sent again after the timeout") {
    MenuRouter router;
    RouterOutput out = router.update(menuFrame(0.0, {0, 0}, 0.5f, 0.5f));
    REQUIRE(out.events.size() == 1);
    out = router.update(menuFrame(0.1, {0, 0}, 0.5f, 0.5f));
    CHECK(out.events.empty());
    out = router.update(menuFrame(0.2, {0, 0}, 0.5f, 0.5f));
    REQUIRE(out.events.size() == 1);
    CHECK(out.events[0].dx == 500);
    CHECK(out.events[0].dy == 250);
}

TEST_CASE("router: no ray on the panel, or no cursor to read, leaves the cursor alone") {
    MenuRouter router;
    RouterInput in = menuFrame(0.0, {10, 10});
    in.hands[kRight].hit.reset();
    CHECK(router.update(in).events.empty());
    in = menuFrame(0.1, {10, 10});
    in.gameCursor.reset();
    const RouterOutput out = router.update(in);
    CHECK(out.events.empty());
    CHECK(out.suppressGameplay);
}

TEST_CASE("router: the trigger clicks once the cursor has settled, and holds the button while held") {
    MenuRouter router;
    // Cursor already on the target.
    router.update(menuFrame(0.0, {500, 250}));
    RouterInput in = menuFrame(0.1, {500, 250});
    in.hands[kRight].trigger = 0.9f;
    RouterOutput out = router.update(in);
    CHECK(kinds(out) == std::vector<Kind>{Kind::ButtonDown});
    CHECK(router.buttonDown());
    in.seconds = 0.2;
    out = router.update(in);
    CHECK(out.events.empty()); // held: dragging keeps the button down
    // Released below the release threshold (hysteresis: 0.45 still counts as held).
    in.seconds = 0.3;
    in.hands[kRight].trigger = 0.45f;
    CHECK(router.update(in).events.empty());
    in.seconds = 0.4;
    in.hands[kRight].trigger = 0.1f;
    out = router.update(in);
    CHECK(kinds(out) == std::vector<Kind>{Kind::ButtonUp});
    CHECK_FALSE(router.buttonDown());
}

TEST_CASE("router: a click waits for a move in flight, then lands") {
    MenuRouter router;
    RouterInput in = menuFrame(0.0, {0, 0}, 0.5f, 0.5f);
    in.hands[kRight].trigger = 1.0f;
    RouterOutput out = router.update(in);
    CHECK(kinds(out) == std::vector<Kind>{Kind::Move}); // the move first, no button yet
    // The game shows the cursor there: the move has landed, the click waits for it to settle.
    in = menuFrame(0.01, {500, 250});
    in.hands[kRight].trigger = 1.0f;
    CHECK(router.update(in).events.empty());
    in.seconds = 0.05;
    out = router.update(in);
    CHECK(kinds(out) == std::vector<Kind>{Kind::ButtonDown});
}

TEST_CASE("router: a quick tap still clicks, held for the minimum time") {
    MenuRouter router;
    router.update(menuFrame(0.0, {500, 250}));
    RouterInput in = menuFrame(0.1, {500, 250});
    in.hands[kRight].trigger = 1.0f;
    CHECK(kinds(router.update(in)) == std::vector<Kind>{Kind::ButtonDown});
    in.seconds = 0.11;
    in.hands[kRight].trigger = 0.0f;
    CHECK(router.update(in).events.empty()); // too soon
    in.seconds = 0.17;
    CHECK(kinds(router.update(in)) == std::vector<Kind>{Kind::ButtonUp});
}

TEST_CASE("router: a trigger already held when the menu opens does not click") {
    MenuRouter router;
    RouterInput in;
    in.seconds = 0.0;
    in.hands[kRight].trigger = 1.0f; // firing in gameplay
    router.update(in);
    in = menuFrame(0.1, {500, 250});
    in.hands[kRight].trigger = 1.0f;
    CHECK(router.update(in).events.empty());
    in.seconds = 0.2;
    CHECK(router.update(in).events.empty());
}

TEST_CASE("router: B and Y go back with a tapped Escape") {
    MenuRouter router;
    router.update(menuFrame(0.0, {500, 250}));
    RouterInput in = menuFrame(0.1, {500, 250});
    in.hands[kRight].secondary = true;
    RouterOutput out = router.update(in);
    REQUIRE(out.events.size() == 1);
    CHECK(out.events[0].kind == Kind::KeyDown);
    CHECK(out.events[0].key == kKeyEscape);
    in.seconds = 0.2;
    out = router.update(in);
    CHECK(has(out, Kind::KeyUp, kKeyEscape));
    CHECK_FALSE(has(out, Kind::KeyDown));
    in.hands[kRight].secondary = false;
    in.seconds = 0.3;
    CHECK(router.update(in).events.empty());
    // Y (the hand not pointing) goes back as well.
    in.hands[kLeft].secondary = true;
    in.seconds = 0.4;
    CHECK(has(router.update(in), Kind::KeyDown, kKeyEscape));
}

TEST_CASE("router: the left grip goes to the previous tab, the right grip to the next") {
    for (const Hand dominant : {Hand::Right, Hand::Left}) {
        MenuRouter router(dominant);
        router.update(menuFrame(0.0, {500, 250}));
        RouterInput in = menuFrame(0.1, {500, 250});
        in.hands[kLeft].grip = 0.9f;
        RouterOutput out = router.update(in);
        CHECK(kinds(out) == std::vector<Kind>{Kind::KeyDown});
        CHECK(has(out, Kind::KeyDown, kKeyPreviousTab));
        CHECK_FALSE(has(out, Kind::KeyDown, kKeyEscape)); // not back
        CHECK_FALSE(has(out, Kind::RightButtonDown));     // not the right button any more
        // Held: one tab only.
        in.seconds = 0.3;
        CHECK_FALSE(has(router.update(in), Kind::KeyDown));
        in.hands[kLeft].grip = 0.0f;
        in.hands[kRight].grip = 1.0f;
        in.seconds = 0.4;
        CHECK(has(router.update(in), Kind::KeyDown, kKeyNextTab));
    }
}

TEST_CASE("router: a stick click taps the map's centre key") {
    MenuRouter router;
    router.update(menuFrame(0.0, {500, 250}));
    RouterInput in = menuFrame(0.1, {500, 250});
    in.hands[kLeft].stickClick = true;
    CHECK(has(router.update(in), Kind::KeyDown, kKeyCentre));
    in.seconds = 0.2;
    RouterOutput out = router.update(in);
    CHECK(has(out, Kind::KeyUp, kKeyCentre));
    CHECK_FALSE(has(out, Kind::KeyDown)); // held, not repeated
    in.hands[kLeft].stickClick = false;
    in.hands[kRight].stickClick = true;
    in.seconds = 0.3;
    CHECK(has(router.update(in), Kind::KeyDown, kKeyCentre));
    // The other stick pressed while this one is held: the recenter chord, no key.
    in.hands[kLeft].stickClick = true;
    in.seconds = 0.5;
    CHECK_FALSE(has(router.update(in), Kind::KeyDown, kKeyCentre));
}

TEST_CASE("router: a stick up or down scrolls, with repeat") {
    MenuRouter router;
    router.update(menuFrame(0.0, {500, 250}));
    RouterInput in = menuFrame(0.1, {500, 250});
    in.hands[kRight].stick = {0.1f, 0.9f};
    RouterOutput out = router.update(in);
    REQUIRE(out.events.size() == 1);
    CHECK(out.events[0].kind == Kind::Wheel);
    CHECK(out.events[0].wheel == kWheelNotch);
    // Held: nothing until the repeat delay, then every interval.
    in.seconds = 0.3;
    CHECK(router.update(in).events.empty());
    in.seconds = 0.51;
    CHECK(has(router.update(in), Kind::Wheel));
    in.seconds = 0.6;
    CHECK(router.update(in).events.empty());
    in.seconds = 0.64;
    CHECK(has(router.update(in), Kind::Wheel));
    // Down scrolls the other way.
    in.hands[kRight].stick = {0.0f, -0.8f};
    in.seconds = 0.7;
    out = router.update(in);
    REQUIRE(!out.events.empty());
    CHECK(out.events[0].wheel == -kWheelNotch);
    // The other hand's stick scrolls too.
    in.hands[kRight].stick = {};
    in.hands[kLeft].stick = {0.0f, 1.0f};
    in.seconds = 0.8;
    out = router.update(in);
    REQUIRE(out.events.size() == 1);
    CHECK(out.events[0].wheel == kWheelNotch);
}

TEST_CASE("router: a stick left or right changes tabs") {
    MenuRouter router;
    router.update(menuFrame(0.0, {500, 250}));
    RouterInput in = menuFrame(0.1, {500, 250});
    in.hands[kRight].stick = {0.9f, 0.1f};
    CHECK(has(router.update(in), Kind::KeyDown, kKeyNextTab));
    in.hands[kRight].stick = {};
    in.hands[kLeft].stick = {-1.0f, 0.0f};
    in.seconds = 0.3;
    const RouterOutput out = router.update(in);
    CHECK(has(out, Kind::KeyDown, kKeyPreviousTab));
    CHECK(has(out, Kind::KeyUp, kKeyNextTab));
}

TEST_CASE("router: the other hand's trigger on the panel takes the pointer") {
    MenuRouter router(Hand::Right);
    router.update(menuFrame(0.0, {500, 250}));
    RouterInput in = menuFrame(0.1, {500, 250});
    in.hands[kLeft].hit = hitAt(0.1f, 0.1f);
    in.hands[kLeft].trigger = 1.0f;
    RouterOutput out = router.update(in);
    CHECK(out.pointerHand == Hand::Left);
    // The cursor goes to the left hand's ray first; the click follows once it settles.
    REQUIRE(!out.events.empty());
    CHECK(out.events[0].kind == Kind::Move);
    CHECK(out.events[0].dx == 100 - 500);
    CHECK_FALSE(has(out, Kind::ButtonDown));
    // Pointed off the panel, the other trigger does not take it.
    MenuRouter second(Hand::Right);
    second.update(menuFrame(0.0, {500, 250}));
    in = menuFrame(0.1, {500, 250});
    in.hands[kLeft].trigger = 1.0f;
    CHECK(second.update(in).pointerHand == Hand::Right);
}

TEST_CASE("router: closing the menu releases what is held and holds gameplay back until let go") {
    MenuRouter router;
    router.update(menuFrame(0.0, {500, 250}));
    RouterInput in = menuFrame(0.1, {500, 250});
    in.hands[kRight].trigger = 1.0f;
    in.hands[kRight].secondary = true;
    RouterOutput out = router.update(in);
    CHECK(has(out, Kind::ButtonDown));
    CHECK(has(out, Kind::KeyDown, kKeyEscape));
    // The click closed the menu (Resume): the button and the key go up at once.
    RouterInput closed;
    closed.seconds = 0.12;
    closed.hands[kRight].trigger = 1.0f;
    out = router.update(closed);
    CHECK(has(out, Kind::ButtonUp));
    CHECK(has(out, Kind::KeyUp, kKeyEscape));
    CHECK(out.suppressGameplay); // the trigger is still held from the menu: no shot
    CHECK_FALSE(out.pointerVisible);
    closed.seconds = 0.5;
    CHECK(router.update(closed).suppressGameplay);
    // Let go: gameplay is back, and a new pull fires.
    closed.seconds = 0.6;
    closed.hands[kRight].trigger = 0.0f;
    CHECK_FALSE(router.update(closed).suppressGameplay);
    closed.seconds = 0.7;
    closed.hands[kRight].trigger = 1.0f;
    out = router.update(closed);
    CHECK_FALSE(out.suppressGameplay);
    CHECK(out.events.empty());
}

TEST_CASE("router: a left-handed player points with the left hand") {
    MenuRouter router(Hand::Left);
    RouterInput in = menuFrame(0.0, {500, 250});
    in.hands[kRight].hit.reset();
    in.hands[kLeft].hit = hitAt(0.0f, 0.0f);
    const RouterOutput out = router.update(in);
    CHECK(out.pointerHand == Hand::Left);
    REQUIRE(out.events.size() == 1);
    CHECK(out.events[0].dx == -500);
    CHECK(out.events[0].dy == -250);
}

TEST_CASE("router: NaN input is ignored") {
    MenuRouter router;
    RouterInput in = menuFrame(0.0, {500, 250});
    in.hands[kRight].trigger = std::nanf("");
    in.hands[kRight].stick = {std::nanf(""), std::nanf("")};
    CHECK(router.update(in).events.empty());
}

TEST_CASE("router: in a popup A / X taps Space instead of clicking, a stick click taps E") {
    MenuRouter router;
    RouterInput in = menuFrame(0.0, {500, 250});
    in.popup = true;
    router.update(in);
    // A on the pointing hand: Space, no click.
    in.seconds = 0.1;
    in.hands[kRight].primary = true;
    RouterOutput out = router.update(in);
    CHECK(has(out, Kind::KeyDown, kKeyContinue));
    CHECK_FALSE(has(out, Kind::ButtonDown));
    in.seconds = 0.2;
    out = router.update(in);
    CHECK(has(out, Kind::KeyUp, kKeyContinue));
    CHECK_FALSE(has(out, Kind::ButtonDown));
    // X on the other hand as well, and it does not take the pointer.
    in.hands[kRight].primary = false;
    in.hands[kLeft].primary = true;
    in.hands[kLeft].hit = hitAt(0.2f, 0.2f);
    in.seconds = 0.3;
    out = router.update(in);
    CHECK(has(out, Kind::KeyDown, kKeyContinue));
    CHECK(out.pointerHand == Hand::Right);
    // A stick click: E, not C.
    in.hands[kLeft].primary = false;
    in.hands[kRight].stickClick = true;
    in.seconds = 0.5;
    out = router.update(in);
    CHECK(has(out, Kind::KeyDown, kKeyUse));
    CHECK_FALSE(has(out, Kind::KeyDown, kKeyCentre));
    // Y holds Left Alt (the objectives key) for as long as it is held, and does not go back.
    in.hands[kRight].stickClick = false;
    in.hands[kLeft].secondary = true;
    in.seconds = 0.55;
    out = router.update(in);
    CHECK(has(out, Kind::KeyDown, kKeyObjectives));
    CHECK_FALSE(has(out, Kind::KeyDown, kKeyEscape));
    in.seconds = 0.6;
    CHECK_FALSE(has(router.update(in), Kind::KeyUp, kKeyObjectives));
    in.hands[kLeft].secondary = false;
    in.seconds = 0.65;
    CHECK(has(router.update(in), Kind::KeyUp, kKeyObjectives));
    // The trigger still clicks, B still goes back.
    in.hands[kRight].trigger = 1.0f;
    in.hands[kRight].secondary = true;
    in.seconds = 0.7;
    out = router.update(in);
    CHECK(has(out, Kind::ButtonDown));
    CHECK(has(out, Kind::KeyDown, kKeyEscape));
    CHECK(out.suppressGameplay);
}

TEST_CASE("router: outside a popup A clicks and a stick click taps C, never Space or E") {
    MenuRouter router;
    RouterInput in = menuFrame(0.0, {500, 250});
    router.update(in);
    in.seconds = 0.1;
    in.hands[kRight].primary = true;
    in.hands[kLeft].stickClick = true;
    const RouterOutput out = router.update(in);
    CHECK(has(out, Kind::ButtonDown));
    CHECK(has(out, Kind::KeyDown, kKeyCentre));
    CHECK_FALSE(has(out, Kind::KeyDown, kKeyContinue));
    CHECK_FALSE(has(out, Kind::KeyDown, kKeyUse));
}

TEST_CASE("router: A held when a popup opens is not a Space press") {
    MenuRouter router;
    RouterInput idle;
    idle.seconds = 0.0;
    idle.hands[kRight].primary = true; // jumping when the popup came up
    router.update(idle);
    RouterInput in = menuFrame(0.1, {500, 250});
    in.popup = true;
    in.hands[kRight].primary = true;
    CHECK_FALSE(has(router.update(in), Kind::KeyDown, kKeyContinue));
    in.seconds = 0.2;
    in.hands[kRight].primary = false;
    router.update(in);
    in.seconds = 0.3;
    in.hands[kRight].primary = true;
    CHECK(has(router.update(in), Kind::KeyDown, kKeyContinue));
}

TEST_CASE("router: Left Alt held in a popup is let go when the popup closes") {
    MenuRouter router;
    RouterInput in = menuFrame(0.0, {500, 250});
    in.popup = true;
    router.update(in);
    in.seconds = 0.1;
    in.hands[kLeft].secondary = true;
    CHECK(has(router.update(in), Kind::KeyDown, kKeyObjectives));
    RouterInput closed;
    closed.seconds = 0.12;
    closed.hands[kLeft].secondary = true;
    CHECK(has(router.update(closed), Kind::KeyUp, kKeyObjectives));
}
