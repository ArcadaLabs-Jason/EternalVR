#include "features/menu/menu_kind.hpp"

#include <doctest/doctest.h>

#include <limits>

using evr::menu::MenuKind;
using evr::menu::MenuKindInput;
using evr::menu::menuKindOnCursor;

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
