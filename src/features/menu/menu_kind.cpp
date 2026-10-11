#include "features/menu/menu_kind.hpp"

#include <cmath>

namespace evr::menu {

bool continuesMenu(const MenuKindInput& in, double continueSeconds) {
    return in.hadMenu && !in.mapLoadedSince && std::isfinite(in.sinceCursorGone) &&
           in.sinceCursorGone >= 0.0 && in.sinceCursorGone < continueSeconds;
}

MenuKind menuKindOnCursor(const MenuKindInput& in, double continueSeconds) {
    if (continuesMenu(in, continueSeconds)) {
        // The Dossier on another screen: which page is up is not known, so not the map's controls.
        return in.previous == MenuKind::Dossier ? MenuKind::Screen : in.previous;
    }
    if ((in.overGame || in.playMap) && !in.menuRequested) {
        return MenuKind::Popup;
    }
    return in.dossierRequested ? MenuKind::Dossier : MenuKind::Screen;
}

bool screenBecomesPopup(MenuKind kind, bool chainAsked, bool overGame, bool playMap) {
    return kind == MenuKind::Screen && !chainAsked && (overGame || playMap);
}

MenuChain::Step MenuChain::onCursor(MenuKindInput in, bool held, std::uint32_t mapLoads) {
    in.mapLoadedSince = started_ && mapLoads != loads_;
    Step step;
    step.afterLoad = in.mapLoadedSince;
    if (held && !in.mapLoadedSince) {
        // The same menu, its panel never down: everything stays as it was.
        step.kind = in.previous;
        step.continues = true;
        step.keepsPanel = true;
        return step;
    }
    step.kind = menuKindOnCursor(in);
    step.continues = continuesMenu(in);
    if (!step.continues) {
        started_ = true;
        asked_ = in.menuRequested || in.dossierRequested;
        loads_ = mapLoads;
    }
    return step;
}

bool MenuChain::becomesPopup(MenuKind kind, bool overGame, bool playMap, std::uint32_t mapLoads) const {
    return screenBecomesPopup(kind, asked_ && mapLoads == loads_, overGame, playMap);
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
