#include "features/menu/dossier_screen.hpp"
#include "features/menu/menu_router.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

// The Dossier's page from the screen the game shows (dossier_screen.hpp, RouterInput::dossierShows), as well
// as from the router's own tab keys.

using evr::input::Hand;
using evr::menu::CursorPixel;
using evr::menu::dossierPageOfScreen;
using evr::menu::kKeyNextTab;
using evr::menu::kKeyPreviousTab;
using evr::menu::MenuRouter;
using evr::menu::PageChange;
using evr::menu::PanelHit;
using evr::menu::RouterEvent;
using evr::menu::RouterInput;
using evr::menu::RouterOutput;

namespace {

using Kind = RouterEvent::Kind;

constexpr std::size_t kLeft = 0;
constexpr std::size_t kRight = 1;

// The game's dossierPage_t values.
constexpr int kMapScreen = 0;
constexpr int kCodexScreen = 4;
constexpr int kChallengesScreen = 5;

// The Dossier the controllers asked for, on a 1000 x 500 GUI, the right hand pointing off the panel's middle
// (the sticks have the cursor).
RouterInput dossierFrame(double t) {
    RouterInput in;
    in.seconds = t;
    in.menuActive = true;
    in.dossier = true;
    in.width = 1000;
    in.height = 500;
    in.gameCursor = CursorPixel{100, 100};
    PanelHit hit;
    hit.u = 0.1f;
    hit.v = 0.1f;
    hit.distance = 1.5f;
    in.hands[kRight].hit = hit;
    return in;
}

// The game showed `screen` (its show count goes up by `calls`).
void shows(RouterInput& in, int screen, std::uint32_t calls = 1) {
    in.dossierShows += calls;
    in.dossierShown = dossierPageOfScreen(screen);
}

bool has(const std::vector<RouterEvent>& events, Kind kind, std::uint8_t key = 0) {
    return std::any_of(events.begin(), events.end(),
                       [&](const RouterEvent& e) { return e.kind == kind && (key == 0 || e.key == key); });
}

// Frames at 90 Hz from `from` to `to`; every event.
std::vector<RouterEvent> run(MenuRouter& router, RouterInput& in, double from, double to) {
    std::vector<RouterEvent> all;
    for (double t = from; t < to; t += 1.0 / 90.0) {
        in.seconds = t;
        const RouterOutput out = router.update(in);
        all.insert(all.end(), out.events.begin(), out.events.end());
    }
    return all;
}

} // namespace

TEST_CASE("dossier screen: the game's six screens on the router's four pages") {
    CHECK(dossierPageOfScreen(0) == 0);  // map
    CHECK(dossierPageOfScreen(1) == 1);  // arsenal
    CHECK(dossierPageOfScreen(2) == -1); // the Praetor suit: no tab of its own
    CHECK(dossierPageOfScreen(3) == -1); // runes: no tab of its own
    CHECK(dossierPageOfScreen(4) == 2);  // codex
    CHECK(dossierPageOfScreen(5) == 3);  // challenges
    CHECK(dossierPageOfScreen(-1) == -1);
    CHECK(dossierPageOfScreen(6) == -1);
}

TEST_CASE(
    "router: a Dossier the game opens on Challenges is not the map: the sticks scroll and change tabs") {
    MenuRouter router(Hand::Right);
    RouterInput in = dossierFrame(0.0);
    shows(in, kChallengesScreen, 2); // ShowScreen runs twice on opening
    const RouterOutput opened = router.update(in);
    CHECK_FALSE(opened.mapPage);
    CHECK(router.dossierPage() == 3);
    REQUIRE(opened.pageChanges.size() == 2);
    CHECK(opened.pageChanges[0].why == PageChange::Why::Opened);
    CHECK(opened.pageChanges[1].why == PageChange::Why::GameShows);
    CHECK(opened.pageChanges[1].to == 3);
    in.hands[kRight].stick = {1.0f, 0.0f};
    const std::vector<RouterEvent> events = run(router, in, 0.01, 0.5);
    CHECK_FALSE(has(events, Kind::ButtonDown)); // no map drag
}

TEST_CASE("router: the game's screen a frame after the Dossier opened corrects the map taken for granted") {
    MenuRouter router(Hand::Right);
    RouterInput in = dossierFrame(0.0);
    CHECK(router.update(in).mapPage); // nothing shown yet: the map, as before
    in.seconds = 0.011;
    shows(in, kChallengesScreen, 2);
    const RouterOutput out = router.update(in);
    CHECK_FALSE(out.mapPage);
    REQUIRE(out.pageChanges.size() == 1);
    CHECK(out.pageChanges[0].from == 0);
    CHECK(out.pageChanges[0].to == 3);
}

TEST_CASE("router: opened on Challenges, back to the map by the tabs: the sticks pan the map there") {
    // yodaui (2026-10-26): the Dossier sometimes opened on Challenges; on the map, reached by the tabs, the
    // sticks changed pages instead of moving the map (the router counted from the map it took for granted).
    MenuRouter router(Hand::Right);
    RouterInput in = dossierFrame(0.0);
    shows(in, kChallengesScreen, 2);
    router.update(in);
    // The left grip: previous tab. From Challenges the router sends Q (it no longer keeps it back as if on
    // the map's first tab).
    in.hands[kLeft].grip = 1.0f;
    const std::vector<RouterEvent> back = run(router, in, 0.01, 0.1);
    CHECK(has(back, Kind::KeyDown, kKeyPreviousTab));
    in.hands[kLeft].grip = 0.0f;
    shows(in, kCodexScreen);
    run(router, in, 0.1, 0.2);
    CHECK(router.dossierPage() == 2);
    // The game ends up on its map (another two tabs, or a tab clicked): the sticks pan.
    shows(in, kMapScreen);
    in.seconds = 0.21;
    CHECK(router.update(in).mapPage);
    in.hands[kRight].stick = {1.0f, 0.0f};
    const std::vector<RouterEvent> panned = run(router, in, 0.22, 0.6);
    CHECK(has(panned, Kind::ButtonDown));
    CHECK_FALSE(has(panned, Kind::KeyDown, kKeyNextTab));
}

TEST_CASE("router: screens shown in an earlier Dossier are not taken again when it opens on its map") {
    MenuRouter router(Hand::Right);
    RouterInput in = dossierFrame(0.0);
    shows(in, kMapScreen, 2);
    router.update(in);
    shows(in, kChallengesScreen); // a tab, then the Dossier closes on Challenges
    run(router, in, 0.01, 0.1);
    CHECK(router.dossierPage() == 3);
    in.menuActive = false;
    in.dossier = false;
    run(router, in, 0.1, 0.5);
    // Opened again: the game has not shown a screen yet, so the map is taken, not the last Challenges.
    in.menuActive = true;
    in.dossier = true;
    in.seconds = 0.5;
    CHECK(router.update(in).mapPage);
    CHECK(router.dossierPage() == 0);
}

TEST_CASE("router: a screen the game shows while the menu is still closed is the page it opens on") {
    // ShowScreen can run a frame before the game's cursor comes up.
    MenuRouter router(Hand::Right);
    RouterInput in = dossierFrame(0.0);
    in.menuActive = false;
    in.dossier = false;
    router.update(in);
    shows(in, kChallengesScreen, 2);
    in.seconds = 0.011;
    router.update(in);
    in.menuActive = true;
    in.dossier = true;
    in.seconds = 0.022;
    CHECK_FALSE(router.update(in).mapPage);
    CHECK(router.dossierPage() == 3);
}

TEST_CASE("router: without the hook (no screens counted) the Dossier opens on its map as before") {
    MenuRouter router(Hand::Right);
    RouterInput in = dossierFrame(0.0);
    const RouterOutput out = router.update(in);
    CHECK(out.mapPage);
    REQUIRE(out.pageChanges.size() == 1);
    CHECK(out.pageChanges[0].why == PageChange::Why::Opened);
}

TEST_CASE("router: a popup does not take a Dossier screen") {
    MenuRouter router(Hand::Right);
    RouterInput in = dossierFrame(0.0);
    in.dossier = false;
    in.popup = true;
    shows(in, kMapScreen);
    const RouterOutput out = router.update(in);
    CHECK_FALSE(out.mapPage);
    CHECK(router.dossierPage() == -1);
}
