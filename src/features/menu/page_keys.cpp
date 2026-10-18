#include "features/menu/page_keys.hpp"

namespace evr::menu {

namespace {

bool fresh(double since, const PageKeysTuning& tuning) {
    return since >= 0.0 && since < tuning.updateFresh;
}

} // namespace

bool rewardsShowing(const PageKeysInput& in, const PageKeysTuning& tuning) {
    return in.allowed && in.rewardsArrows && fresh(in.sinceRewardsUpdate, tuning);
}

PageKeys pageKeysFor(const PageKeysInput& in, const PageKeysTuning& tuning) {
    const bool tabList = in.allowed && fresh(in.sinceTabListUpdate, tuning);
    return rewardsShowing(in, tuning) || tabList ? kPageArrowKeys : PageKeys{};
}

} // namespace evr::menu
