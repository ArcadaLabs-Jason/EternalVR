#pragma once

// A second pause button where the runtime keeps the Menu button for its own dashboard.
//
// SteamVR opens its dashboard on the left Menu button of Quest (Touch) controllers, so the game never
// gets the press the built-in maps pause with. With that runtime, the hold of the Y button on the same
// hand pauses too, in place of mission information (which the Dossier also shows). Applied to the compiled
// control map like the Dossier press (dossier_press.hpp); a button a player remapped (not exactly Menu tap
// = pause and secondary hold = mission info on one hand) is left alone.

#include "features/input/binding_profile.hpp"

#include <cstddef>
#include <string_view>

namespace evr::input {

// True for a runtime whose name says it keeps the Menu button for its dashboard (SteamVR).
bool runtimeTakesMenuButton(std::string_view runtimeName);

// Turns each secondary-button hold that shows mission information into a pause, on every hand whose Menu
// tap pauses. Returns the number of buttons changed.
std::size_t applyDashboardPause(BindingProfile& profile);

} // namespace evr::input
