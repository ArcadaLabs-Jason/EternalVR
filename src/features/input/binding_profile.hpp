#pragma once

// A compiled set of controller bindings, ready for the mapper (R06 section 4).
//
// Players remap controls, so bindings are never hard-coded: they are written as text
// (binding_text.hpp), checked and compiled into this form (binding_compiler.hpp), and a player's
// profile stores only the entries they changed (binding_overrides.hpp). The built-in maps are text
// in the same format (game/eternal/quest_touch_bindings.hpp).

#include "features/input/controller_state.hpp"
#include "game/eternal/game_action.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace evr::input {

enum class ButtonInput : std::uint8_t {
    Trigger,
    Grip,
    StickClick,
    Primary,   // A on the right Touch controller, X on the left.
    Secondary, // B on the right, Y on the left.
    Menu,
    Count,
};

inline constexpr std::size_t kButtonInputCount = static_cast<std::size_t>(ButtonInput::Count);

enum class PressKind : std::uint8_t {
    WhileDown, // Active for as long as the input is down.
    Tap,       // One frame, when the input is released before the hold time.
    Hold,      // Active from the hold time until the input is released.
};

struct ButtonBinding {
    Hand hand = Hand::Right;
    ButtonInput input = ButtonInput::Trigger;
    PressKind kind = PressKind::WhileDown;
    game::GameAction action = game::GameAction::Fire;

    friend bool operator==(const ButtonBinding&, const ButtonBinding&) = default;
};

// Deliberate directional gestures on the turn stick. What counts as deliberate is decided by the
// turn-stick arbiter (turn_stick_arbiter.hpp), not by the bindings.
enum class StickGesture : std::uint8_t {
    Up,       // Active for the whole up sweep.
    DownTap,  // One frame, when a short down sweep returns to the centre.
    DownHold, // Active from the hold time until the stick returns to the centre.
};

struct StickGestureBinding {
    StickGesture gesture = StickGesture::Up;
    game::GameAction action = game::GameAction::Chainsaw;

    friend bool operator==(const StickGestureBinding&, const StickGestureBinding&) = default;
};

struct BindingProfile {
    // The hand holding the weapon. The other hand's grip steadies the weapon during two-handed aim,
    // and none of its grip bindings fire while it does.
    Hand weaponHand = Hand::Right;
    std::optional<Hand> moveStick;
    std::optional<Hand> turnStick;
    std::vector<ButtonBinding> buttons;
    // Gestures on the turn stick.
    std::vector<StickGestureBinding> stickGestures;
};

} // namespace evr::input
