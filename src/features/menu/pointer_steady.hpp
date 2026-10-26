#pragma once

// The menu pointer's steadiness (docs/VR_MENUS.md): each hand's aim orientation through the same one-euro
// filter and strength as the gun's aim (Aim steadiness, ETERNALVR_AIM_SMOOTHING,
// features/input/aim_smoothing.hpp), so a hand that shakes while held out moves the pointer as little as it
// moves the gun. Only the orientation is filtered, as for the gun; the ray starts where the hand is. The hit
// on the panel and the beam take the same filtered pose.
//
// Pure: no OpenXR.

#include "common/pose.hpp"
#include "features/input/aim_smoothing.hpp"
#include "features/input/controller_state.hpp"

#include <array>
#include <optional>

namespace evr::menu {

class PointerSteady {
public:
    // `params`: aimSmoothingParams of the player's strength; nullopt (off) passes every pose through.
    explicit PointerSteady(std::optional<input::OneEuroParams> params);

    // The pointer's pose for `hand`'s aim pose `aim` sampled at `seconds` (input::InputFrame::sampleSeconds).
    // The same time twice gives the same pose.
    Pose update(input::Hand hand, const Pose& aim, double seconds);
    // The hand is not tracked or no menu is up: its next pose passes through.
    void reset(input::Hand hand);

private:
    std::array<std::optional<input::OneEuroRotation>, 2> filters_;
};

} // namespace evr::menu
