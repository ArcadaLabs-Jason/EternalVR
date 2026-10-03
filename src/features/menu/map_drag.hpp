#pragma once

// The Dossier map from the thumbsticks (docs/VR_MENUS.md). The game's automap pans with a left drag and
// rotates with a right drag, and it adds up the raw mouse motion that arrives while the button is held
// (idAutomap::HandleEvent, docs/rig-findings/menus.md section 6), not the cursor's position: the motion still
// counts with the cursor held at the edge of its range. So a stick held over the map holds one button down
// for as long as it is off centre and streams the stick's motion: the cursor goes to the middle of the
// screen with the buttons up, the button goes down, and it stays down until the stick is let go. Before,
// each drag was a short stroke from the middle (release, back to the middle, press again, three times a
// second at full deflection), which the owner felt as the map stopping and starting (2026-10-02).
//
// With both buttons held every motion pans, so a drag pans or rotates, never both. While the rotate stick is
// held the pan stick pans with W, A, S and D instead (map_pan_keys.hpp), which the automap reads on their
// own: both sticks work at once. Once a pan has gone onto the keys it stays there until its stick is let go,
// so the drag does not switch back and forth. Every press is made with the cursor in the middle of the
// screen, as before (a press goes to the Dossier's screen too).
//
// While a button is down the drag owns the cursor: the router sends no move of its own, and the ray has the
// cursor again once the stick is let go. Pure: time comes from the input.

#include "features/input/axis2.hpp"
#include "features/menu/map_pan_keys.hpp"
#include "features/menu/panel_pointer.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace evr::menu {

struct MapDragTuning {
    float deadzone = 0.2f;       // stick deflection below this does nothing (the rest is rescaled to 0..1)
    float releaseZone = 0.15f;   // a drag going on ends only once the stick is back within this
    float rotateStart = 0.3f;    // the rotate stick's left / right starts a rotation beyond this
    float panSpeed = 1000.0f;    // mouse counts per second at full deflection (left drag)
    float rotateSpeed = 1100.0f; // mouse counts per second at full deflection (right drag)
    double minHold = 0.06;       // a button is held at least this long
    double settleTimeout = 0.25; // a press waits at most this long for the cursor to reach the middle
};

enum class DragButton : std::uint8_t {
    Left,  // pans the map
    Right, // rotates it
};

// What the drag sends, in order.
struct MapEvent {
    enum class Kind : std::uint8_t {
        Button, // `button` down or up
        Key,    // `key` (map_pan_keys.hpp) down or up
        Move,   // relative motion (dx, dy)
    };
    Kind kind = Kind::Move;
    DragButton button = DragButton::Left;
    std::uint8_t key = 0;
    bool down = false;
    std::int32_t dx = 0;
    std::int32_t dy = 0;
};

struct MapDragInput {
    double seconds = 0.0;
    input::Axis2 pan;  // the stick that pans (+x right, +y away from the player)
    input::Axis2 turn; // the stick whose left / right rotates (its up / down is the router's zoom)
    std::optional<CursorPixel> cursor; // the game's cursor now
    bool cursorIdle = false;           // no cursor move is in flight
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct MapDragOutput {
    std::optional<CursorPixel> target; // where the router moves the cursor (the middle, before a press)
    std::vector<MapEvent> events;      // buttons, keys and the drag's motion, sent in this order
};

// How the pan stick pans now.
enum class MapPanBy : std::uint8_t {
    None,
    Drag, // the left button
    Keys, // W, A, S, D
};

// A stick deflection with the deadzone taken out and the rest rescaled, so the speed starts from 0.
input::Axis2 withDeadzone(input::Axis2 stick, float deadzone);

class MapDrag {
public:
    explicit MapDrag(MapDragTuning tuning = {});

    MapDragOutput update(const MapDragInput& in);

    // The button and keys still down (the router releases them when the menu closes or the page changes);
    // the drag is reset.
    std::vector<MapEvent> reset();

    // A stick moves the map (the drag owns the cursor, or the keys pan).
    [[nodiscard]] bool active() const { return phase_ != Phase::Idle || panBy_ != MapPanBy::None; }
    [[nodiscard]] bool buttonDown() const { return phase_ == Phase::Down; }
    [[nodiscard]] MapPanBy panBy() const { return panBy_; }
    [[nodiscard]] bool rotating() const { return rotate_; }

private:
    enum class Phase : std::uint8_t {
        Idle,
        Seeking, // the cursor goes to the middle with the buttons up
        Down,    // the button is down and the stick's motion is sent
    };

    void sendKeys(MapKeys keys, MapDragOutput& out);
    void release(MapDragOutput& out);

    MapDragTuning tuning_;
    Phase phase_ = Phase::Idle;
    DragButton button_ = DragButton::Left;
    MapPanBy panBy_ = MapPanBy::None;
    bool rotate_ = false;
    double last_ = -1.0;
    double seekStart_ = 0.0;
    double downAt_ = 0.0;
    float restX_ = 0.0f; // motion not yet sent (less than a count)
    float restY_ = 0.0f;
    MapPanKeys keyPan_;
    MapKeys keys_;
};

} // namespace evr::menu
