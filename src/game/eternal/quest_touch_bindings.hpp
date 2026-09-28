#pragma once

// The built-in control maps for Meta Quest Touch style controllers (R06 section 4), written in the
// same binding text players use (features/input/binding_text.hpp), so a built-in map is only a base
// that a player's overrides apply to. Index, Reverb G2, PSVR2 Sense and Pico controllers use these
// through their OpenXR interaction profiles until they get maps of their own.
//
// This is data only. The tests check that every map parses and compiles without issues.

#include <cstdint>
#include <string_view>

namespace evr::game {

enum class Handedness : std::uint8_t {
    Right,
    // Weapon in the left hand: triggers, grips and stick clicks swap sides. Sticks keep move-left /
    // turn-right and the face buttons stay where they are.
    LeftButtonSwap,
    // A full mirror: sticks swap too, as do the face buttons (jump and dash move to X/Y). The pause
    // stays on the left Menu button, the only one an application can read on Touch controllers.
    LeftButtonAndStickSwap,
};

std::string_view questTouchBindingText(Handedness handedness = Handedness::Right);

} // namespace evr::game
