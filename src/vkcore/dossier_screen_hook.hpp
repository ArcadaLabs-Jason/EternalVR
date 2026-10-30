#pragma once

// The Dossier screen the game shows, for the menu router's page (features/menu/dossier_screen.hpp;
// docs/VR_MENUS.md, "The Dossier's map"; docs/rig-findings/menus.md section 8).
//
// idHUDMenu_Dossier's ShowScreen (its vtable's slot 5, RVA 0xF39010 in build 25216728; edx a dossierPage_t)
// runs for the screen the Dossier opens on (twice) and for every tab change, whichever way it came. A
// read-only mid hook at its first instruction counts the calls and keeps the last screen. Without the hook
// (another game build) the count stays 0 and the router counts its tab keys from the map as before.

#include <cstdint>

namespace evr::vkcore::dossier_screen {

// Installs the hook the first time; false when ShowScreen is not found (logged).
bool install();
// The ShowScreen calls so far (0: none yet, or no hook) and the router's page of the last screen
// (menu::dossierPageOfScreen); any thread.
void readShown(std::uint32_t& count, int& page);

} // namespace evr::vkcore::dossier_screen
