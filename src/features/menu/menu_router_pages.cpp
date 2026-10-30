#include "features/menu/menu_router.hpp"

// The Dossier's page (menu_router.hpp, PageChange): counted from the tab keys the router sends and taken from
// the screen the game shows.

namespace evr::menu {

const char* dossierPageName(int page) {
    static constexpr const char* kNames[kDossierPages] = {"map", "arsenal", "codex", "challenges"};
    return page >= 0 && page < kDossierPages ? kNames[page] : "not known";
}

void MenuRouter::setPage(int page, PageChange::Why why, std::uint8_t key, RouterOutput& out) {
    if (page != page_ || why == PageChange::Why::EndKept) {
        out.pageChanges.push_back({page_, page, why, key});
    }
    if (page != page_) {
        keptKey_ = 0; // a key kept at an end counts as asked again only until the page changes
    }
    page_ = page;
}

void MenuRouter::tabKey(std::uint8_t key, double now, RouterOutput& out) {
    // The router counts the tab keys it sends (and follows the screen the game then shows, when it can tell:
    // followShownScreen). A key past either end is kept back, whether the game would wrap or stop there: the
    // count stays right either way (it used to give up, which turned the map's sticks off until the Dossier
    // was opened again).
    if (page_ >= 0) {
        const int next = key == kKeyNextTab ? page_ + 1 : page_ - 1;
        if (next < 0 || next >= kDossierPages) {
            if (keptKey_ == key && now - keptAt_ <= tuning_.endRepeatWindow) {
                // Asked again: the count may be wrong. The key goes out and the page is no longer known.
                keptKey_ = 0;
                tapKey(key, now, out);
                setPage(-1, PageChange::Why::EndForced, key, out);
                return;
            }
            keptKey_ = key;
            keptAt_ = now;
            setPage(page_, PageChange::Why::EndKept, key, out);
            return;
        }
        tapKey(key, now, out);
        setPage(next, PageChange::Why::TabKey, key, out);
        return;
    }
    tapKey(key, now, out);
}

void MenuRouter::followShownScreen(const RouterInput& in, RouterOutput& out) {
    // A Dossier screen shown since the page last followed one (the Dossier opening, a tab key, a tab
    // clicked): its page, whatever the count says. A popup over the game is not the Dossier.
    if (in.dossierShows == shows_ || in.popup) {
        return;
    }
    shows_ = in.dossierShows;
    setPage(in.dossierShown, PageChange::Why::GameShows, 0, out);
}

} // namespace evr::menu
