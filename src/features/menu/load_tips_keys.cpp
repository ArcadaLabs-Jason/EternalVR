#include "features/menu/load_tips_keys.hpp"

#include "features/menu/menu_router.hpp"

#include <cstddef>

namespace evr::menu {

namespace {

constexpr std::array<std::uint8_t, 2> kKeys{kKeyPreviousTab, kKeyNextTab};

} // namespace

bool tipsBarShowing(const LoadTipsInput& in, const LoadTipsTuning& tuning) {
    return in.allowed && !in.menuUp && !in.gameView && in.sincePoll >= 0.0 && in.sincePoll < tuning.pollFresh;
}

std::vector<LoadTipsKey> LoadTipsKeys::release() {
    std::vector<LoadTipsKey> out;
    for (std::size_t i = 0; i < grips_.size(); ++i) {
        if (grips_[i].keyDown) {
            out.push_back({kKeys[i], false});
        }
        grips_[i].keyDown = false;
        grips_[i].armed = false;
    }
    return out;
}

std::vector<LoadTipsKey> LoadTipsKeys::update(const LoadTipsInput& in) {
    std::vector<LoadTipsKey> out;
    const bool showing = tipsBarShowing(in, tuning_);
    const std::array<float, 2> values{in.leftGrip, in.rightGrip};
    for (std::size_t i = 0; i < grips_.size(); ++i) {
        Grip& g = grips_[i];
        const bool wasHeld = g.held;
        g.held = wasHeld ? values[i] > tuning_.releaseThreshold : values[i] > tuning_.pressThreshold;
        if (g.keyDown && (!showing || (!g.held && in.seconds - g.downAt >= tuning_.minHold))) {
            out.push_back({kKeys[i], false});
            g.keyDown = false;
        }
        if (!showing) {
            g.armed = false;
            continue;
        }
        if (!g.held) {
            g.armed = true;
        } else if (!wasHeld && g.armed && !g.keyDown) {
            out.push_back({kKeys[i], true});
            g.keyDown = true;
            g.downAt = in.seconds;
            g.armed = false;
        }
    }
    return out;
}

} // namespace evr::menu
