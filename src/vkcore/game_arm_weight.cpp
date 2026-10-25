// The test hold of the hands' joint-modifier weight and the arm hands only writes (game_arm.hpp).

#include "features/arm/twist_fault.hpp"
#include "features/input/controller_settings.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/game_arm.hpp"

#include <cstddef>

namespace evr::vkcore::controllers::game_arm {

namespace {

constexpr std::size_t kHandsJointMods = 0x2888;
constexpr std::size_t kModWeights[] = {0x3C, 0x40}; // the current and the target weight
// What holdModWeight found (game thread only).
struct HeldWeight {
    const std::byte* owner = nullptr;
    float found[2] = {};
    bool held = false;
};
HeldWeight g_held;

const std::byte* weightOwner(const std::byte* hands) {
    const std::byte* owner = nullptr;
    return !gameCallFaulted() && hands && safeRead(hands + kHandsJointMods, owner) ? owner : nullptr;
}

} // namespace

bool holdModWeight(const std::byte* hands, float weight) {
    const std::byte* owner = weightOwner(hands);
    if (!owner) {
        return false;
    }
    float now[2] = {};
    for (std::size_t i = 0; i < 2; ++i) {
        // Only over a weight: a value outside 0..1 means the layout is not what the probe saw.
        if (!safeRead(owner + kModWeights[i], now[i]) || !(now[i] >= 0.0f && now[i] <= 1.0f)) {
            return false;
        }
    }
    if (!g_held.held || g_held.owner != owner) {
        g_held = {owner, {now[0], now[1]}, true};
    }
    bool wrote = false;
    for (std::size_t i = 0; i < 2; ++i) {
        if (now[i] != weight) {
            wrote =
                safeCopy(const_cast<std::byte*>(owner + kModWeights[i]), &weight, sizeof(weight)) || wrote;
        }
    }
    return wrote;
}

void releaseModWeight(const std::byte* hands) {
    const std::byte* owner = weightOwner(hands);
    if (!g_held.held || !owner || owner != g_held.owner) {
        return;
    }
    g_held.held = false;
    for (std::size_t i = 0; i < 2; ++i) {
        safeCopy(const_cast<std::byte*>(owner + kModWeights[i]), &g_held.found[i], sizeof(float));
    }
}

arm::Cuff cuffOf(input::HandsCuff cuff) {
    return cuff == input::HandsCuff::Stub   ? arm::Cuff::Stub
           : cuff == input::HandsCuff::Ring ? arm::Cuff::Ring
                                            : arm::Cuff::Fold;
}

arm::HandsOnlyArm handsOnlyFor(const arm::ArmPoses& arm, const input::ControllerSettings& cfg) {
    if (!cfg.armsHandsOnly) {
        return {arm, 0u};
    }
    return cfg.testArmFault == input::ArmFault::Forearm ? arm::plantForearmFault(arm)
                                                        : arm::handsOnly(arm, cuffOf(cfg.handsCuff));
}

} // namespace evr::vkcore::controllers::game_arm
