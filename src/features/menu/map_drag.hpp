#pragma once

// The Dossier map from the thumbsticks (docs/VR_MENUS.md): the game pans its map with a left mouse drag and
// rotates it with a right drag, so a stick held over the map is turned into drags. Each drag is a stroke
// from the middle of the screen: the cursor goes there with the buttons up, the button goes down, the
// cursor moves at the stick's speed, and after a short distance (or when the stick is let go) the button
// goes up and the next stroke starts from the middle again. The cursor never reaches the edge of its range,
// where the game would clamp it and the drag would stop.
//
// While a stroke is going on the drag owns the cursor: the router sends it to `target` instead of where
// the ray points. Pure: time comes from the input.

#include "features/input/axis2.hpp"
#include "features/menu/panel_pointer.hpp"

#include <cstdint>
#include <optional>

namespace evr::menu {

struct MapDragTuning {
    float deadzone = 0.2f;       // stick deflection below this does nothing (the rest is rescaled to 0..1)
    float panSpeed = 0.6f;       // GUI heights per second at full deflection (left drag)
    float rotateSpeed = 0.5f;    // GUI heights per second at full deflection (right drag)
    float strokeLength = 0.2f;   // of the GUI's smaller side: a stroke ends this far from the middle
    double minHold = 0.06;       // a button is held at least this long
    double settleTimeout = 0.25; // a press or release waits at most this long for the cursor
};

enum class DragButton : std::uint8_t {
    Left,  // pans the map
    Right, // rotates it
};

struct DragEvent {
    DragButton button = DragButton::Left;
    bool down = false;
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
    std::optional<CursorPixel> target; // where the cursor must go; nullopt: the ray has it
    std::optional<DragEvent> event;    // a button to press or release now
};

// A stick deflection with the deadzone taken out and the rest rescaled, so the speed starts from 0.
input::Axis2 withDeadzone(input::Axis2 stick, float deadzone);

class MapDrag {
public:
    explicit MapDrag(MapDragTuning tuning = {});

    MapDragOutput update(const MapDragInput& in);

    // The drag's button, if it is down (the router releases it when the menu closes); the drag is reset.
    std::optional<DragEvent> reset();

    [[nodiscard]] bool active() const { return phase_ != Phase::Idle; }
    [[nodiscard]] bool buttonDown() const { return phase_ == Phase::Dragging || phase_ == Phase::Releasing; }

private:
    enum class Phase : std::uint8_t {
        Idle,
        Seeking,   // the cursor goes to the middle with the button up
        Dragging,  // the button is down and the cursor moves
        Releasing, // the cursor waits for its last move, then the button goes up
    };

    MapDragTuning tuning_;
    Phase phase_ = Phase::Idle;
    DragButton button_ = DragButton::Left;
    double last_ = -1.0;
    double phaseStart_ = 0.0;
    double downAt_ = 0.0;
    CursorPixel anchor_;
    CursorPixel target_;
    float x_ = 0.0f;
    float y_ = 0.0f;
};

} // namespace evr::menu
