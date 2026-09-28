#include "common/mat4.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace evr {

Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 result;
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            float sum = 0.0f;
            for (std::size_t k = 0; k < 4; ++k) {
                sum += a.at(row, k) * b.at(k, col);
            }
            result.set(row, col, sum);
        }
    }
    return result;
}

Vec4 operator*(const Mat4& a, Vec4 v) {
    const std::array<float, 4> in{v.x, v.y, v.z, v.w};
    std::array<float, 4> out{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            out[row] += a.at(row, col) * in[col];
        }
    }
    return {out[0], out[1], out[2], out[3]};
}

Mat4 makeRigidTransform(Quat rotation, Vec3 translation) {
    // The columns of the rotation block are the images of the basis vectors.
    const Vec3 xAxis = rotate(rotation, {1.0f, 0.0f, 0.0f});
    const Vec3 yAxis = rotate(rotation, {0.0f, 1.0f, 0.0f});
    const Vec3 zAxis = rotate(rotation, {0.0f, 0.0f, 1.0f});

    Mat4 result = Mat4::identity();
    const std::array<Vec3, 4> columns{xAxis, yAxis, zAxis, translation};
    for (std::size_t col = 0; col < 4; ++col) {
        result.set(0, col, columns[col].x);
        result.set(1, col, columns[col].y);
        result.set(2, col, columns[col].z);
    }
    return result;
}

std::optional<Mat4> inverse(const Mat4& a) {
    // Work on an augmented pair [work | result], reducing `work` to the identity. Row-major scratch
    // arrays keep the row operations readable; the result is converted back at the end.
    std::array<std::array<float, 4>, 4> work{};
    std::array<std::array<float, 4>, 4> result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            work[row][col] = a.at(row, col);
            result[row][col] = (row == col) ? 1.0f : 0.0f;
        }
    }

    // The singularity threshold scales with the matrix, so a uniformly scaled matrix is accepted
    // whenever the unscaled one is. Pivots below about a millionth of the largest entry are treated as
    // zero, since float rounding dominates at that ratio. Projection matrices stay well clear of it:
    // their smallest pivot is the near-plane distance, around 1e-2 of their largest entry.
    constexpr float kRelativeSingularThreshold = 1e-6f;
    float largest = 0.0f;
    for (const float value : a.m) {
        if (!std::isfinite(value)) {
            return std::nullopt;
        }
        largest = std::max(largest, std::fabs(value));
    }
    if (largest == 0.0f) {
        return std::nullopt;
    }
    const float singularThreshold = largest * kRelativeSingularThreshold;

    for (std::size_t pivotCol = 0; pivotCol < 4; ++pivotCol) {
        std::size_t pivotRow = pivotCol;
        for (std::size_t row = pivotCol + 1; row < 4; ++row) {
            if (std::fabs(work[row][pivotCol]) > std::fabs(work[pivotRow][pivotCol])) {
                pivotRow = row;
            }
        }
        if (!(std::fabs(work[pivotRow][pivotCol]) >= singularThreshold)) {
            return std::nullopt;
        }
        std::swap(work[pivotRow], work[pivotCol]);
        std::swap(result[pivotRow], result[pivotCol]);

        const float invPivot = 1.0f / work[pivotCol][pivotCol];
        for (std::size_t col = 0; col < 4; ++col) {
            work[pivotCol][col] *= invPivot;
            result[pivotCol][col] *= invPivot;
        }

        for (std::size_t row = 0; row < 4; ++row) {
            if (row == pivotCol) {
                continue;
            }
            const float factor = work[row][pivotCol];
            for (std::size_t col = 0; col < 4; ++col) {
                work[row][col] -= factor * work[pivotCol][col];
                result[row][col] -= factor * result[pivotCol][col];
            }
        }
    }

    Mat4 out;
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            out.set(row, col, result[row][col]);
        }
    }
    return out;
}

} // namespace evr
