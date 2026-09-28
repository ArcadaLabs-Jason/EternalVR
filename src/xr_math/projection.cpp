#include "xr_math/projection.hpp"

#include <cmath>
#include <initializer_list>
#include <numbers>

namespace evr::xr_math {

namespace {

// Writes the X, Y and W rows, which do not depend on the depth mode.
//
// For a view-space point (x, y, z) with distance d = -z, the tangent of its horizontal angle is x / d.
// That tangent must map linearly from [left, right] to NDC [-1, 1]:
//     ndc.x = (2 * x/d - (right + left)) / (right - left)
// Multiplying through by w = d gives the clip-space row
//     clip.x = 2/(right - left) * x + (right + left)/(right - left) * z
// The Y row is the same with [down, up], then negated because Vulkan NDC +Y points down.
void writeLateralRows(Mat4& m, const FovTangents& t) {
    const float width = t.right - t.left;
    const float height = t.up - t.down;

    m.set(0, 0, 2.0f / width);
    m.set(0, 2, (t.right + t.left) / width);

    m.set(1, 1, -2.0f / height);
    m.set(1, 2, -(t.up + t.down) / height);

    m.set(3, 2, -1.0f);
}

// Depth row for [near -> 0, far -> 1]:
//     ndc.z = (A*z + B) / -z  with  A = -far/(far - near),  B = -far*near/(far - near)
// Check: z = -near gives (A*-near + B) / near = 0, and z = -far gives 1.
void writeStandardDepth(Mat4& m, float nearZ, float farZ) {
    const float range = farZ - nearZ;
    m.set(2, 2, -farZ / range);
    m.set(2, 3, -(farZ * nearZ) / range);
}

// Depth row for [near -> 1, infinity -> 0]: ndc.z = near / d. This is the limit of a reversed
// finite projection as far goes to infinity, and it has no far-plane term at all.
void writeReversedInfiniteDepth(Mat4& m, float nearZ) {
    m.set(2, 2, 0.0f);
    m.set(2, 3, nearZ);
}

// Every half-angle strictly inside (-90, 90) degrees, and a non-empty span on both axes. Written as
// positive tests so that NaN fails them.
bool fovIsProjectable(const Fov& fov) {
    constexpr float kRightAngle = std::numbers::pi_v<float> / 2.0f;
    for (const float angle : {fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown}) {
        if (!(std::fabs(angle) < kRightAngle)) {
            return false;
        }
    }
    const FovTangents t = toTangents(fov);
    return t.right - t.left > 0.0f && t.up - t.down > 0.0f;
}

} // namespace

std::optional<Mat4> makeProjection(const ProjectionParams& params) {
    if (!fovIsProjectable(params.fov) || !(params.nearZ > 0.0f && std::isfinite(params.nearZ))) {
        return std::nullopt;
    }

    Mat4 m;
    writeLateralRows(m, toTangents(params.fov));

    switch (params.depthMode) {
    case DepthMode::Standard:
        if (!(params.farZ > params.nearZ && std::isfinite(params.farZ))) {
            return std::nullopt;
        }
        writeStandardDepth(m, params.nearZ, params.farZ);
        break;
    case DepthMode::ReversedInfinite:
        writeReversedInfiniteDepth(m, params.nearZ);
        break;
    }
    return m;
}

std::optional<Mat4> makeProjectionStandard(const Fov& fov, float nearZ, float farZ) {
    return makeProjection({fov, nearZ, farZ, DepthMode::Standard});
}

std::optional<Mat4> makeProjectionReversedInfinite(const Fov& fov, float nearZ) {
    return makeProjection({fov, nearZ, 0.0f, DepthMode::ReversedInfinite});
}

} // namespace evr::xr_math
