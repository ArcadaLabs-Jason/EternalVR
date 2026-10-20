// The arm policy's signals (arm_signals.hpp).

#include "vkcore/arm_signals.hpp"

#include "vkcore/controllers.hpp"
#include "vkcore/controllers_impl.hpp"

namespace evr::vkcore::controllers {

namespace {

// Type info (build 25216728).
constexpr std::size_t kHandsDestState = 0x1970;        // idHands::destHandsState
constexpr std::size_t kHandsHiddenReasons = 0x29A0;    // idHands::hiddenReasons
constexpr std::size_t kHandsPendingAction = 0x8CD0;    // idHands::pendingAction.action
constexpr std::size_t kHandsFlags = 0x8DA0;            // idHands::handsFlags
constexpr std::size_t kPlayerFpHandsDisabled = 0x5C28; // idPlayer::disableFPHandsReasons

} // namespace

input::ArmSignals readArmSignals(const std::byte* hands, const std::byte* player) {
    input::ArmSignals s;
    safeRead(hands + kHandsPendingAction, s.pendingAction);
    safeRead(hands + kHandsDestState, s.destHandsState);
    safeRead(hands + kHandsHiddenReasons, s.hiddenReasons);
    safeRead(hands + kHandsFlags, s.handsFlags);
    safeRead(player + kPlayerFpHandsDisabled, s.fpHandsDisabled);
    // Any sync: a pickup's animation moves the hands too.
    s.syncActive = syncEntity(player) != nullptr;
    return s;
}

std::optional<std::int32_t> readDestHandsState(const std::byte* hands) {
    std::int32_t state = -1;
    return hands && safeRead(hands + kHandsDestState, state) ? std::optional(state) : std::nullopt;
}

std::uint64_t readHandsFlags(const std::byte* hands) {
    std::uint64_t flags = 0;
    return hands && safeRead(hands + kHandsFlags, flags) ? flags : 0;
}

} // namespace evr::vkcore::controllers
