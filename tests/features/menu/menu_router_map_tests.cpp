#include "features/menu/menu_router.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

// The Dossier's map page from the sticks (map_drag.hpp through the router).

using evr::input::Hand;
using evr::input::MapSticks;
using evr::menu::CursorPixel;
using evr::menu::kKeyNextTab;
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

// A menu on a 1000 x 500 GUI with the game's cursor at `cursor` and the right hand pointing at the middle.
RouterInput menuFrame(double t, CursorPixel cursor) {
    RouterInput in;
    in.seconds = t;
    in.menuActive = true;
    in.width = 1000;
    in.height = 500;
    in.gameCursor = cursor;
    in.hands[kRight].hit = hitAt(0.5f, 0.5f);
    return in;
}

bool has(const std::vector<RouterEvent>& events, Kind kind, std::uint8_t key = 0) {
    return std::any_of(events.begin(), events.end(),
                       [&](const RouterEvent& e) { return e.kind == kind && (key == 0 || e.key == key); });
}

} // namespace

namespace {

// The Dossier on its map page (opened by the controllers), the right hand pointing at the middle.
RouterInput mapFrame(double t, CursorPixel cursor) {
    RouterInput in = menuFrame(t, cursor);
    in.dossier = true;
    return in;
}

// Runs frames at 90 Hz from `from` to `to`, the game's cursor following every move at once; the events.
std::vector<RouterEvent>
runMap(MenuRouter& router, RouterInput in, double from, double to, CursorPixel& cursor) {
    std::vector<RouterEvent> all;
    for (double t = from; t < to; t += 1.0 / 90.0) {
        in.seconds = t;
        in.gameCursor = cursor;
        const RouterOutput out = router.update(in);
        for (const RouterEvent& e : out.events) {
            if (e.kind == Kind::Move) {
                cursor.x += e.dx;
                cursor.y += e.dy;
            }
            all.push_back(e);
        }
    }
    return all;
}

int count(const std::vector<RouterEvent>& events, Kind kind) {
    return static_cast<int>(
        std::count_if(events.begin(), events.end(), [kind](const RouterEvent& e) { return e.kind == kind; }));
}

} // namespace

TEST_CASE("router: on the Dossier's map the weapon hand's stick pans with left drags from the middle") {
    MenuRouter router(Hand::Right);
    CursorPixel cursor{100, 100};
    RouterInput in = mapFrame(0.0, cursor);
    in.hands[kRight].hit = hitAt(0.1f, 0.1f); // the ray is somewhere else: the stick has the cursor
    CHECK(router.update(in).mapPage);
    in.hands[kRight].stick = {1.0f, 0.0f};
    const std::vector<RouterEvent> events = runMap(router, in, 0.01, 1.5, cursor);
    // Several strokes: each a press in the middle, moves to the right (the map follows the stick), a release.
    CHECK(count(events, Kind::ButtonDown) >= 2);
    CHECK(count(events, Kind::ButtonDown) - count(events, Kind::ButtonUp) <= 1);
    CHECK(count(events, Kind::Wheel) == 0);
    CHECK_FALSE(has(events, Kind::KeyDown, kKeyNextTab)); // no tabs from the stick here
    bool down = false;
    for (const RouterEvent& e : events) {
        if (e.kind == Kind::ButtonDown) {
            down = true;
        } else if (e.kind == Kind::ButtonUp) {
            down = false;
        } else if (e.kind == Kind::Move && down) {
            CHECK(e.dx >= 0);
        }
    }
    // Let go: the last stroke ends and the ray has the cursor again.
    in.hands[kRight].stick = {};
    const std::vector<RouterEvent> after = runMap(router, in, 1.5, 2.0, cursor);
    CHECK(count(events, Kind::ButtonDown) + 0 ==
          count(events, Kind::ButtonUp) + count(after, Kind::ButtonUp));
    CHECK(cursor == CursorPixel{100, 50}); // (0.1, 0.1) on the 1000 x 500 GUI
}

TEST_CASE("router: on the map the other stick zooms up and down and rotates with right drags") {
    MenuRouter router(Hand::Right);
    CursorPixel cursor{500, 250};
    RouterInput in = mapFrame(0.0, cursor);
    router.update(in);
    in.hands[kLeft].stick = {0.0f, 1.0f};
    std::vector<RouterEvent> events = runMap(router, in, 0.01, 0.5, cursor);
    CHECK(count(events, Kind::Wheel) >= 4); // faster than the menu's scroll repeat
    CHECK(count(events, Kind::ButtonDown) == 0);
    in.hands[kLeft].stick = {-1.0f, 0.1f};
    events = runMap(router, in, 0.5, 1.0, cursor);
    CHECK(count(events, Kind::RightButtonDown) >= 1);
    CHECK(count(events, Kind::ButtonDown) == 0);
    CHECK(count(events, Kind::Wheel) == 0);
    in.hands[kLeft].stick = {};
    in.menuActive = false;
    in.seconds = 1.1;
    const RouterOutput closed = router.update(in);
    CHECK_FALSE(closed.mapPage);
}

namespace {

// On the map page, the stick at `panStick` pans (left drags, never the wheel, whichever way it is pushed)
// and the one at `turnStick` zooms (up / down, the wheel) and rotates (left / right, right drags).
void checkMapSticks(MenuRouter& router, std::size_t panStick, std::size_t turnStick) {
    CursorPixel cursor{500, 250};
    RouterInput in = mapFrame(0.0, cursor);
    in.hands[kLeft].hit = hitAt(0.5f, 0.5f);
    REQUIRE(router.update(in).mapPage);
    in.hands[panStick].stick = {0.0f, 1.0f};
    std::vector<RouterEvent> events = runMap(router, in, 0.01, 0.5, cursor);
    CHECK(count(events, Kind::ButtonDown) >= 1);
    CHECK(count(events, Kind::RightButtonDown) == 0);
    CHECK(count(events, Kind::Wheel) == 0);
    in.hands[panStick].stick = {};
    in.hands[turnStick].stick = {0.0f, -1.0f};
    events = runMap(router, in, 0.5, 1.0, cursor);
    CHECK(count(events, Kind::Wheel) >= 4);
    CHECK(count(events, Kind::ButtonDown) == 0);
    CHECK(count(events, Kind::RightButtonDown) == 0);
    in.hands[turnStick].stick = {1.0f, 0.1f};
    events = runMap(router, in, 1.0, 1.5, cursor);
    CHECK(count(events, Kind::RightButtonDown) >= 1);
    CHECK(count(events, Kind::ButtonDown) == 0);
    CHECK(count(events, Kind::Wheel) == 0);
}

} // namespace

TEST_CASE("router: the map's sticks swap with MapSticks::OtherPans, for either weapon hand") {
    SUBCASE("right-handed, the default: the right stick pans, the left zooms and rotates") {
        MenuRouter router(Hand::Right);
        checkMapSticks(router, kRight, kLeft);
    }
    SUBCASE("right-handed, the other hand pans: the left stick pans, the right zooms and rotates") {
        MenuRouter router(Hand::Right, MapSticks::OtherPans);
        checkMapSticks(router, kLeft, kRight);
    }
    SUBCASE("left-handed, the default: the left stick pans, the right zooms and rotates") {
        MenuRouter router(Hand::Left, MapSticks::WeaponPans);
        checkMapSticks(router, kLeft, kRight);
    }
    SUBCASE("left-handed, the other hand pans: the right stick pans, the left zooms and rotates") {
        MenuRouter router(Hand::Left, MapSticks::OtherPans);
        checkMapSticks(router, kRight, kLeft);
    }
}

TEST_CASE("router: the map page ends with a tab key or a click on the tab strip, and after the last tab") {
    MenuRouter router(Hand::Right);
    RouterInput in = mapFrame(0.0, {500, 250});
    CHECK(router.update(in).mapPage);
    // The right grip: next tab (Arsenal); the left grip back to the map.
    in.hands[kRight].grip = 1.0f;
    in.seconds = 0.1;
    CHECK_FALSE(router.update(in).mapPage);
    CHECK(router.dossierPage() == 1);
    in.hands[kRight].grip = 0.0f;
    in.hands[kLeft].grip = 1.0f;
    in.seconds = 0.2;
    CHECK(router.update(in).mapPage);
    // Past the first tab the page is no longer known (the game may wrap).
    in.hands[kLeft].grip = 0.0f;
    in.seconds = 0.3;
    router.update(in);
    in.hands[kLeft].grip = 1.0f;
    in.seconds = 0.4;
    CHECK_FALSE(router.update(in).mapPage);
    CHECK(router.dossierPage() == -1);

    MenuRouter clicked(Hand::Right);
    in = mapFrame(0.0, {500, 250});
    clicked.update(in);
    in.hands[kRight].trigger = 1.0f;
    in.hands[kRight].onTabStrip = true;
    in.seconds = 0.1;
    CHECK_FALSE(clicked.update(in).mapPage);
}

TEST_CASE("router: a menu that is not the Dossier, or a popup, keeps the sticks on scroll and tabs") {
    MenuRouter router;
    RouterInput in = menuFrame(0.0, {500, 250});
    CHECK_FALSE(router.update(in).mapPage);
    MenuRouter popup;
    in = mapFrame(0.0, {500, 250});
    in.popup = true;
    CHECK_FALSE(popup.update(in).mapPage);
}
