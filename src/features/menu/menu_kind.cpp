#include "features/menu/menu_kind.hpp"

#include <algorithm>
#include <cmath>

namespace evr::menu {

bool continuesMenu(const MenuKindInput& in, double continueSeconds) {
    return in.hadMenu && !in.mapLoadedSince && !in.dossierAskedSinceGone && !in.menuAskedSinceGone &&
           std::isfinite(in.sinceCursorGone) && in.sinceCursorGone >= 0.0 &&
           in.sinceCursorGone < continueSeconds;
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
    if (held && !in.mapLoadedSince && !in.dossierAskedSinceGone && !in.menuAskedSinceGone) {
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

bool padMenuOverGame(bool gamepadMode, bool cursorMenu, bool backdrop, bool overGame, bool menuModel) {
    return gamepadMode && !cursorMenu && (backdrop || menuModel) && overGame;
}

bool padDossierOverGame(bool gamepadMode, bool cursorMenu, bool dossierOpen, bool overGame) {
    return gamepadMode && !cursorMenu && dossierOpen && overGame;
}

bool dossierOpen(std::uint32_t opens, bool lastOpen, std::uint32_t loadsAtOpen, std::uint32_t mapLoads) {
    return opens != 0 && lastOpen && loadsAtOpen == mapLoads;
}

PadMenuWatch::Step PadMenuWatch::update(bool seen, double seconds, std::uint32_t mapLoads, bool known) {
    Step step;
    if (mapLoads != loads_) {
        loadedAt_ = seconds;
    }
    seen = known || (seen && seconds - loadedAt_ >= kAfterLoadSeconds);
    if (known) {
        knownAt_ = seconds;
    }
    if (!seen || mapLoads != loads_ || !(seconds >= since_)) {
        since_ = seen ? seconds : -1.0; // a map load (or a clock gone back) starts the wait again
        up_ = false;
        capped_ = capped_ && seen && mapLoads == loads_;
    } else if (since_ < 0.0) {
        since_ = seconds;
    }
    loads_ = mapLoads;
    if (!seen || capped_) {
        return step;
    }
    const double held = seconds - since_;
    if (!known && seconds - std::max(since_, knownAt_) >= kSettleSeconds + kMaxSeconds) {
        capped_ = true;
        step.capped = up_;
        up_ = false;
        return step;
    }
    up_ = known || held >= kSettleSeconds;
    step.up = up_;
    return step;
}

} // namespace evr::menu
