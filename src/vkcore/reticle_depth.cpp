#include "vkcore/reticle_depth.hpp"

#include "vkcore/controllers.hpp"
#include "vkcore/head_sweep.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace evr::vkcore {

namespace {} // namespace

float reticleHitMetres(const std::byte* player, Vec3 eye, float unitsPerMetre) {
    if (!headSweepAvailable() || !(unitsPerMetre > 0.0f)) {
        return 0.0f;
    }
    const std::optional<controllers::WorldRay> ray = controllers::weaponRayInWorld(eye);
    if (!ray) {
        return 0.0f;
    }
    const float reach = kReticleReachMetres * unitsPerMetre;
    // The weapon trace's own shape and contents: the dot stops where a shot does.
    bool inContact = false;
    const std::optional<float> hit =
        sweepShot(player, ray->origin, ray->origin + ray->direction * reach, inContact);
    if (inContact) {
        // The hand is at a wall or in cover: the shot's line starts at it, not 100 m away.
        return kReticleNearestMetres;
    }
    if (!hit || !std::isfinite(*hit)) {
        return kReticleReachMetres;
    }
    return std::clamp(*hit * kReticleReachMetres, kReticleNearestMetres, kReticleReachMetres);
}

} // namespace evr::vkcore
