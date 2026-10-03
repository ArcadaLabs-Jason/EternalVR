#include "features/foveation/foveation_region.hpp"

#include "xr_math/projection.hpp"

#include <cmath>
#include <numbers>
#include <optional>

namespace evr::foveation {

namespace {

constexpr float kRightAngle = std::numbers::pi_v<float> / 2.0f;

float degreesToRadians(float degrees) {
    return degrees * std::numbers::pi_v<float> / 180.0f;
}

} // namespace

std::optional<FoveationRegion>
foveationRegion(const xr_math::Fov& eyeFov, const Quat& eyeOrientationInHead, float halfAngleDegrees) {
    const float halfAngle = degreesToRadians(halfAngleDegrees);
    // Written as a positive range test so that NaN fails it.
    const bool halfAngleValid = halfAngle > 0.0f && halfAngle < kRightAngle;
    if (!halfAngleValid) {
        return std::nullopt;
    }

    // Head-forward expressed in the eye's own space; it must be in front of the eye.
    const Vec3 forward = normalize(rotate(conjugate(eyeOrientationInHead), Vec3{0.0f, 0.0f, -1.0f}));
    if (!(forward.z < 0.0f)) {
        return std::nullopt;
    }

    // Project the centre with the eye's own projection so the NDC convention (Vulkan, y down) is
    // guaranteed to match the rendered image. Depth parameters do not affect x and y.
    const std::optional<Mat4> projection = xr_math::makeProjectionStandard(eyeFov, 0.1f, 100.0f);
    if (!projection) {
        return std::nullopt;
    }
    const Vec3 center = perspectiveDivide(*projection * toPoint(forward));
    const bool inside = center.x > -1.0f && center.x < 1.0f && center.y > -1.0f && center.y < 1.0f;
    if (!inside) {
        return std::nullopt;
    }

    // NDC maps the tangent width W and height H to 2 units each, so four quarter ellipses with radii f times
    // each side's NDC extent cover pi / 4 * f^2 * W * H of the tangent plane, whatever the centre. Equal to
    // the cone's pi * tan^2(half-angle): f = 2 tan(half-angle) / sqrt(W * H).
    const xr_math::FovTangents tangents = xr_math::toTangents(eyeFov);
    const double width = static_cast<double>(tangents.right) - tangents.left;
    const double height = static_cast<double>(tangents.up) - tangents.down;
    const double fraction = 2.0 * std::tan(static_cast<double>(halfAngle)) / std::sqrt(width * height);

    return FoveationRegion{
        center.x,
        center.y,
        static_cast<float>(fraction * (1.0 + center.x)),
        static_cast<float>(fraction * (1.0 - center.x)),
        static_cast<float>(fraction * (1.0 + center.y)),
        static_cast<float>(fraction * (1.0 - center.y)),
    };
}

} // namespace evr::foveation
