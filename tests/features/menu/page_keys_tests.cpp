#include "features/menu/menu_router.hpp"
#include "features/menu/page_keys.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>

using evr::menu::kKeyNextTab;
using evr::menu::kKeyPreviousTab;
using evr::menu::kPageArrowKeys;
using evr::menu::MenuRouter;
using evr::menu::PageKeys;
using evr::menu::pageKeysFor;
using evr::menu::PageKeysInput;
using evr::menu::RouterEvent;
using evr::menu::RouterInput;
using evr::menu::RouterOutput;

namespace {

using Kind = RouterEvent::Kind;

constexpr std::size_t kLeft = 0;
constexpr std::size_t kRight = 1;
constexpr std::uint8_t kA = 'A';
constexpr std::uint8_t kD = 'D';

// The Rewards tab as its update hook saw it `since` seconds ago.
PageKeysInput rewards(double since, bool arrows = true) {
    PageKeysInput in;
    in.sinceRewardsUpdate = since;
    in.rewardsArrows = arrows;
    return in;
}

// The mission-complete screen (a popup the game raised) on a 1000 x 500 GUI, with the screen's page keys.
RouterInput screen(double t, PageKeys keys) {
    RouterInput in;
    in.seconds = t;
    in.menuActive = true;
    in.popup = true;
    in.width = 1000;
    in.height = 500;
    in.gameCursor = evr::menu::CursorPixel{500, 250};
    in.pageKeys = keys;
    return in;
}

int count(const RouterOutput& out, Kind kind, std::uint8_t key) {
    int n = 0;
    for (const RouterEvent& e : out.events) {
        n += e.kind == kind && e.key == key ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("page keys: the Rewards tab's arrows while its update runs") {
    // Its update ran a game frame ago with the arrows up: A and D.
    CHECK(pageKeysFor(rewards(0.011)) == kPageArrowKeys);
    CHECK(kPageArrowKeys.previous == kA);
    CHECK(kPageArrowKeys.next == kD);
    CHECK(pageKeysFor(rewards(0.24)).any());
}

TEST_CASE("page keys: none once the tab is gone, without arrows or touch") {
    CHECK_FALSE(pageKeysFor(rewards(0.25)).any()); // no update for a quarter second: another tab or no screen
    CHECK_FALSE(pageKeysFor(rewards(-1.0)).any()); // never seen
    CHECK_FALSE(pageKeysFor(rewards(0.01, false)).any()); // one page: the arrows are hidden
    PageKeysInput guarded = rewards(0.01);
    guarded.allowed = false;
    CHECK_FALSE(pageKeysFor(guarded).any());
    CHECK(pageKeysFor(PageKeysInput{}) == PageKeys{});
}

TEST_CASE("page keys: a visible tab list on A and D while it updates") {
    PageKeysInput in;
    in.sinceTabListUpdate = 0.011;
    CHECK(pageKeysFor(in) == kPageArrowKeys);
    CHECK_FALSE(evr::menu::rewardsShowing(in));
    in.sinceTabListUpdate = 0.3; // the page closed or changed to the map
    CHECK_FALSE(pageKeysFor(in).any());
    in.sinceTabListUpdate = 0.011;
    in.allowed = false;
    CHECK_FALSE(pageKeysFor(in).any());
}

TEST_CASE("router: on the Rewards tab a stick left or right turns its pages, with repeat") {
    MenuRouter router;
    router.update(screen(0.0, kPageArrowKeys));
    RouterInput in = screen(0.1, kPageArrowKeys);
    in.hands[kRight].stick = {0.9f, 0.1f};
    RouterOutput out = router.update(in);
    CHECK(count(out, Kind::KeyDown, kD) == 1);
    CHECK(count(out, Kind::KeyDown, kKeyNextTab) == 0);
    // Held: the next page again after the repeat delay.
    in.seconds = 0.3;
    CHECK(count(router.update(in), Kind::KeyDown, kD) == 0);
    in.seconds = 0.51;
    CHECK(count(router.update(in), Kind::KeyDown, kD) == 1);
    // The other stick, left: the previous page.
    in.hands[kRight].stick = {};
    in.hands[kLeft].stick = {-1.0f, 0.0f};
    in.seconds = 0.7;
    out = router.update(in);
    CHECK(count(out, Kind::KeyDown, kA) == 1);
    CHECK(count(out, Kind::KeyDown, kKeyPreviousTab) == 0);
    in.hands[kLeft].stick = {};
    in.seconds = 0.8;
    CHECK(count(router.update(in), Kind::KeyUp, kA) == 1);
}

TEST_CASE("router: on the Rewards tab the grips still change tabs") {
    MenuRouter router;
    router.update(screen(0.0, kPageArrowKeys));
    RouterInput in = screen(0.1, kPageArrowKeys);
    in.hands[kLeft].grip = 1.0f;
    RouterOutput out = router.update(in);
    CHECK(count(out, Kind::KeyDown, kKeyPreviousTab) == 1);
    CHECK(count(out, Kind::KeyDown, kA) == 0);
    in.hands[kLeft].grip = 0.0f;
    in.hands[kRight].grip = 1.0f;
    in.seconds = 0.3;
    out = router.update(in);
    CHECK(count(out, Kind::KeyDown, kKeyNextTab) == 1);
    CHECK(count(out, Kind::KeyDown, kD) == 0);
}

TEST_CASE("router: without page keys a stick left or right changes tabs, never A or D") {
    MenuRouter router;
    router.update(screen(0.0, PageKeys{}));
    RouterInput in = screen(0.1, PageKeys{});
    in.hands[kRight].stick = {0.9f, 0.0f};
    RouterOutput out = router.update(in);
    CHECK(count(out, Kind::KeyDown, kKeyNextTab) == 1);
    CHECK(count(out, Kind::KeyDown, kD) == 0);
    // The tab goes away under a held stick: the next repeat is a tab key again.
    in.pageKeys = kPageArrowKeys;
    in.hands[kRight].stick = {};
    in.seconds = 0.2;
    router.update(in);
    in.hands[kRight].stick = {0.9f, 0.0f};
    in.seconds = 0.3;
    CHECK(count(router.update(in), Kind::KeyDown, kD) == 1);
    in.pageKeys = PageKeys{};
    in.seconds = 0.71;
    out = router.update(in);
    CHECK(count(out, Kind::KeyDown, kKeyNextTab) == 1);
    CHECK(count(out, Kind::KeyDown, kD) == 0);
}

TEST_CASE("router: on a Dossier page's inner tab list the sticks send A and D and leave the page count") {
    // The Dossier the controllers opened (not a popup) opens on its map page, where the sticks move the map;
    // the right grip goes to the next page, whose tab list is on A / D (the Arsenal's weapon row).
    MenuRouter router;
    RouterInput in = screen(0.0, PageKeys{});
    in.popup = false;
    in.dossier = true;
    router.update(in);
    in.hands[kRight].grip = 1.0f;
    in.seconds = 0.1;
    router.update(in);
    in.hands[kRight].grip = 0.0f;
    in.seconds = 0.2;
    router.update(in);
    const int page = router.dossierPage();
    REQUIRE(page == 1);
    in.pageKeys = kPageArrowKeys;
    in.hands[kRight].stick = {0.9f, 0.0f};
    in.seconds = 0.3;
    RouterOutput out = router.update(in);
    CHECK(count(out, Kind::KeyDown, kD) == 1);
    CHECK(count(out, Kind::KeyDown, kKeyNextTab) == 0);
    CHECK(router.dossierPage() == page); // A / D turn the inner tabs, not the Dossier's pages
    in.hands[kRight].stick = {};
    in.seconds = 0.4;
    router.update(in);
    // A stick click: A or D, not the menus' C.
    in.hands[kRight].stickClick = true;
    in.seconds = 0.5;
    out = router.update(in);
    CHECK(count(out, Kind::KeyDown, kD) == 1);
    CHECK(count(out, Kind::KeyDown, evr::menu::kKeyCentre) == 0);
    CHECK(router.dossierPage() == page);
}

TEST_CASE("router: with page keys the left stick's click turns back, the right stick's on") {
    MenuRouter router;
    router.update(screen(0.0, kPageArrowKeys));
    RouterInput in = screen(0.1, kPageArrowKeys);
    in.hands[kLeft].stickClick = true;
    RouterOutput out = router.update(in);
    CHECK(count(out, Kind::KeyDown, kA) == 1);
    CHECK(count(out, Kind::KeyDown, evr::menu::kKeyUse) == 0); // not the popup's E (the next tab here)
    in.hands[kLeft].stickClick = false;
    in.seconds = 0.2;
    router.update(in);
    in.hands[kRight].stickClick = true;
    in.seconds = 0.3;
    out = router.update(in);
    CHECK(count(out, Kind::KeyDown, kD) == 1);
    // Both sticks pressed: the recenter chord, no key.
    in.hands[kLeft].stickClick = true;
    in.seconds = 0.4;
    out = router.update(in);
    CHECK(count(out, Kind::KeyDown, kA) == 0);
    // Without page keys a stick click is the popup's E again.
    in.hands[kLeft].stickClick = false;
    in.hands[kRight].stickClick = false;
    in.pageKeys = PageKeys{};
    in.seconds = 0.5;
    router.update(in);
    in.hands[kRight].stickClick = true;
    in.seconds = 0.6;
    out = router.update(in);
    CHECK(count(out, Kind::KeyDown, evr::menu::kKeyUse) == 1);
    CHECK(count(out, Kind::KeyDown, kD) == 0);
}
