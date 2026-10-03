#pragma once

// A second pause button where the runtime keeps the Menu button for its own dashboard.
//
// SteamVR opens its dashboard on the left Menu button of Touch controllers (Quest through Steam Link, and
// the PS VR2 controllers SteamVR reports as Touch), so the game never gets the press the built-in maps pause
// with. Only those: Index has no Menu button (its menu input is a firm trackpad press, which SteamVR leaves
// alone), and the other families have a system button of their own for the dashboard. With that runtime
// and family, the secondary button's hold that shows mission information pauses instead (the Dossier also
// shows it): Y in the right-handed map and the button swap, B in the full mirror, where mission info is on
// the right hand. Applied to the compiled control map like the Dossier press (dossier_press.hpp); a map a
// player changed is left alone when it pauses on another button already, or has no Menu tap pause or no
// secondary hold for mission info.

#include "features/input/binding_profile.hpp"
#include "features/input/capture_chord.hpp"
#include "game/eternal/controller_data.hpp"

#include <array>
#include <optional>
#include <string_view>

namespace evr::input {

// The families whose left Menu button SteamVR keeps for its dashboard.
inline constexpr std::array kDashboardMenuFamilies{game::Controller::OculusTouch};

// True for a runtime whose name says it keeps the Menu button for its dashboard (SteamVR), with a family
// whose Menu button it keeps.
bool runtimeTakesMenuButton(std::string_view runtimeName, game::Controller family);

// The capture chord's buttons for the runtime and family: where the runtime keeps the Menu button, both
// sticks held are the chord's button too (capture_chord.hpp).
CaptureButtons captureButtonsFor(std::string_view runtimeName, game::Controller family);

// When the Menu tap is the only pause, turns a secondary-button hold that shows mission information into a
// pause: the one on the Menu button's hand, else the other hand's. Returns the hand changed, or nullopt.
std::optional<Hand> applyDashboardPause(BindingProfile& profile);

} // namespace evr::input
