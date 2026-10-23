#pragma once

// The arm solve the settings ask for (ETERNALVR_ARM_SOLVE), as features/arm/solve_mode.hpp spells it out:
// the off hand (offhand_hook.cpp, offhand_arm.cpp), the weapon arm (weapon_arm.cpp) and the modifiers' room
// (offhand_mods.cpp) take every difference between the two solves from here.

#include "features/arm/solve_mode.hpp"
#include "features/input/controller_settings.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/offhand_mods.hpp"

#include <cstddef>
#include <optional>

namespace evr::vkcore::controllers {

inline arm::SolveMode armSolveMode() {
    return arm::solveMode(settings().armSolve == input::ArmSolve::Bind ? arm::TwistFrom::Bind
                                                                       : arm::TwistFrom::Animated);
}

// The weight the joints are written past their targets by with a solve that does (the hands' modifier
// weight, offhand_mods::modWeight); nullopt otherwise, or when the weight cannot be read.
inline std::optional<float> pastWeight(const arm::SolveMode& mode, const std::byte* hands) {
    return mode.writesPast ? offhand_mods::modWeight(hands) : std::nullopt;
}

} // namespace evr::vkcore::controllers
