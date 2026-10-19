#pragma once

// The hands' and player's state the arm policy decides from (features/input/offhand_policy.hpp), read for the
// off-hand hook's tick.

#include "features/input/offhand_policy.hpp"

#include <cstddef>

namespace evr::vkcore::controllers {

// idHands' pending action, destination state, hidden reasons and flags, and the player's first-person hands
// disables and sync (type info, build 25216728); offHandTracked, modelPlaced and forcedView are left to the
// caller.
input::ArmSignals readArmSignals(const std::byte* hands, const std::byte* player);

} // namespace evr::vkcore::controllers
