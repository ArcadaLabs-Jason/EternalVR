#include "stereo_seq/centered_matrix.hpp"

#include <cmath>
#include <cstddef>

namespace evr::stereo_seq {

std::optional<CenteredDepth> centeredDepthOf(const Matrix4& m) {
    // Row 3 = -V2 (a unit vector), row 2 = a * V2 + (0, 0, 0, b).
    std::size_t k = 0;
    for (std::size_t i = 1; i < 3; ++i) {
        if (std::fabs(m[12 + i]) > std::fabs(m[12 + k])) {
            k = i;
        }
    }
    const float v = -m[12 + k];
    if (!std::isfinite(v) || std::fabs(v) < 0.1f || std::fabs(m[15]) > 1e-3f) {
        return std::nullopt;
    }
    CenteredDepth d;
    d.a = m[8 + k] / v;
    d.b = m[11];
    for (std::size_t i = 0; i < 3; ++i) {
        const float want = d.a * -m[12 + i];
        if (std::fabs(want - m[8 + i]) > 1e-3f * (1.0f + std::fabs(want))) {
            return std::nullopt;
        }
    }
    if (!std::isfinite(d.a) || !std::isfinite(d.b)) {
        return std::nullopt;
    }
    return d;
}

bool retargetViewProjection(Matrix4& m, const Matrix4& eye) {
    const auto norm3 = [&m](std::size_t row) {
        return std::sqrt(m[row * 4] * m[row * 4] + m[row * 4 + 1] * m[row * 4 + 1] +
                         m[row * 4 + 2] * m[row * 4 + 2]);
    };
    const float s0 = norm3(0);
    const float s1 = norm3(1);
    const float s3 = norm3(3);
    // Row 3 of the product is -(view row 2), a unit rotation row.
    if (!std::isfinite(s0) || !std::isfinite(s1) || s0 < 1e-3f || s1 < 1e-3f ||
        std::fabs(s3 - 1.0f) > 1e-2f) {
        return false;
    }
    Matrix4 out = m;
    for (std::size_t j = 0; j < 4; ++j) {
        const float v0 = m[j] / s0;
        const float v1 = m[4 + j] / s1;
        const float v2 = -m[12 + j];
        out[j] = eye[0] * v0 + eye[1] * v1 + eye[2] * v2;
        out[4 + j] = eye[4] * v0 + eye[5] * v1 + eye[6] * v2;
    }
    for (const float v : out) {
        if (!std::isfinite(v)) {
            return false;
        }
    }
    m = out;
    return true;
}

void setCenteredDepth(Matrix4& m, const CenteredDepth& depth) {
    for (std::size_t i = 0; i < 3; ++i) {
        m[8 + i] = depth.a * -m[12 + i];
    }
    m[11] = depth.b + depth.a * -m[15];
}

} // namespace evr::stereo_seq
