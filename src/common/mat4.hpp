#pragma once

// 4x4 matrix for transforms and projections.
//
// Convention:
//   - Column vectors: a point is transformed as p' = M * p, and M = A * B applies B first.
//   - Column-major storage: element (row, col) lives at m[col * 4 + row]. This is the layout Vulkan,
//     GLSL and OpenXR's sample math expect, so a Mat4 can be uploaded to a uniform buffer unchanged.

#include "common/quat.hpp"
#include "common/vector.hpp"

#include <array>
#include <cstddef>
#include <optional>

namespace evr {

struct Mat4 {
    std::array<float, 16> m{};

    static constexpr Mat4 identity() {
        Mat4 result;
        result.set(0, 0, 1.0f);
        result.set(1, 1, 1.0f);
        result.set(2, 2, 1.0f);
        result.set(3, 3, 1.0f);
        return result;
    }

    [[nodiscard]] constexpr float at(std::size_t row, std::size_t col) const { return m[col * 4 + row]; }
    constexpr void set(std::size_t row, std::size_t col, float value) { m[col * 4 + row] = value; }

    friend constexpr bool operator==(const Mat4& a, const Mat4& b) = default;
};

Mat4 operator*(const Mat4& a, const Mat4& b);
Vec4 operator*(const Mat4& a, Vec4 v);

// Rigid transform that rotates by `rotation` and then translates by `translation`.
Mat4 makeRigidTransform(Quat rotation, Vec3 translation);

// General inverse by Gauss-Jordan elimination with partial pivoting. Returns nullopt when the matrix
// has a non-finite entry or is singular, or so close to it relative to its own scale that the
// inverse would be meaningless in float precision.
std::optional<Mat4> inverse(const Mat4& a);

} // namespace evr
