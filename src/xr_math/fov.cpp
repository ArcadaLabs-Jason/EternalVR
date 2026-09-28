#include "xr_math/fov.hpp"

#include <algorithm>
#include <cmath>

namespace evr::xr_math {

FovTangents toTangents(const Fov& fov) {
    return {
        std::tan(fov.angleLeft),
        std::tan(fov.angleRight),
        std::tan(fov.angleUp),
        std::tan(fov.angleDown),
    };
}

Fov fromTangents(const FovTangents& tangents) {
    return {
        std::atan(tangents.left),
        std::atan(tangents.right),
        std::atan(tangents.up),
        std::atan(tangents.down),
    };
}

Fov makeSymmetric(const Fov& fov) {
    const float horizontal = std::max(std::fabs(fov.angleLeft), std::fabs(fov.angleRight));
    const float vertical = std::max(std::fabs(fov.angleUp), std::fabs(fov.angleDown));
    return {-horizontal, horizontal, vertical, -vertical};
}

} // namespace evr::xr_math
