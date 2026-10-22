#include "stereo_seq/screen_space_view.hpp"

#include <cmath>

namespace evr::stereo_seq {

std::optional<ViewShear> screenSpaceShear(const Matrix4& projection) {
    for (const float v : projection) {
        if (!std::isfinite(v)) {
            return std::nullopt;
        }
    }
    constexpr float kZero = 1e-6f;
    const float m0 = projection[0];
    const float m5 = projection[5];
    const float m14 = projection[14];
    // Rows 0 and 1: no y term in x, no x term in y, no translation; row 3: z only (a perspective divide).
    if (!(m0 > 1e-4f) || !(m5 > 1e-4f) || !(std::fabs(m14) > 1e-4f) || std::fabs(projection[1]) > kZero ||
        std::fabs(projection[3]) > kZero || std::fabs(projection[4]) > kZero ||
        std::fabs(projection[7]) > kZero || std::fabs(projection[12]) > kZero ||
        std::fabs(projection[13]) > kZero || std::fabs(projection[15]) > kZero) {
        return std::nullopt;
    }
    return ViewShear{projection[2] / m0, projection[6] / m5};
}

std::array<ParamRow, 3> shearRotationRows(const std::array<ParamRow, 3>& rows, const ViewShear& shear) {
    std::array<ParamRow, 3> out = rows;
    for (ParamRow& row : out) {
        row[0] += shear.x * row[2];
        row[1] += shear.y * row[2];
    }
    return out;
}

} // namespace evr::stereo_seq
