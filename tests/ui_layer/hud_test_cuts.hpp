#pragma once

// Corner blocks as the GUI readback measures them (ui_layer/hud_blocks.hpp) on the rig's captures, with the
// readback's margin, at the game's Medium and Large HUD Scale, for the tests of the hand HUDs' layout and
// cuts. Band coordinates; the blocks as drawn (hud_regions.hpp): Medium health and armor x 0.029..0.188,
// y 0.780..0.947, ammo x 0.793..0.936, y 0.829..0.940; Large x 0.037..0.225, y 0.749..0.944 and x
// 0.752..0.922, y 0.805..0.937.

#include "ui_layer/hud_regions.hpp"

#include <cstdint>
#include <vector>

namespace hud_test {

inline evr::ui_layer::CornerBlocks blocksAt(bool large, std::uint32_t width, std::uint32_t height) {
    using evr::ui_layer::BandRect;
    using evr::ui_layer::toPixels;
    const BandRect vitals =
        large ? BandRect{0.025f, 0.728f, 0.237f, 0.965f} : BandRect{0.017f, 0.759f, 0.200f, 0.968f};
    const BandRect weapon =
        large ? BandRect{0.740f, 0.784f, 0.934f, 0.958f} : BandRect{0.781f, 0.808f, 0.948f, 0.961f};
    return {toPixels(vitals, width, height), toPixels(weapon, width, height)};
}

inline evr::ui_layer::HandCuts cutsAt(bool large,
                                      const std::vector<evr::ui_layer::WristBlock>& moved,
                                      std::uint32_t width,
                                      std::uint32_t height) {
    return evr::ui_layer::handCuts(moved, blocksAt(large, width, height), width, height);
}

inline const std::vector<evr::ui_layer::WristBlock>& corners() {
    static const std::vector<evr::ui_layer::WristBlock> both{evr::ui_layer::WristBlock::Vitals,
                                                             evr::ui_layer::WristBlock::Weapon};
    return both;
}

} // namespace hud_test
