#include "features/roomscale/eye_separation.hpp"

#include "common/finite.hpp"

#include <cmath>

namespace evr::roomscale {

std::array<Vec3, 2> withSeparation(const std::array<Vec3, 2>& eyes, float ipdMetres) {
    if (!finiteInRange(ipdMetres, 0.04f, 0.09f)) {
        return eyes;
    }
    const Vec3 between = eyes[1] - eyes[0];
    const float distance = length(between);
    if (!(distance >= 0.01f) || !std::isfinite(distance)) {
        return eyes;
    }
    const Vec3 mid = (eyes[0] + eyes[1]) * 0.5f;
    const Vec3 half = between * (0.5f * ipdMetres / distance);
    return {mid - half, mid + half};
}

} // namespace evr::roomscale
