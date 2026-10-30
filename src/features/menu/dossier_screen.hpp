#pragma once

// The Dossier screen the game shows (vkcore/dossier_screen_hook.hpp) as the menu router's page
// (menu_router.hpp; docs/rig-findings/menus.md section 8).
//
// The Dossier (idHUDMenu_Dossier) has six screens, dossierPage_t: MAP 0, ARSENAL 1, SUIT 2, RUNES 3, CODEX 4,
// CHALLENGES 5, and four tabs: Map, Arsenal, Codex and Challenges. Its screenToTabIndexMapping reads
// 0 1 -1 -1 2 3: the Praetor suit and rune screens have no tab of their own.
//
// Pure.

namespace evr::menu {

// The router's page (0 the map, 1 Arsenal, 2 Codex, 3 Challenges) of a screen the game shows; -1 for a
// screen without a tab of its own or a value that is not a screen.
constexpr int dossierPageOfScreen(int screen) {
    constexpr int kPageOfScreen[] = {0, 1, -1, -1, 2, 3};
    return screen >= 0 && screen < 6 ? kPageOfScreen[screen] : -1;
}

} // namespace evr::menu
