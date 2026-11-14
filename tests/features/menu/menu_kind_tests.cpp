#include "features/menu/menu_kind.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <limits>

using evr::menu::continuesMenu;
using evr::menu::MenuChain;
using evr::menu::MenuKind;
using evr::menu::MenuKindInput;
using evr::menu::menuKindOnCursor;
using evr::menu::padMenuOverGame;
using evr::menu::screenBecomesPopup;

TEST_CASE("a menu asked for is a screen, one over the game nobody asked for is a popup") {
    MenuKindInput in;
    in.overGame = true;
    in.menuRequested = true;
    CHECK(menuKindOnCursor(in) == MenuKind::Screen);
    in.menuRequested = false;
    CHECK(menuKindOnCursor(in) == MenuKind::Popup);
    in.overGame = false; // a full menu screen (the title screen) is never a popup
    CHECK(menuKindOnCursor(in) == MenuKind::Screen);
}

TEST_CASE("the Dossier asked for opens as the Dossier") {
    MenuKindInput in;
    in.overGame = true;
    in.menuRequested = true;
    in.dossierRequested = true;
    CHECK(menuKindOnCursor(in) == MenuKind::Dossier);
}

TEST_CASE("the pause menu opening Settings keeps its kind across the cursor's short gap") {
    // The rig: the cursor went at 463.42 s when Settings was clicked and came back at 463.67 s, over the
    // game, 97 s after the menu button: before this rule the Settings screen was taken for a popup.
    MenuKindInput in;
    in.overGame = true;
    in.hadMenu = true;
    in.previous = MenuKind::Screen;
    in.sinceCursorGone = 0.25;
    CHECK(menuKindOnCursor(in) == MenuKind::Screen);
}

TEST_CASE("a popup's next page stays a popup; the Dossier on another screen is a screen") {
    MenuKindInput in;
    in.overGame = true;
    in.hadMenu = true;
    in.sinceCursorGone = 0.3;
    in.previous = MenuKind::Popup;
    CHECK(menuKindOnCursor(in) == MenuKind::Popup);
    // Not the Dossier again: the router would take it to be on its map page.
    in.previous = MenuKind::Dossier;
    in.dossierRequested = true;
    CHECK(menuKindOnCursor(in) == MenuKind::Screen);
}

TEST_CASE("after a longer gap the menu is decided again") {
    MenuKindInput in;
    in.overGame = true;
    in.hadMenu = true;
    in.previous = MenuKind::Screen;
    in.sinceCursorGone = 0.75;
    CHECK(menuKindOnCursor(in) == MenuKind::Popup);
    in.sinceCursorGone = 5.0;
    CHECK(menuKindOnCursor(in) == MenuKind::Popup);
    in.hadMenu = false; // no menu before: a small time since means nothing
    in.sinceCursorGone = 0.1;
    CHECK(menuKindOnCursor(in) == MenuKind::Popup);
}

TEST_CASE("a clock going backwards or a bad time decides again") {
    MenuKindInput in;
    in.overGame = true;
    in.hadMenu = true;
    in.previous = MenuKind::Screen;
    in.sinceCursorGone = -0.1;
    CHECK(menuKindOnCursor(in) == MenuKind::Popup);
    in.sinceCursorGone = std::numeric_limits<double>::quiet_NaN();
    CHECK(menuKindOnCursor(in) == MenuKind::Popup);
}

TEST_CASE("a screen nobody asked for becomes a popup once it shows over the game") {
    // A checkpoint load: the popup kept the loading screen's kind (a screen) before any head-tracked frame.
    CHECK(screenBecomesPopup(MenuKind::Screen, false, true, false));
    CHECK_FALSE(screenBecomesPopup(MenuKind::Screen, false, false, false)); // still a full menu screen
    // The pause menu's Settings shows over the game too: its chain was asked for.
    CHECK_FALSE(screenBecomesPopup(MenuKind::Screen, true, true, false));
    CHECK_FALSE(screenBecomesPopup(MenuKind::Dossier, false, true, false));
    CHECK_FALSE(screenBecomesPopup(MenuKind::Popup, false, true, false));
}

TEST_CASE("a cursor nobody asked for in a play map is a popup without a head-tracked frame behind it") {
    // A prompt raised right after a cinematic or a load pauses the game before its first head-tracked frame
    // (a player's log, 0.1.31: taken for a screen, A clicked for 58 s and never sent Space).
    MenuKindInput in;
    in.overGame = false;
    in.playMap = true;
    CHECK(menuKindOnCursor(in) == MenuKind::Popup);
    // Asked for, it stays a screen (the pause menu over a cinematic's frame) or the Dossier.
    in.menuRequested = true;
    CHECK(menuKindOnCursor(in) == MenuKind::Screen);
    in.dossierRequested = true;
    CHECK(menuKindOnCursor(in) == MenuKind::Dossier);
}

TEST_CASE("outside a play map a full menu screen nobody asked for stays a screen") {
    // The title screen and the main menu (the shell) show the game's whole frame and click with A.
    MenuKindInput in;
    in.overGame = false;
    in.playMap = false;
    CHECK(menuKindOnCursor(in) == MenuKind::Screen);
    CHECK_FALSE(screenBecomesPopup(MenuKind::Screen, false, false, false));
}

TEST_CASE("a screen nobody asked for in a play map becomes a popup without showing over the game") {
    // The cursor came back within moments of a screen's (a loading screen's) and kept its kind.
    CHECK(screenBecomesPopup(MenuKind::Screen, false, false, true));
    // A screen of a menu the controllers asked for stays one in a play map too.
    CHECK_FALSE(screenBecomesPopup(MenuKind::Screen, true, false, true));
    CHECK_FALSE(screenBecomesPopup(MenuKind::Dossier, false, false, true));
}

TEST_CASE("a cursor back after a map load is a new menu") {
    MenuKindInput in;
    in.playMap = true;
    in.hadMenu = true;
    in.previous = MenuKind::Screen;
    in.sinceCursorGone = 0.2;
    CHECK(continuesMenu(in));
    CHECK(menuKindOnCursor(in) == MenuKind::Screen);
    // The load ended the menu before (the pause menu's Load Checkpoint): decided again, a popup.
    in.mapLoadedSince = true;
    CHECK_FALSE(continuesMenu(in));
    CHECK(menuKindOnCursor(in) == MenuKind::Popup);
}

// The chain, fed in the order the game produces: the map-load hook fires while the menu that asked for the
// load still shows its cursor; that cursor goes; the next one comes up after the load.
namespace {

MenuKindInput cursorUp(bool playMap, bool overGame, bool asked, MenuKind previous, double sinceGone) {
    MenuKindInput in;
    in.playMap = playMap;
    in.overGame = overGame;
    in.menuRequested = asked;
    in.hadMenu = sinceGone >= 0.0;
    in.previous = previous;
    in.sinceCursorGone = sinceGone;
    return in;
}

} // namespace

TEST_CASE("chain: Pause, Exit to Main Menu, Continue: a prompt right after the load is a popup") {
    MenuChain chain;
    std::uint32_t loads = 3;
    // The pause menu, asked for, over the game.
    MenuChain::Step step = chain.onCursor(cursorUp(true, true, true, MenuKind::Screen, -1.0), false, loads);
    CHECK(step.kind == MenuKind::Screen);
    CHECK(chain.asked());
    // Exit to Main Menu: the shell loads while the pause menu's cursor is up; the main menu's cursor comes
    // back 0.4 s after it went, nobody asked for it.
    ++loads;
    step = chain.onCursor(cursorUp(false, false, false, MenuKind::Screen, 0.4), false, loads);
    CHECK(step.afterLoad);
    CHECK_FALSE(step.continues);
    CHECK(step.kind == MenuKind::Screen); // the main menu clicks with A
    CHECK_FALSE(chain.asked());
    // Continue: the campaign map loads while the main menu's cursor is up; the main menu becomes a popup
    // until its cursor goes.
    ++loads;
    CHECK(chain.becomesPopup(MenuKind::Screen, false, true, loads));
    // A prompt 0.3 s after the cursor went, before any head-tracked frame.
    step = chain.onCursor(cursorUp(true, false, false, MenuKind::Screen, 0.3), false, loads);
    CHECK(step.afterLoad);
    CHECK(step.kind == MenuKind::Popup);
}

TEST_CASE("chain: Load Checkpoint from the pause menu: a prompt right after the load is a popup") {
    MenuChain chain;
    std::uint32_t loads = 7;
    chain.onCursor(cursorUp(true, true, true, MenuKind::Screen, -1.0), false, loads);
    CHECK_FALSE(chain.becomesPopup(MenuKind::Screen, false, true, loads)); // asked for, before the load
    ++loads; // the checkpoint's map load, the pause menu's cursor still up
    CHECK(chain.becomesPopup(MenuKind::Screen, false, true, loads));
    const MenuChain::Step step =
        chain.onCursor(cursorUp(true, false, false, MenuKind::Screen, 0.3), false, loads);
    CHECK(step.kind == MenuKind::Popup);
}

TEST_CASE("chain: the held panel keeps its menu, unless a map load came since the menu began") {
    MenuChain chain;
    std::uint32_t loads = 2;
    // A pause menu opened from the keyboard: nobody's, over the game, a popup.
    MenuChain::Step step = chain.onCursor(cursorUp(true, true, false, MenuKind::Screen, -1.0), false, loads);
    REQUIRE(step.kind == MenuKind::Popup);
    // Its cursor goes and comes back onto the held panel.
    step = chain.onCursor(cursorUp(true, false, false, MenuKind::Popup, 0.5), true, loads);
    CHECK(step.keepsPanel);
    CHECK(step.kind == MenuKind::Popup);
    // Exit to Main Menu: the shell loads while its cursor is up, then the main menu's comes back onto the
    // held panel: a new menu, a screen.
    ++loads;
    step = chain.onCursor(cursorUp(false, false, false, MenuKind::Popup, 0.5), true, loads);
    CHECK_FALSE(step.keepsPanel);
    CHECK(step.afterLoad);
    CHECK(step.kind == MenuKind::Screen);
}

TEST_CASE("chain: the pause menu's Settings stays the asked-for menu") {
    MenuChain chain;
    chain.onCursor(cursorUp(true, true, true, MenuKind::Screen, -1.0), false, 4);
    const MenuChain::Step step =
        chain.onCursor(cursorUp(true, true, false, MenuKind::Screen, 0.25), false, 4);
    CHECK(step.continues);
    CHECK(step.kind == MenuKind::Screen);
    CHECK(chain.asked());
    CHECK_FALSE(chain.becomesPopup(MenuKind::Screen, true, true, 4));
}

TEST_CASE("the Dossier asked for right after another menu's cursor went opens as the Dossier") {
    // A headset log (0.1.50): the pause menu closed at 581.36 s and Hold X asked for the Dossier at 582.06 s;
    // its cursor came 0.72 s after the last one went and was taken for the pause menu on another screen, so
    // the sticks scrolled and changed tabs instead of moving the map.
    MenuKindInput in;
    in.overGame = true;
    in.playMap = true;
    in.menuRequested = true;
    in.dossierRequested = true;
    in.hadMenu = true;
    in.previous = MenuKind::Screen;
    in.sinceCursorGone = 0.72;
    CHECK(menuKindOnCursor(in) == MenuKind::Screen); // not asked since the cursor went: the same menu
    in.dossierAskedSinceGone = true;
    in.menuAskedSinceGone = true;
    CHECK_FALSE(continuesMenu(in));
    CHECK(menuKindOnCursor(in) == MenuKind::Dossier);
    // After a popup too (a tutorial dismissed, then the map opened at once).
    in.previous = MenuKind::Popup;
    in.sinceCursorGone = 0.2;
    CHECK(menuKindOnCursor(in) == MenuKind::Dossier);
}

TEST_CASE("a menu asked for right after a popup's cursor went is a screen, not the popup's next page") {
    MenuKindInput in;
    in.overGame = true;
    in.playMap = true;
    in.menuRequested = true;
    in.hadMenu = true;
    in.previous = MenuKind::Popup;
    in.sinceCursorGone = 0.3;
    CHECK(menuKindOnCursor(in) == MenuKind::Popup);
    in.menuAskedSinceGone = true;
    CHECK(menuKindOnCursor(in) == MenuKind::Screen);
}

TEST_CASE("chain: a menu asked for since the last cursor went begins a new chain, held panel or not") {
    MenuChain chain;
    chain.onCursor(cursorUp(true, true, false, MenuKind::Screen, -1.0), false, 2); // a popup nobody asked for
    CHECK_FALSE(chain.asked());
    MenuKindInput in = cursorUp(true, true, true, MenuKind::Popup, 0.3);
    in.dossierRequested = true;
    in.dossierAskedSinceGone = true;
    in.menuAskedSinceGone = true;
    for (const bool held : std::array<bool, 2>{false, true}) {
        const MenuChain::Step step = chain.onCursor(in, held, 2);
        CHECK_FALSE(step.continues);
        CHECK_FALSE(step.keepsPanel);
        CHECK(step.kind == MenuKind::Dossier);
        CHECK(chain.asked());
    }
}

TEST_CASE("gamepad mode: a menu the pad navigates is told by the backdrop over a head-tracked frame") {
    // The Sentinel Crystal's, the rune's and the mod bot's menus with a pad (rig, 2026-11-12, runs pa1 and
    // pa2): no cursor, the GUI's backdrop up over the head-tracked view.
    CHECK(padMenuOverGame(true, false, true, true, false));
    // No backdrop (play), or a loading screen behind it (no head-tracked frame): not a menu over the game.
    CHECK_FALSE(padMenuOverGame(true, false, false, true, false));
    CHECK_FALSE(padMenuOverGame(true, false, true, false, false));
    // A menu the cursor shows (a mouse in gamepad mode) is the cursor's already.
    CHECK_FALSE(padMenuOverGame(true, true, true, true, false));
    // With the motion controllers on every menu shows the cursor: a backdrop without one is a fade.
    CHECK_FALSE(padMenuOverGame(false, false, true, true, false));
}

TEST_CASE("gamepad mode: the mod bot's menu is told by its 3D model, without a backdrop") {
    // CHOOSE A WEAPON MOD with a pad: the weapon's model in its middle, no backdrop round the crosshair, the
    // model placed every tick.
    CHECK(padMenuOverGame(true, false, false, true, true));
    // Over a loading screen, under the cursor's menu, or with the motion controllers on: as for the backdrop.
    CHECK_FALSE(padMenuOverGame(true, false, false, false, true));
    CHECK_FALSE(padMenuOverGame(true, true, false, true, true));
    CHECK_FALSE(padMenuOverGame(false, false, false, true, true));
}

TEST_CASE("gamepad mode's menu without a cursor: up after half a second, not for a fade, capped, and new "
          "after a load") {
    using evr::menu::PadMenuWatch;
    PadMenuWatch watch;
    watch.update(false, 0.0, 1); // the map loaded at 0
    // The fade in after a load (1.4 to 1.7 s on the rig) is not a menu.
    CHECK_FALSE(watch.update(true, 1.0, 1).up);
    CHECK_FALSE(watch.update(true, 2.2, 1).up);
    CHECK_FALSE(watch.update(false, 2.3, 1).up);
    double t = 10.0;
    // A fade (a checkpoint's, a death's) shorter than the settle: never up.
    CHECK_FALSE(watch.update(true, t, 1).up);
    CHECK_FALSE(watch.update(true, t + 0.4, 1).up);
    CHECK_FALSE(watch.update(false, t + 0.45, 1).up);
    // A menu: up once it held half a second, down when it goes.
    t = 20.0;
    CHECK_FALSE(watch.update(true, t, 1).up);
    CHECK_FALSE(watch.update(true, t + 0.3, 1).up);
    CHECK(watch.update(true, t + 0.6, 1).up);
    CHECK(watch.update(true, t + 100.0, 1).up);
    CHECK_FALSE(watch.update(false, t + 100.1, 1).up);
    // A map load while it shows starts the wait again.
    t = 200.0;
    watch.update(true, t, 1);
    CHECK(watch.update(true, t + 1.0, 1).up);
    CHECK_FALSE(watch.update(true, t + 1.1, 2).up);
    CHECK_FALSE(watch.update(true, t + 1.7, 2).up); // within kAfterLoadSeconds of the load
    watch.update(true, t + 1.1 + PadMenuWatch::kAfterLoadSeconds, 2);
    CHECK(watch.update(true, t + 1.7 + PadMenuWatch::kAfterLoadSeconds, 2).up);
    // Capped after kMaxSeconds up, and down until it goes and comes back.
    const PadMenuWatch::Step capped = watch.update(
        true,
        t + 1.2 + PadMenuWatch::kAfterLoadSeconds + PadMenuWatch::kSettleSeconds + PadMenuWatch::kMaxSeconds,
        2);
    CHECK(capped.capped);
    CHECK_FALSE(capped.up);
    CHECK_FALSE(watch.update(true, t + 400.0, 2).up);
    CHECK_FALSE(watch.update(false, t + 401.0, 2).up);
    watch.update(true, t + 402.0, 2);
    CHECK(watch.update(true, t + 402.6, 2).up);
}

TEST_CASE("gamepad mode: the Dossier open is a menu over the game, its map and CODEX pages included") {
    using evr::menu::padDossierOverGame;
    // The map page (the 3D automap) and the CODEX page (a black frame): no backdrop, no model, the Dossier
    // open (rig, 2026-11-13, run x4 of pad-model-menus).
    CHECK(padDossierOverGame(true, false, true, true));
    // Closed (play), or nothing head-tracked behind it, or a cursor's menu, or the motion controllers on (the
    // Dossier shows the cursor then): not this rule's.
    CHECK_FALSE(padDossierOverGame(true, false, false, true));
    CHECK_FALSE(padDossierOverGame(true, false, true, false));
    CHECK_FALSE(padDossierOverGame(true, true, true, true));
    CHECK_FALSE(padDossierOverGame(false, false, true, true));
}

TEST_CASE("the Dossier's open and close calls: open from an open until the close or a map load") {
    using evr::menu::dossierOpen;
    // No hooks (another game build) or no open yet.
    CHECK_FALSE(dossierOpen(0, false, 0, 1));
    CHECK_FALSE(dossierOpen(0, true, 0, 1));
    // Open (in map 1), then the close.
    CHECK(dossierOpen(1, true, 1, 1));
    CHECK_FALSE(dossierOpen(1, false, 1, 1));
    // Opened again: the page changes call no open.
    CHECK(dossierOpen(2, true, 1, 1));
    // A map load with no close seen: closed, and stays so until the next open.
    CHECK_FALSE(dossierOpen(2, true, 1, 2));
    // A second open with no close, after that load: a new open, held.
    CHECK(dossierOpen(3, true, 2, 2));
    // Its own map load ends it too.
    CHECK_FALSE(dossierOpen(3, true, 2, 3));
    CHECK(dossierOpen(4, true, 3, 3));
    // An open and its close between two frames: closed.
    CHECK_FALSE(dossierOpen(5, false, 3, 3));
    // A load between the open call and the frame that reads it: the open's own count of loads is older, so it
    // is not held into the new map.
    CHECK_FALSE(dossierOpen(6, true, 3, 4));
}

TEST_CASE("gamepad mode: the Dossier holds at once, map load or not, and lets go as it closes") {
    using evr::menu::PadMenuWatch;
    PadMenuWatch watch;
    watch.update(false, 0.0, 1, false);
    // The Dossier opened on its map (run x4: no backdrop, no model): up in the frame it opened, the look
    // stick pushed under it 4 s later.
    double t = 70.0;
    CHECK_FALSE(watch.update(false, t - 0.01, 1, false).up);
    CHECK(watch.update(false, t, 1, true).up);
    CHECK(watch.update(false, t + 4.0, 1, true).up);
    // B closes it: down in the frame the close came.
    CHECK_FALSE(watch.update(false, t + 5.8, 1, false).up);
    // Opened on the way to CODEX: the Arsenal's backdrop and model, then the CODEX page with neither.
    t = 157.0;
    CHECK(watch.update(true, t, 1, true).up);
    CHECK(watch.update(true, t + 3.0, 1, true).up);
    CHECK(watch.update(false, t + 6.0, 1, true).up);
    CHECK_FALSE(watch.update(false, t + 14.4, 1, false).up);
    // Closed while its backdrop still fades (the backdrop test's off readings): held to the backdrop's end,
    // as for any menu without a cursor.
    t = 300.0;
    CHECK(watch.update(true, t, 1, true).up);
    CHECK(watch.update(true, t + 1.0, 1, true).up);
    CHECK(watch.update(true, t + 1.1, 1, false).up);
    CHECK_FALSE(watch.update(false, t + 1.3, 1, false).up);
    // Within kAfterLoadSeconds of a load the Dossier holds all the same (no fade to wait out); a backdrop
    // alone there does not.
    t = 400.0;
    watch.update(false, t, 2, false);
    CHECK_FALSE(watch.update(true, t + 1.0, 2, false).up);
    CHECK_FALSE(watch.update(true, t + 1.6, 2, false).up);
    CHECK(watch.update(true, t + 2.0, 2, true).up);
    CHECK_FALSE(watch.update(false, t + 2.5, 2, false).up);
    // A death's fade after it: not held (shorter than the settle).
    CHECK_FALSE(watch.update(true, t + 20.0, 2, false).up);
    CHECK_FALSE(watch.update(true, t + 20.4, 2, false).up);
    CHECK_FALSE(watch.update(false, t + 20.5, 2, false).up);
    // Not capped: CODEX read for longer than kMaxSeconds stays held until the close.
    t = 1000.0;
    CHECK(watch.update(false, t, 2, true).up);
    const PadMenuWatch::Step long1 =
        watch.update(false, t + PadMenuWatch::kSettleSeconds + PadMenuWatch::kMaxSeconds, 2, true);
    CHECK(long1.up);
    CHECK_FALSE(long1.capped);
    CHECK(watch.update(false, t + 900.0, 2, true).up);
    CHECK_FALSE(watch.update(false, t + 901.0, 2, false).up);
    // Its backdrop's tail after a long read is held as before, the cap counted from the Dossier's close.
    t = 2000.0;
    CHECK(watch.update(true, t, 2, true).up);
    CHECK(watch.update(true, t + 600.0, 2, true).up);
    CHECK(watch.update(true, t + 600.2, 2, false).up);
    CHECK_FALSE(watch.update(false, t + 600.4, 2, false).up);
    // A backdrop with no Dossier is still capped after kMaxSeconds, until it goes.
    t = 3000.0;
    watch.update(true, t, 2, false);
    CHECK(watch.update(true, t + PadMenuWatch::kMaxSeconds, 2, false).up);
    const PadMenuWatch::Step capped =
        watch.update(true, t + PadMenuWatch::kSettleSeconds + PadMenuWatch::kMaxSeconds, 2, false);
    CHECK(capped.capped);
    CHECK_FALSE(capped.up);
    CHECK_FALSE(watch.update(true, t + 400.0, 2, false).up);
}
