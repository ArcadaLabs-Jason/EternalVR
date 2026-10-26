#pragma once

// What kind of menu the game's cursor belongs to when it appears (docs/VR_MENUS.md): the router treats a
// popup the game raised by itself (a tutorial or lore popup: A / X sends Space, the gameplay buttons their
// actions' keys) differently from a menu screen the player opened, and opens the Dossier on its map.
//
// The cursor also goes for a moment while one menu changes screens: clicking Settings in the pause menu hid
// it for 0.25 s on the rig. A cursor that comes back that soon belongs to the same menu and keeps its kind
// (the Dossier comes back as a screen: the router would take a new Dossier to be on its map page); deciding
// again then would call the Settings screen a popup (no menu button was pressed in the last moment), and the
// turn stick's gestures would send their gameplay keys (Q, the game's previous tab) into it. A cursor that
// comes up after a map load is a new menu all the same, back within moments or onto a panel still held for
// the menu's backdrop: the load ended the menu that asked for it (MenuChain).
//
// In a play map (not the main menu's shell) a cursor nobody asked for is a popup even when no head-tracked
// frame is behind it. A prompt the game raises right after a cinematic or a load pauses the game before the
// first head-tracked frame, and the frame behind stays the cinematic's or the loading screen's: decided by
// the frame alone it was a screen, A clicked and never sent the Space it waited for (a player's log of
// 2026-10-07, 0.1.31: 58 s of presses at a prompt in e1m1_intro, no Space). The other screens the game
// raises by itself in a play map (the Fortress's and Horde's clickable screens among them) were popups
// already in the players' logs on hand (of 252 cursors nobody asked for in play maps and decided afresh,
// 250 at once and one 0.28 s later; the 252nd is the stuck prompt); the trigger still clicks in them.

#include <cstdint>

namespace evr::menu {

enum class MenuKind {
    Screen,  // a menu screen the player opened (the pause menu, settings), or one without the game behind it
    Popup,   // a popup over the game that nobody asked for
    Dossier, // the Dossier the controllers asked for
};

struct MenuKindInput {
    bool overGame = false;         // the menu shows over the game's view (not a full menu screen)
    bool playMap = false;          // a map the player plays in is loaded (not the main menu's shell)
    bool menuRequested = false;    // the controllers asked for a menu screen a moment ago
    bool dossierRequested = false; // the controllers asked for the Dossier a moment ago
    // The previous menu's cursor went `sinceCursorGone` seconds ago (false: no menu yet).
    bool hadMenu = false;
    MenuKind previous = MenuKind::Screen;
    double sinceCursorGone = 0.0;
    bool mapLoadedSince = false; // a map load came since the previous menu began (MenuChain fills it in)
    // The controllers asked for the Dossier, or for a menu screen, after the previous menu's cursor went:
    // this cursor begins a new menu however short the gap (the pause menu closed, then the Dossier opened at
    // once).
    bool dossierAskedSinceGone = false;
    bool menuAskedSinceGone = false;
};

// The longest gap in the cursor that still counts as the same menu changing screens.
inline constexpr double kMenuContinueSeconds = 0.75;

// The cursor continues the previous menu (the same menu on another screen): it came back within
// `continueSeconds`, no map load came in between and the controllers asked for no menu since it went.
bool continuesMenu(const MenuKindInput& in, double continueSeconds = kMenuContinueSeconds);

MenuKind menuKindOnCursor(const MenuKindInput& in, double continueSeconds = kMenuContinueSeconds);

// A screen nobody asked for (the controllers did not ask for the menu that began the chain of screens) that
// comes to show over the game, or is up in a play map, is a popup. After a checkpoint load the popup's cursor
// came back within kMenuContinueSeconds of the loading screen's, kept its kind (a screen) and was decided
// before the first head-tracked frame, so A clicked and never sent the Space it waited for ("[A] TO DISMISS"
// that no button dismissed, 2026-10-03). A screen of a menu the controllers asked for (the pause menu's
// Settings) stays one.
bool screenBecomesPopup(MenuKind kind, bool chainAsked, bool overGame, bool playMap);

// The chain of screens one menu goes through (the pause menu, then its Settings) from the cursor that began
// it: whether the controllers asked for it, and the map loads counted then (mp_guard::mapLoads). The game
// loads a map while the menu that asked for the load still shows its cursor (156 of 157 loads in the
// players' logs on hand), so the load is compared with the chain's start, not with when a cursor went: after
// Pause, Exit to Main Menu and Continue, a prompt right after the load is decided afresh instead of keeping
// the asked-for pause menu's screen, and a popup's chain carried into the main menu ends there.
class MenuChain {
public:
    struct Step {
        MenuKind kind = MenuKind::Screen;
        bool continues = false;  // the same menu on another screen: its kind kept
        bool keepsPanel = false; // back onto the panel held for the menu's backdrop: it stays where it was
        bool afterLoad = false;  // decided afresh because a map load came since the chain began
    };

    // A cursor comes up. `held`: the panel stayed up since the last cursor went (the menu's backdrop still
    // showed). `in.mapLoadedSince` is set from `mapLoads`. A cursor that does not continue the chain begins a
    // new one.
    Step onCursor(MenuKindInput in, bool held, std::uint32_t mapLoads);

    // screenBecomesPopup for this chain. After a map load the chain counts as nobody's: a cursor that stays
    // up from the menu that asked for the load into the next map is not the asked-for menu any more.
    [[nodiscard]] bool becomesPopup(MenuKind kind, bool overGame, bool playMap, std::uint32_t mapLoads) const;
    [[nodiscard]] bool asked() const { return asked_; }

private:
    bool started_ = false;
    bool asked_ = false;
    std::uint32_t loads_ = 0;
};

// For the log.
const char* menuKindName(MenuKind kind);

} // namespace evr::menu
