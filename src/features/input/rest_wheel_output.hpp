#pragma once

// What the thumb-rest wheel (rest_wheel.hpp) gives each frame, apart from the wheel itself, so the mapper's
// output (game_input.hpp) and the vibration (haptics_policy.hpp) can carry it without the whole wheel.

#include "features/input/axis2.hpp"
#include "features/input/controller_state.hpp"
#include "features/input/wheel_mouse.hpp"
#include "game/eternal/game_action.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace evr::input {

enum class WheelTick : std::uint8_t {
    None,
    Arm,  // the thumb-rest wheel started picking
    Pick, // it picked a weapon (pressed the slot, or let the game's wheel go)
};

enum class RestWheelEvent : std::uint8_t {
    None,
    Voided,    // a thumb landed but opened no window (RestWheelVoid says why)
    Armed,     // picking started
    Opened,    // the game's wheel is held
    Picked,    // a weapon slot was pressed
    Cancelled, // picking ended with nothing picked
    Released,  // the game's wheel was let go
};

const char* restWheelEventName(RestWheelEvent event);

// Why a landing opened no window.
enum class RestWheelVoid : std::uint8_t {
    None,
    StickOut,  // a stick was out of the centre
    OwnStick,  // the thumb came from its own stick (out of the centre just before)
    OwnButton, // the thumb came from its own face button (let go just before)
};

struct RestWheelOutput {
    bool wheelDown = false;               // hold WeaponWheel
    Axis2 pointer;                        // where the wheel points while it is held
    std::optional<game::GameAction> slot; // one frame
    std::array<bool, 2> taken{};          // the hand's stick belongs to the wheel: no turn, move or gesture
    std::array<WheelTick, 2> tick{};      // a haptic tick, on the picking stick's hand
    // For the log: what happened this frame, with whose rest and stick.
    RestWheelEvent event = RestWheelEvent::None;
    RestWheelVoid voided = RestWheelVoid::None;
    Hand restHand = Hand::Left;
    Hand stickHand = Hand::Right;
    WheelDirection direction = WheelDirection::None;
    float sinceTouch = 0.0f; // Armed under edge: seconds from the thumb's landing
};

} // namespace evr::input
