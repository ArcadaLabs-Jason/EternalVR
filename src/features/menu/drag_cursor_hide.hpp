#pragma once

// Whether the game's cursor is hidden while a stick drags the Dossier map (map_drag.hpp, docs/VR_MENUS.md).
// A drag moves the game's own cursor: to the middle of the screen, with the stick's motion (to the edge of
// its range, where it stays while the drag goes on), and at the end back to where the ray points. Shown, that
// cursor jumps about while the map pans; the layer hides it for as long as a drag owns it (the menu stays in
// mouse mode throughout).
//
// Hidden from the first frame a drag owns the cursor until the drag ends, the cursor has gone back to the
// ray (no move in flight) and `linger` has passed, so the jump back to the ray does not flash the cursor;
// never longer than `maxHold` after the drag. Shown at once when the menu goes. Pure: time comes from the
// input.

namespace evr::menu {

struct DragCursorHideTuning {
    double linger = 0.15; // seconds the cursor stays hidden after the drag
    double maxHold = 0.5; // and at most this long, should the cursor never settle
};

struct DragCursorHideInput {
    double seconds = 0.0;
    bool menuActive = false;     // the router has a menu up
    bool dragOwnsCursor = false; // a stick moves the map (MapDrag::active)
    bool cursorSettled = false;  // no cursor move is in flight
};

class DragCursorHide {
public:
    explicit DragCursorHide(DragCursorHideTuning tuning = {});

    // Whether the cursor is hidden this frame.
    bool update(const DragCursorHideInput& in);
    void reset();

    [[nodiscard]] bool hidden() const { return hidden_; }

private:
    DragCursorHideTuning tuning_;
    bool hidden_ = false;
    double releasedAt_ = -1.0; // when the drag ended, while still hidden
};

} // namespace evr::menu
