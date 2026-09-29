#include "features/menu/menu_kind.hpp"

#include <cmath>

namespace evr::menu {

MenuKind menuKindOnCursor(const MenuKindInput& in, double continueSeconds) {
    if (in.hadMenu && std::isfinite(in.sinceCursorGone) && in.sinceCursorGone >= 0.0 &&
        in.sinceCursorGone < continueSeconds) {
        // The Dossier on another screen: which page is up is not known, so not the map's controls.
        return in.previous == MenuKind::Dossier ? MenuKind::Screen : in.previous;
    }
    if (in.overGame && !in.menuRequested) {
        return MenuKind::Popup;
    }
    return in.dossierRequested ? MenuKind::Dossier : MenuKind::Screen;
}

const char* menuKindName(MenuKind kind) {
    switch (kind) {
    case MenuKind::Screen:
        return "screen";
    case MenuKind::Popup:
        return "popup";
    case MenuKind::Dossier:
        return "dossier";
    }
    return "?";
}

} // namespace evr::menu
