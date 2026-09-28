#pragma once

// The built-in controller data: the files in data/input/controllers/, built in at compile time. Each
// holds a controller family's OpenXR suggested bindings and its default control maps, and is read
// with features/input/controller_bindings.hpp; a copy edited by a player is read the same way.
//
// Families without a file of their own (Reverb G2, PSVR2 Sense, Pico) use the Touch data until they
// get one.

#include <cstdint>
#include <string_view>

namespace evr::game {

enum class Controller : std::uint8_t {
    OculusTouch,
    ValveIndex,
};

// The data file's base name, e.g. "oculus_touch" for oculus_touch.toml.
std::string_view controllerName(Controller controller);

std::string_view builtinControllerData(Controller controller);

} // namespace evr::game
