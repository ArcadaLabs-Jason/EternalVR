#include "xr_math/shield_pose.hpp"

#include <cmath>

namespace evr::xr_math {

namespace {

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

Vec3 inFrame(const IdViewAxis& axis, Vec3 local) {
    return axis.forward * local.x + axis.left * local.y + axis.up * local.z;
}

// The shield is never turned round: a ray more than this far from the shield's own forward (cosine) is
// not one the game's shield belongs to.
constexpr float kTurnedBackCosine = -0.5f;

} // namespace

Vec3 shieldEffectForward(const IdViewAxis& effectAxis) {
    return -effectAxis.left;
}

IdViewAxis shieldEffectAxis(const IdViewAxis& view) {
    return {view.left, -view.forward, view.up};
}

Vec3 chaingunMuzzle(const EyeRelativePose& arms, float unitsPerMetre) {
    return arms.offset + inFrame(arms.axis, kChaingunMuzzleInArms * unitsPerMetre);
}

std::optional<ShieldEffect> shieldInFrontOfGun(
    const ShieldEffect& game, Vec3 rayStart, Vec3 rayDirection, Vec3 muzzle, float unitsPerMetre) {
    if (!finite(game.origin) || !finite(game.axis.forward) || !finite(game.axis.left) ||
        !finite(game.axis.up) || !finite(rayStart) || !finite(rayDirection) || !finite(muzzle) ||
        !std::isfinite(unitsPerMetre) || unitsPerMetre <= 0.0f || !isOrthonormal(game.axis)) {
        return std::nullopt;
    }
    const float rayLength = length(rayDirection);
    if (!(rayLength > 0.0f)) {
        return std::nullopt;
    }
    const Vec3 ray = rayDirection * (1.0f / rayLength);
    const Vec3 forward = shieldEffectForward(game.axis);
    const float cosine = dot(forward, ray);
    if (cosine < kTurnedBackCosine) {
        return std::nullopt;
    }
    const std::optional<IdViewAxis> view = axisFromDirection(ray);
    if (!view) {
        return std::nullopt;
    }
    ShieldEffect out;
    out.axis = shieldEffectAxis(*view);
    const float muzzleAlong = std::fmax(0.0f, dot(muzzle - rayStart, ray));
    const Vec3 centre = rayStart + ray * (muzzleAlong + kShieldPastMuzzle * unitsPerMetre);
    out.origin = centre - shieldEffectForward(out.axis) * (kShieldEffectReach * unitsPerMetre);
    return out;
}

ShieldScale::Step ShieldScale::next(bool placing) {
    if (placing) {
        scaled_ = true;
        missed_ = 0;
        restore_ = false;
        return {kShieldWorldScale, true};
    }
    if (!scaled_) {
        return {};
    }
    if (!restore_ && ++missed_ <= kHoldThinks) {
        return {std::nullopt, true};
    }
    scaled_ = false;
    missed_ = 0;
    restore_ = false;
    return {1.0f, false};
}

void ShieldScale::notSent(const Step& step) {
    if (step.send) {
        scaled_ = true;
        restore_ = true;
    }
}

void ShieldScale::refused() {
    if (scaled_) {
        restore_ = true;
    }
}

bool movesShieldPose(const ShieldPoseAsk& ask) {
    return ask.placedShield && ask.shieldClass && ask.fromEffect && ask.sincePlaced >= 0.0 &&
           ask.sincePlaced <= kShieldPlacedSeconds;
}

} // namespace evr::xr_math
