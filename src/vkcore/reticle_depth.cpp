#include "vkcore/reticle_depth.hpp"

#include "vkcore/controllers.hpp"
#include "vkcore/head_sweep.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace evr::vkcore {

namespace {

// A thin trace: the dot marks where a shot's line meets the world.
constexpr float kTraceRadiusMetres = 0.01f;

} // namespace

float reticleHitMetres(const std::byte* player, Vec3 eye, float unitsPerMetre) {
    if (!headSweepAvailable() || !(unitsPerMetre > 0.0f)) {
        return 0.0f;
    }
    const std::optional<controllers::WorldRay> ray = controllers::weaponRayInWorld(eye);
    if (!ray) {
        return 0.0f;
    }
    const float reach = kReticleReachMetres * unitsPerMetre;
    const std::optional<float> hit = sweepHead(player, ray->origin, ray->origin + ray->direction * reach,
                                               kTraceRadiusMetres * unitsPerMetre);
    if (!hit || !std::isfinite(*hit)) {
        return kReticleReachMetres;
    }
    return std::clamp(*hit * kReticleReachMetres, kReticleNearestMetres, kReticleReachMetres);
}

} // namespace evr::vkcore
