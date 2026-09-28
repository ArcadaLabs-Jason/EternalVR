#include "xr_math/enclosing_fov.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>

namespace evr::xr_math {

namespace {

// Depth along the centre camera's forward axis (-Z).
float depthOf(Vec3 v) {
    return -v.z;
}

// Running bounds on the centre camera's tangent plane.
class TangentBounds {
public:
    void include(float tanX, float tanY) {
        bounds_.left = std::min(bounds_.left, tanX);
        bounds_.right = std::max(bounds_.right, tanX);
        bounds_.down = std::min(bounds_.down, tanY);
        bounds_.up = std::max(bounds_.up, tanY);
    }

    // Tangents of a point or direction with positive depth.
    void includeVector(Vec3 v) { include(v.x / depthOf(v), v.y / depthOf(v)); }

    [[nodiscard]] const FovTangents& tangents() const { return bounds_; }

private:
    static constexpr float kInf = std::numeric_limits<float>::infinity();
    FovTangents bounds_{kInf, -kInf, -kInf, kInf};
};

// The four edges of an eye frustum, as head-space directions.
std::array<Vec3, 4> cornerRaysInHead(const EyeView& eye) {
    const FovTangents t = toTangents(eye.fov);
    const std::array<Vec3, 4> eyeSpaceCorners{{
        {t.left, t.up, -1.0f},
        {t.right, t.up, -1.0f},
        {t.left, t.down, -1.0f},
        {t.right, t.down, -1.0f},
    }};

    std::array<Vec3, 4> headSpace{};
    for (std::size_t i = 0; i < eyeSpaceCorners.size(); ++i) {
        headSpace[i] = transformDirection(eye.poseInHead, eyeSpaceCorners[i]);
    }
    return headSpace;
}

// Adds the eight candidate extremes of one eye. Returns false if the eye cannot be enclosed.
bool includeEye(TangentBounds& bounds, const EyeView& eye, float minDepth) {
    const Vec3 apex = eye.poseInHead.position;
    const float apexDepth = depthOf(apex);

    for (const Vec3& ray : cornerRaysInHead(eye)) {
        const float rayDepth = depthOf(ray);
        if (rayDepth <= 0.0f) {
            return false;
        }

        // Unbounded edge: the limit of the tangent along the ray is the ray's own tangent.
        bounds.includeVector(ray);

        // Vertex: where the ray crosses the minimum-depth plane, or the apex when the eye already
        // sits beyond that plane (then the whole frustum is in the region).
        const float distanceAlongRay = std::max(0.0f, (minDepth - apexDepth) / rayDepth);
        bounds.includeVector(apex + ray * distanceAlongRay);
    }
    return true;
}

} // namespace

std::optional<Fov> enclosingFov(std::span<const EyeView> eyes, float minDepth, EnclosingShape shape) {
    if (eyes.empty() || minDepth <= 0.0f) {
        return std::nullopt;
    }

    TangentBounds bounds;
    for (const EyeView& eye : eyes) {
        if (!includeEye(bounds, eye, minDepth)) {
            return std::nullopt;
        }
    }

    const Fov fov = fromTangents(bounds.tangents());
    if (shape == EnclosingShape::Symmetric) {
        return makeSymmetric(fov);
    }
    return fov;
}

} // namespace evr::xr_math
