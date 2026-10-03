#pragma once

// Which controller family's control map is used, from the profiles the runtime reports for the two hands
// (docs/VR_CONTROLLERS.md). Each hand's profile maps to a family, or to none (no controller, or a profile
// without data). A controller that is off or asleep reports nothing, so either hand alone decides; the
// right hand used to decide alone, which kept the previous family (Touch at first) while the right
// controller slept. Pure.

#include "game/eternal/controller_data.hpp"

#include <optional>

namespace evr::input {

// - both hands the same family, or only one hand with a family: that family;
// - two different families (one controller's profile changing before the other's): `current` when it is
//   one of them, so the map does not flip back and forth, else the right hand's;
// - neither hand with a family: `current`.
game::Controller pickControllerFamily(std::optional<game::Controller> left,
                                      std::optional<game::Controller> right,
                                      game::Controller current);

// Whether the XR worker builds the control map now, ahead of the mapper's first run. The mapper runs with the
// game's user commands, which the title screen and the main menu do not build, and the game's prompts name
// the buttons only once a control map is built: the main menu's hint bars named the keyboard keys until the
// first level (owner, 2026-10-02). It is built once the runtime gave this frame's controller data (`synced`)
// and a controller is known (`profileReported`: a hand reports an interaction profile; `scripted`: scripted
// input stands in for one), for the family in use; the builder does nothing when that family's map is built.
bool buildControlMapAhead(bool synced, bool profileReported, bool scripted);

} // namespace evr::input
