#pragma once

// Which press of the X button opens the Dossier (ETERNALVR_DOSSIER, docs/VR_CONTROLLERS.md).
//
// The built-in maps put two actions on the off hand's primary button (X, or A in the full mirror): a tap
// switches equipment and a hold opens the Dossier. Players who open the Dossier more often than they
// switch equipment can swap the two. The swap is applied to the compiled control map, so it covers every
// controller family and handedness, and a button a player remapped (no longer tap = switch equipment and
// hold = Dossier) is left alone.

#include "features/input/binding_profile.hpp"

#include <cstddef>
#include <cstdint>

namespace evr::input {

enum class DossierPress : std::uint8_t {
    Hold, // a tap switches equipment, a hold (the hold time, 0.25 s) opens the Dossier (default)
    Tap,  // a tap opens the Dossier, a hold switches equipment
};

// Swaps the tap and hold of each button that carries switch equipment on its tap and the Dossier on its
// hold, when `press` is Tap; nothing for Hold. Returns the number of buttons swapped.
std::size_t applyDossierPress(BindingProfile& profile, DossierPress press);

const char* dossierPressName(DossierPress press);

} // namespace evr::input
