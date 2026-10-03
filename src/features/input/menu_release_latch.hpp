#pragma once

// Inputs still held when a menu lets go of gameplay input (docs/VR_MENUS.md, "Gameplay held back"): each
// stays out of gameplay until it is let go.
//
// While a menu is up the controllers drive the menu and the mapper's actions are dropped. A control used
// there is often still down when the menu goes: B or Y backs out of the pause menu, the trigger clicks
// Resume, a stick held to scroll or to pan the Dossier's map is still out. Passed on, the release of that B
// would complete its tap binding (switch the weapon mod), and the stick would carry on as a sweep (the
// chainsaw, the weapon wheel, a quick switch).
//
// So every input down on the last frame of the hold is latched: from the first frame after it, it reads as
// released (a stick as centred) until it is actually let go (a stick back within `stickCentre`). Each input
// is let go on its own, and one up when the hold ends passes straight through, so a fresh press works at
// once. `holdEnded` marks that first frame: the mapper uses up the presses and sweeps begun under the menu
// there, since one let go on that very frame would otherwise still finish them.
//
// Pure: no clock.

#include "features/input/analog_button.hpp"
#include "features/input/binding_profile.hpp"
#include "features/input/controller_state.hpp"

#include <array>

namespace evr::input {

// An analog value above its threshold, or a stick further out than `stickCentre`, is held.
struct HeldThresholds {
    float trigger = kTriggerThresholds.release;
    float grip = kGripThresholds.release;
    float stickCentre = 0.25f;
};

struct MenuReleaseOutput {
    InputFrame frame;       // the input, with every latched input released or centred
    bool holdEnded = false; // the first frame after a hold
};

class MenuReleaseLatch {
public:
    explicit MenuReleaseLatch(HeldThresholds thresholds = {}) : thresholds_(thresholds) {}

    // `holding`: a menu holds the controllers' gameplay input back this frame. Its frames pass unchanged
    // (the menu reads them); only what is held when it ends is latched.
    MenuReleaseOutput update(const InputFrame& in, bool holding);

    // True while any input is latched.
    [[nodiscard]] bool anyLatched() const;

private:
    struct HandLatch {
        std::array<bool, kButtonInputCount> buttons{};
        bool stick = false;
    };

    HeldThresholds thresholds_;
    std::array<HandLatch, 2> latched_{};
    bool holding_ = false;
};

} // namespace evr::input
