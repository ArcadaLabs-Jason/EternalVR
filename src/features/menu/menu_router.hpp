#pragma once

// Controller input while a menu is up (docs/VR_MENUS.md): the laser pointer drives the game's own mouse
// cursor, the trigger (or A / X) clicks, B / Y goes back, the left grip and the right grip change tabs
// (previous, next), a stick pushed up or down scrolls and pushed left or right changes tabs, a stick click
// taps C (the map's centre key), and the controllers' gameplay actions are held back until the menu is gone
// and every control is let go. On the Dossier's map page the sticks move the map instead (map_drag.hpp):
// the weapon hand's stick pans, the other stick zooms (up / down) and rotates (left / right), or the other
// way round with input::MapSticks::OtherPans (ETERNALVR_MAP_STICKS=other). In a popup the
// game raised by itself (a tutorial or lore popup, which waits for Space, E or Left Alt) A / X taps Space, a
// stick click taps E and Y holds Left Alt instead. A tutorial popup waits for the key of the mechanic it
// introduces ("[R]" for the Flame Belch), so there every gameplay action the controllers press also presses
// that action's default key (popupActionKey) for as long as it is held.
//
// The router is a pure state machine run once per XR frame. It decides; the layer carries its decisions
// out through the game's raw input (relative mouse motion, mouse buttons, the wheel and key presses).
//
// The cursor is closed-loop: the game moves its cursor by the relative motion it receives and clamps it
// to its range, so the router reads the game's cursor, sends the difference to where the ray points, and
// sends the next difference only once the game shows the cursor where the last one should have put it
// (or after a short timeout, in case the clamp or a lost event disagreed). A click waits until the cursor
// has settled, so it lands where the ray points.

#include "features/input/axis2.hpp"
#include "features/input/controller_state.hpp"
#include "features/input/map_sticks.hpp"
#include "features/menu/map_drag.hpp"
#include "features/menu/panel_pointer.hpp"
#include "features/menu/wheel_cursor.hpp"
#include "game/eternal/game_action.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace evr::menu {

// Virtual-key codes the router sends (Windows values; the layer passes them through as they are). Escape
// goes back; Q and E are the game's previous and next tab keys in tabbed screens (settings, the Dossier).
// The game's menus do not move their focus with the arrow keys, so none are sent.
inline constexpr std::uint8_t kKeyEscape = 0x1B;
inline constexpr std::uint8_t kKeyPreviousTab = 'Q';
inline constexpr std::uint8_t kKeyNextTab = 'E';
// C centres the Dossier map on the player; the other screens seen so far do not use it.
inline constexpr std::uint8_t kKeyCentre = 'C';
// A tutorial or lore popup closes with Space (continue) or E (use), the keys its prompt shows.
inline constexpr std::uint8_t kKeyContinue = 0x20; // VK_SPACE
inline constexpr std::uint8_t kKeyUse = 'E';
// A popup that asks to pull up the objectives (the Objective Marker tutorial) waits for Left Alt.
inline constexpr std::uint8_t kKeyObjectives = 0xA4; // VK_LMENU

// The key a gameplay action presses in a popup: its default Slayer key (game::defaultKey). None for the
// actions on the mouse (fire and aim are the pointer's), for the pause key, the Dossier and the automap
// (they open a screen of their own; B, Y and the Menu button already go back or pause), and for an action
// whose key is one the popup already has on a button (jump's Space on A / X, melee's E on a stick click,
// mission information's Left Alt on Y), so one press does not send the key twice.
std::optional<std::uint8_t> popupActionKey(game::GameAction action);

// The Dossier's pages in the order its tabs switch (Q / E), starting on the map.
inline constexpr int kDossierPages = 4; // Map, Arsenal, Codex, Challenges

// One notch of the mouse wheel (WHEEL_DELTA).
inline constexpr std::int16_t kWheelNotch = 120;

// Tuning; the defaults are what the rig was checked with.
struct RouterTuning {
    float pressThreshold = 0.55f;   // an analog trigger or grip counts as pressed above this
    float releaseThreshold = 0.35f; // and as released again below this
    float stickThreshold = 0.6f;    // a stick direction counts beyond this
    double repeatDelay = 0.4;       // seconds before a held stick repeats
    double repeatInterval = 0.12;   // seconds between repeats
    double minHold = 0.06;          // a mouse button or key is held at least this long
    double settle = 0.03;           // seconds a click waits after the last cursor move landed
    double moveTimeout = 0.15;      // a move the game has not shown after this long is sent again
    double clickTimeout = 0.25;     // a click waits at most this long for the cursor to settle
    double zoomInterval = 0.1;      // seconds between wheel notches while a stick zooms the map
    MapDragTuning map;              // the map's pan and rotate from the sticks
};

struct RouterHand {
    float trigger = 0.0f;
    float grip = 0.0f;
    input::Axis2 stick;
    bool primary = false;   // A / X
    bool secondary = false; // B / Y
    bool stickClick = false;
    // Where this hand's ray meets the menu panel, if it does.
    std::optional<PanelHit> hit;
    // The hit is on the strip at the top of the screen where tabbed screens have their tabs.
    bool onTabStrip = false;
};

struct RouterInput {
    double seconds = 0.0; // a steady clock
    // The game shows its menu cursor (a menu, the title screen or another GUI-only screen is up).
    bool menuActive = false;
    // The menu is a popup the game raised by itself over the game (no pause, Dossier or mission information
    // was asked for): A / X taps Space instead of clicking, a stick click taps E instead of C.
    bool popup = false;
    // The menu is the Dossier the controllers asked for (it opens on its map page).
    bool dossier = false;
    std::array<RouterHand, 2> hands; // indexed by input::Hand
    // The gameplay actions the controllers hold now, as the control map maps them before a menu holds them
    // back. In a popup each one with a popupActionKey presses that key while it is held.
    game::GameActionSet actions;
    // The game's cursor now, if it could be read, and its range (the GUI's size in pixels).
    std::optional<CursorPixel> gameCursor;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct RouterEvent {
    enum class Kind : std::uint8_t {
        Move,       // relative cursor motion (dx, dy)
        ButtonDown, // the left mouse button
        ButtonUp,
        RightButtonDown, // the right mouse button
        RightButtonUp,
        Wheel, // wheel by `wheel` (positive: away from the user, scrolling up)
        KeyDown,
        KeyUp,
    };
    Kind kind = Kind::Move;
    std::int32_t dx = 0;
    std::int32_t dy = 0;
    std::int16_t wheel = 0;
    std::uint8_t key = 0;
};

// A gameplay action that pressed its key in a popup, the first time it did in that popup (for the log).
struct PopupActionKey {
    game::GameAction action = game::GameAction::Count;
    std::uint8_t key = 0;
};

struct RouterOutput {
    std::vector<RouterEvent> events; // in the order they are sent
    std::vector<PopupActionKey> popupKeys;
    // The controllers' gameplay actions are held back (the menu is up, or it just closed and a control is
    // still held from it).
    bool suppressGameplay = false;
    // The hand the pointer is on, and whether its beam and dot are drawn.
    input::Hand pointerHand = input::Hand::Right;
    bool pointerVisible = false;
    // The Dossier's map page is taken to be up: the sticks move the map.
    bool mapPage = false;
};

class MenuRouter {
public:
    // `mapSticks`: which stick pans the Dossier's map (map_sticks.hpp).
    explicit MenuRouter(input::Hand dominant = input::Hand::Right,
                        input::MapSticks mapSticks = input::MapSticks::WeaponPans,
                        RouterTuning tuning = {});

    RouterOutput update(const RouterInput& in);

    [[nodiscard]] input::Hand pointerHand() const { return pointer_; }
    [[nodiscard]] bool buttonDown() const { return buttonDown_; }
    // The Dossier page the router believes is up (0 the map), or -1 when it cannot tell.
    [[nodiscard]] int dossierPage() const { return page_; }
    // Whether the game's cursor belongs to the weapon wheel rather than a menu (wheel_cursor.hpp); asked
    // before a menu is taken to be up.
    WheelCursor& wheelCursor() { return wheel_; }

private:
    struct Repeater {
        int direction = 0; // the held direction, 0 for none
        double next = 0.0; // when it fires again
    };
    // A direction (-1, 0 or +1) held on one stick axis fires now and then repeats.
    bool repeat(Repeater& r, int direction, double now, double delay, double interval);
    void releaseAll(double now, RouterOutput& out);
    void tapKey(std::uint8_t key, double now, RouterOutput& out);
    void notePage(std::uint8_t key);
    void moveCursor(const RouterInput& in, std::optional<CursorPixel> target, double now, RouterOutput& out);
    void mapSticks(const RouterInput& in, double now, RouterOutput& out);
    void menuSticks(const RouterInput& in, double now, RouterOutput& out);
    void finishKeys(double now, RouterOutput& out);
    // `pressed`: the actions that started this frame.
    void
    popupActions(const RouterInput& in, const game::GameActionSet& pressed, double now, RouterOutput& out);
    void releaseActionKeys(RouterOutput& out);
    // The key is down from a tap, Y's Left Alt or an action in a popup.
    [[nodiscard]] bool keyHeld(std::uint8_t key) const;
    [[nodiscard]] bool allReleased(const RouterInput& in) const;

    input::Hand dominant_;
    input::MapSticks mapSticks_;
    RouterTuning tuning_;
    input::Hand pointer_;
    WheelCursor wheel_;

    bool wasActive_ = false;
    bool latch_ = false; // gameplay held back after the menu closed until every control is let go

    std::array<bool, 2> trigger_{};
    std::array<bool, 2> grip_{};
    std::array<bool, 2> primary_{};
    std::array<bool, 2> secondary_{};
    std::array<bool, 2> stickClick_{};

    // The Dossier page (0 the map; -1 not the Dossier, or not known) and the map's drags.
    int page_ = -1;
    MapDrag drag_;
    std::optional<CursorPixel> dragTarget_;

    // Left Alt, held by Y in a popup.
    bool altDown_ = false;
    double altDownAt_ = 0.0;

    // Gameplay actions in a popup: last frame's held set (a press is one that starts in the popup), the
    // actions whose key is down and since when, and those already logged in this popup.
    game::GameActionSet actions_;
    game::GameActionSet actionKeys_;
    std::array<double, game::kGameActionCount> actionKeyAt_{};
    game::GameActionSet actionsLogged_;

    // Cursor moves in flight.
    bool movePending_ = false;
    CursorPixel expected_;
    double movedAt_ = -1.0;
    double landedAt_ = -1.0;

    // The left button.
    bool clickWanted_ = false;
    double clickWantedAt_ = 0.0;
    bool buttonDown_ = false;
    double buttonDownAt_ = 0.0;
    bool releaseWanted_ = false;

    // Keys tapped (down now, up after minHold).
    struct HeldKey {
        std::uint8_t key = 0;
        double upAt = 0.0;
    };
    std::vector<HeldKey> keys_;

    std::array<Repeater, 2> scroll_{}; // per stick (indexed by input::Hand)
    std::array<Repeater, 2> tabs_{};
};

} // namespace evr::menu
