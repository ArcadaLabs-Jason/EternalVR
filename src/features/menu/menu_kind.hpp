#pragma once

// What kind of menu the game's cursor belongs to when it appears (docs/VR_MENUS.md): the router treats a
// popup the game raised by itself (a tutorial or lore popup: A / X sends Space, the gameplay buttons their
// actions' keys) differently from a menu screen the player opened, and opens the Dossier on its map.
//
// The cursor also goes for a moment while one menu changes screens: clicking Settings in the pause menu hid
// it for 0.25 s on the rig. A cursor that comes back that soon belongs to the same menu and keeps its kind
// (the Dossier comes back as a screen: the router would take a new Dossier to be on its map page); deciding
// again then would call the Settings screen a popup (no menu button was pressed in the last moment), and the
// turn stick's gestures would send their gameplay keys (Q, the game's previous tab) into it.

namespace evr::menu {

enum class MenuKind {
    Screen,  // a menu screen the player opened (the pause menu, settings), or one without the game behind it
    Popup,   // a popup over the game that nobody asked for
    Dossier, // the Dossier the controllers asked for
};

struct MenuKindInput {
    bool overGame = false;         // the menu shows over the game's view (not a full menu screen)
    bool menuRequested = false;    // the controllers asked for a menu screen a moment ago
    bool dossierRequested = false; // the controllers asked for the Dossier a moment ago
    // The previous menu's cursor went `sinceCursorGone` seconds ago (false: no menu yet).
    bool hadMenu = false;
    MenuKind previous = MenuKind::Screen;
    double sinceCursorGone = 0.0;
};

// The longest gap in the cursor that still counts as the same menu changing screens.
inline constexpr double kMenuContinueSeconds = 0.75;

MenuKind menuKindOnCursor(const MenuKindInput& in, double continueSeconds = kMenuContinueSeconds);

// A screen nobody asked for (the controllers did not ask for the menu that began the chain of screens) that
// comes to show over the game is a popup. After a checkpoint load the popup's cursor came back within
// kMenuContinueSeconds of the loading screen's, kept its kind (a screen) and was decided before the first
// head-tracked frame, so A clicked and never sent the Space it waited for ("[A] TO DISMISS" that no button
// dismissed, 2026-10-03). A screen of a menu the controllers asked for (the pause menu's Settings) stays one.
bool screenBecomesPopup(MenuKind kind, bool chainAsked, bool overGame);

// For the log.
const char* menuKindName(MenuKind kind);

} // namespace evr::menu
