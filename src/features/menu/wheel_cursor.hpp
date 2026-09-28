#pragma once

// The weapon wheel is not a menu (docs/VR_MENUS.md, "The weapon wheel").
//
// The game's wheel selects with its menu cursor (features/input/wheel_mouse.hpp). Should the game also show
// that cursor while the wheel is up, the menu router must not take it for a menu: no panel, no pointer and
// no hold on gameplay input, or the stick's selection and the release that picks the weapon would never
// reach the game. A cursor that appears while the wheel button (the weapon_wheel action) is held and no menu
// is up belongs to the wheel until it goes, or until the button has been up for `releaseGraceSeconds` (the
// game closes the wheel on the release; a cursor still shown after that is a menu of its own and the router
// takes it as a new one).
//
// Pure; the presenter asks once per XR frame, before it decides whether a menu is up.

namespace evr::menu {

struct WheelCursorInput {
    double seconds = 0.0;     // a steady clock
    bool cursorShown = false; // the game shows its menu cursor (and menus may be touched)
    bool menuUp = false;      // the router already has a menu (its panel is up or held)
    bool wheelHeld = false;   // the controllers hold the weapon_wheel action
};

struct WheelCursorOutput {
    bool owned = false;   // the cursor is the wheel's: no menu mode for it
    bool started = false; // it became the wheel's this frame
    bool ended = false;   // it stopped being the wheel's this frame
};

class WheelCursor {
public:
    explicit WheelCursor(double releaseGraceSeconds = 0.3) : grace_(releaseGraceSeconds) {}

    WheelCursorOutput update(const WheelCursorInput& in);

    [[nodiscard]] bool owned() const { return owned_; }

private:
    double grace_;
    bool owned_ = false;
    bool wasShown_ = false;
    double releasedAt_ = -1.0;
};

} // namespace evr::menu
