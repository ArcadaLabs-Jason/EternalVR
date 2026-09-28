#pragma once

// The built-in controller data: the files in data/input/controllers/, built in at compile time. Each
// holds a controller family's OpenXR suggested bindings and its default control maps, and is read
// with features/input/controller_bindings.hpp; a copy edited by a player is read the same way.
//
// Every family whose interaction profile the runtime accepts gets its bindings suggested, and the
// profile the runtime then reports for the controllers picks the family's control map. A runtime maps
// controllers without a family of their own (PSVR2 Sense, for one) onto one of these profiles itself.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace evr::game {

enum class Controller : std::uint8_t {
    OculusTouch,
    ValveIndex,
    HpReverbG2,
    WindowsMixedReality,
    ViveCosmos,
    ViveWand,
    Pico4,
    Count,
};

inline constexpr std::size_t kControllerCount = static_cast<std::size_t>(Controller::Count);

// Every family, in enum order.
inline constexpr std::array<Controller, kControllerCount> kControllers{
    Controller::OculusTouch, Controller::ValveIndex, Controller::HpReverbG2, Controller::WindowsMixedReality,
    Controller::ViveCosmos,  Controller::ViveWand,   Controller::Pico4,
};

// The data file's base name, e.g. "oculus_touch" for oculus_touch.toml.
std::string_view controllerName(Controller controller);

std::string_view builtinControllerData(Controller controller);

} // namespace evr::game
