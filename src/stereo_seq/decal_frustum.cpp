#include "stereo_seq/decal_frustum.hpp"

#include <cmath>
#include <numbers>

namespace evr::stereo_seq {

namespace {

// tan(fov / 2), or 0 for a FOV that cannot be a view's.
float halfTangent(float fovDegrees) {
    if (!std::isfinite(fovDegrees) || !(fovDegrees > 0.0f) || !(fovDegrees < 180.0f)) {
        return 0.0f;
    }
    return std::tan(fovDegrees * (std::numbers::pi_v<float> / 360.0f));
}

bool finite3(const float* v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

// The plane through `origin` whose outward normal is a * axisA + b * forward.
std::array<float, 4>
sidePlane(const std::array<float, 3>& origin, const float* axisA, float a, const float* forward, float b) {
    std::array<float, 4> p{};
    for (std::size_t i = 0; i < 3; ++i) {
        p[i] = a * axisA[i] + b * forward[i];
    }
    p[3] = p[0] * origin[0] + p[1] * origin[1] + p[2] * origin[2];
    return p;
}

} // namespace

Matrix4 pairDecalProjection(const Matrix4& latched, float fovXDegrees, float fovYDegrees) {
    const float m0 = latched[0];
    const float m2 = latched[2];
    const float m5 = latched[5];
    const float m6 = latched[6];
    if (!std::isfinite(m0) || !std::isfinite(m2) || !std::isfinite(m5) || !std::isfinite(m6) ||
        !(m0 > 1e-4f) || !(m5 > 1e-4f)) {
        return latched;
    }
    // x_ndc = m0 * tan - m2 and y_ndc = m5 * tan - m6 over the frustum's tangents: the edges sit at
    // tan = (m2 -+ 1) / m0 and (m6 -+ 1) / m5; the wider side of each axis contains both.
    const float eyeX = std::fmax(std::fabs(m2 - 1.0f), std::fabs(m2 + 1.0f)) / m0;
    const float eyeY = std::fmax(std::fabs(m6 + 1.0f), std::fabs(m6 - 1.0f)) / m5;
    const float flatX = halfTangent(fovXDegrees);
    const float flatY = halfTangent(fovYDegrees);
    // Already centred and at least as wide as the flat game (a mono view): unchanged, bit for bit.
    constexpr float kSlack = 1.0f - 1e-5f;
    if (m2 == 0.0f && m6 == 0.0f && eyeX >= flatX * kSlack && eyeY >= flatY * kSlack) {
        return latched;
    }
    Matrix4 m = latched;
    m[0] = 1.0f / std::fmax(eyeX, flatX);
    m[2] = 0.0f;
    m[5] = 1.0f / std::fmax(eyeY, flatY);
    m[6] = 0.0f;
    return m;
}

std::optional<SideFrustum>
sideFrustum(const std::array<float, 3>& origin, const std::array<float, 9>& axis, const Matrix4& projection) {
    const float m0 = projection[0];
    const float m2 = projection[2];
    const float m5 = projection[5];
    const float m6 = projection[6];
    if (!finite3(origin.data()) || !finite3(axis.data()) || !finite3(axis.data() + 3) ||
        !finite3(axis.data() + 6) || !std::isfinite(m0) || !std::isfinite(m2) || !std::isfinite(m5) ||
        !std::isfinite(m6) || !(m0 > 1e-4f) || !(m5 > 1e-4f)) {
        return std::nullopt;
    }
    const float left = (m2 - 1.0f) / m0;
    const float right = (m2 + 1.0f) / m0;
    const float down = (m6 - 1.0f) / m5;
    const float up = (m6 + 1.0f) / m5;
    const float* forward = axis.data();
    const float* leftAxis = axis.data() + 3;
    const float* upAxis = axis.data() + 6;
    // A direction d is past the right edge when right . d > tanRight * forward . d, with right = -leftAxis.
    SideFrustum f;
    f.planes[0] = sidePlane(origin, leftAxis, -1.0f, forward, -right); // right edge
    f.planes[1] = sidePlane(origin, leftAxis, 1.0f, forward, left);    // left edge
    f.planes[2] = sidePlane(origin, upAxis, 1.0f, forward, -up);       // top edge
    f.planes[3] = sidePlane(origin, upAxis, -1.0f, forward, down);     // bottom edge
    return f;
}

bool boxOutside(const SideFrustum& frustum, const float* mins, const float* maxs) {
    if (!finite3(mins) || !finite3(maxs)) {
        return true;
    }
    for (const auto& p : frustum.planes) {
        // The box's corner deepest inside this plane.
        float nearest = 0.0f;
        for (std::size_t i = 0; i < 3; ++i) {
            nearest += p[i] * (p[i] > 0.0f ? mins[i] : maxs[i]);
        }
        if (nearest > p[3]) {
            return true;
        }
    }
    return false;
}

} // namespace evr::stereo_seq
