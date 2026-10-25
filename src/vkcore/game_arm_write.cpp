// The game's first-person arms bent by the layer: the writes of the layer's modifiers and their give-back
// (game_arm.hpp; the read and install in game_arm.cpp).

#include "vkcore/game_arm.hpp"

#include "features/arm/arm_frames.hpp"
#include "features/arm/parent_frames.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/game_arm_cache.hpp"
#include "vkcore/seh_filter.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace evr::vkcore::controllers::game_arm {

using detail::Cache;
using detail::cacheOf;
using detail::callFaulted;
using detail::idsOf;
using detail::kFlagScale;
using detail::kFlagsChange;
using detail::kFlagsOverride;
using detail::kTicksCap;

namespace {

constexpr std::int32_t kReleaseWrites = 3; // no-change writes that reach both lists (one a tick); then none

// The game's call, apart from anything with a destructor (__try needs that). False when it faulted.
bool callSetJointMod(
    void* hands, std::int32_t mod, std::int16_t joint, const float* t, const float* r, std::uint16_t f) {
    __try {
        detail::setJointMod()(hands, mod, joint, t, r, f);
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

// Writes the modifier `k` of the arm: an override at `pose` (model space, game units) or, without one, no
// change. False (the session's calls off) when the call faulted.
bool writeMod(arm::ArmSide side,
              const std::byte* hands,
              std::int32_t k,
              const xr_math::ModelPose* pose,
              Vec3 scale,
              Vec3 origin,
              std::uint16_t flags = kFlagsOverride) {
    Cache& c = cacheOf(side);
    const float zero[3] = {};
    const xr_math::Mat3Rows identity = xr_math::JointMod{}.rotation;
    float translation[3] = {};
    xr_math::Mat3Rows rotation = identity;
    if (pose) {
        translation[0] = (pose->position.x - origin.x) / scale.x;
        translation[1] = (pose->position.y - origin.y) / scale.y;
        translation[2] = (pose->position.z - origin.z) / scale.z;
        rotation = xr_math::toRows(pose->axis);
    }
    if (!callSetJointMod(const_cast<std::byte*>(hands), c.base + k, c.ids[static_cast<std::size_t>(k)],
                         pose ? translation : zero, rotation.data(), pose ? flags : kFlagsChange)) {
        callFaulted(side, "SetJointMod");
        return false;
    }
    return true;
}

} // namespace

bool writeArm(arm::ArmSide side,
              const std::byte* hands,
              const arm::ArmPoses& poses,
              Vec3 scale,
              Vec3 origin,
              std::uint32_t zeroScale,
              const arm::ArmPoses* landing,
              const xr_math::JointMod* wrist,
              const arm::GripOverrides* glove,
              const std::array<xr_math::ModelPose, arm::kFingerModCount>* fingers) {
    Cache& c = cacheOf(side);
    if (detail::faulted() || hands != c.hands ||
        !offhand_mods::layerModsInPlace(c.node, c.base, idsOf(c, side))) {
        return false;
    }
    if (glove) {
        // The glove on a grip: each modifier in its joint's parent's frame (parent_frames.hpp).
        if (offhand_mods::layerCount(side) != static_cast<std::int32_t>(arm::kGloveWrites)) {
            return false;
        }
        const auto writes = arm::gloveWrites(poses, *glove, scale, origin, zeroScale);
        for (std::size_t k = 0; k < writes.size(); ++k) {
            if (!writeMod(side, hands, static_cast<std::int32_t>(k), &writes[k].pose, {1.0f, 1.0f, 1.0f}, {},
                          writes[k].flags)) {
                return false;
            }
        }
        c.wristQuietTicks = 0;
    } else {
        // The wrist's modifier first, when the arm has one (offhand_mods::layerJoint): the off hand's change,
        // the weapon arm's glove as no change.
        const std::int32_t first = offhand_mods::hasWrist(side) ? 1 : 0;
        const xr_math::JointMod change = wrist ? *wrist : xr_math::JointMod{};
        const bool none = change.translation == Vec3{} && change.rotation == xr_math::JointMod{}.rotation;
        c.wristQuietTicks = none ? std::min(c.wristQuietTicks + 1, kTicksCap) : 0;
        const float translation[3] = {change.translation.x, change.translation.y, change.translation.z};
        if (first > 0 && !callSetJointMod(const_cast<std::byte*>(hands), c.base, c.ids[0], translation,
                                          change.rotation.data(), kFlagsChange)) {
            callFaulted(side, "SetJointMod");
            return false;
        }
        for (std::int32_t k = 0; k < offhand_mods::kLayerCount; ++k) {
            const arm::ArmJoint joint = offhand_mods::kLayerJoints[static_cast<std::size_t>(k)];
            const bool zero = (zeroScale & (1u << arm::index(joint))) != 0;
            if (!writeMod(side, hands, first + k, &poses[arm::index(joint)], scale, origin,
                          zero ? static_cast<std::uint16_t>(kFlagsOverride | kFlagScale) : kFlagsOverride)) {
                return false;
            }
        }
        // The fingers: with `fingers` (the free off hand's hand poses) each an override in its parent's
        // frame, its offset divided by the model scale (parent_frames.hpp); otherwise no change.
        const std::int32_t fingerBase = offhand_mods::armCount(side);
        const bool posed =
            fingers && offhand_mods::fingerCount(side) == static_cast<std::int32_t>(fingers->size());
        for (std::int32_t k = 0; k < offhand_mods::fingerCount(side); ++k) {
            const xr_math::ModelPose* pose = posed ? &(*fingers)[static_cast<std::size_t>(k)] : nullptr;
            if (!writeMod(side, hands, fingerBase + k, pose, scale, {}, arm::kFlagsParentOverride)) {
                return false;
            }
        }
    }
    c.writtenUnder = glove && arm::heldInParents(*glove, scale) ? std::optional(glove->attach) : std::nullopt;
    c.modelShoulder = c.writtenUnder && glove->modelShoulder;
    c.lastOverride = GetTickCount64();
    c.ticksSinceOverride = 0;
    c.written = landing ? *landing : poses;
    c.releaseWrites = 0;
    c.collapsed = zeroScale != 0;
    return true;
}

bool releaseArm(arm::ArmSide side, const std::byte* hands, bool bothLists) {
    Cache& c = cacheOf(side);
    // A tick without the layer's overrides: counted toward a clean read (readArm), the wrist's change as
    // none.
    c.ticksSinceOverride = std::min(c.ticksSinceOverride + 1, kTicksCap);
    c.wristQuietTicks = std::min(c.wristQuietTicks + 1, kTicksCap);
    if (bothLists) { // memory writes only, no call into the game: also after a faulted call
        return hands && hands == c.hands && offhand_mods::clearLayerMods(c.node, c.base, idsOf(c, side));
    }
    if (detail::faulted() || !hands || hands != c.hands ||
        !offhand_mods::layerModsInPlace(c.node, c.base, idsOf(c, side))) {
        return false;
    }
    for (std::int32_t k = 0; k < offhand_mods::layerCount(side) && c.releaseWrites < kReleaseWrites; ++k) {
        if (!writeMod(side, hands, k, nullptr, {1.0f, 1.0f, 1.0f}, {})) {
            return false;
        }
    }
    c.releaseWrites = std::min(c.releaseWrites + 1, kReleaseWrites);
    return true;
}

std::array<std::int32_t, 2> overridesLeft(arm::ArmSide side, const std::byte* hands) {
    const Cache& c = cacheOf(side);
    return hands && hands == c.hands ? offhand_mods::overridesInLists(c.node, c.base, idsOf(c, side))
                                     : std::array<std::int32_t, 2>{-1, -1};
}

bool gameCallFaulted() {
    return detail::faulted();
}

const std::byte* armHands(arm::ArmSide side) {
    return cacheOf(side).hands;
}

} // namespace evr::vkcore::controllers::game_arm
