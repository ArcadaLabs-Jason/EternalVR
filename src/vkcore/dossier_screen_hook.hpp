#pragma once

// The Dossier screen the game shows, for the menu router's page (features/menu/dossier_screen.hpp;
// docs/VR_MENUS.md, "The Dossier's map"; docs/rig-findings/menus.md section 8).
//
// idHUDMenu_Dossier's ShowScreen (its vtable's slot 5, RVA 0xF39010 in build 25216728; edx a dossierPage_t)
// runs for the screen the Dossier opens on (twice) and for every tab change, whichever way it came. A
// read-only mid hook at its first instruction counts the calls and keeps the last screen. Without the hook
// (another game build) the count stays 0 and the router counts its tab keys from the map as before.
//
// Whether the Dossier is open (gamepad mode, menu::padDossierOverGame: its map and CODEX pages show no
// backdrop the backdrop test sees and no 3D model): its own open and close, slots 6 (RVA 0xF38450, which
// sends the HUD event 0x21) and 7 (RVA 0xF38560, event 0x22) of the same vtable, the only pointers to them. A
// read-only mid hook at the first instruction of each counts the opens and keeps whether the last call was an
// open, with the map loads at that open. Close first: an open without its close is never installed.

#include <cstdint>

namespace evr::vkcore::dossier_screen {

// Installs the hooks the first time; false when ShowScreen is not found (logged; the open and close hooks are
// installed or not on their own, also logged).
bool install();
// The ShowScreen calls so far (0: none yet, or no hook) and the router's page of the last screen
// (menu::dossierPageOfScreen); any thread.
void readShown(std::uint32_t& count, int& page);
// The Dossier's open calls so far, whether the last of its open and close calls was an open, and the map
// loads (mp_guard::mapLoads) when the last open ran (all 0 and false without the hooks); any thread.
void readOpen(std::uint32_t& opens, bool& open, std::uint32_t& loadsAtOpen);

} // namespace evr::vkcore::dossier_screen
