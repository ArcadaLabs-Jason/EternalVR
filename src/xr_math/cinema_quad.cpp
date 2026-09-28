#include "xr_math/cinema_quad.hpp"

#include <cmath>

namespace evr::xr_math {

std::optional<QuadSize>
cinemaQuadSize(std::uint32_t pixelWidth, std::uint32_t pixelHeight, float widthMetres) {
    if (pixelWidth == 0 || pixelHeight == 0 || !(widthMetres > 0.0f)) {
        return std::nullopt;
    }
    const float aspect = static_cast<float>(pixelHeight) / static_cast<float>(pixelWidth);
    return QuadSize{widthMetres, widthMetres * aspect};
}

Pose cinemaQuadPose(const Pose& head, float distance) {
    const Vec3 forward = rotate(head.orientation, Vec3{0.0f, 0.0f, -1.0f});
    Vec3 flat{forward.x, 0.0f, forward.z};
    // Looking straight up or down leaves no usable yaw; fall back to the space's own forward.
    if (length(flat) < 1e-3f) {
        flat = Vec3{0.0f, 0.0f, -1.0f};
    }
    flat = normalize(flat);
    // Rotating -Z by `yaw` about +Y gives (-sin yaw, 0, -cos yaw).
    const float yaw = std::atan2(-flat.x, -flat.z);
    Pose pose;
    pose.orientation = Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, yaw);
    pose.position = head.position + flat * distance;
    return pose;
}

std::optional<CinemaFov>
cinemaFov(float gameFovX, float gameFovY, std::uint32_t width, std::uint32_t height, double aspect) {
    constexpr double kDegrees = 57.29577951308232;
    constexpr double kMaxFov = 170.0;
    if (width == 0 || height == 0 || !(aspect > 0.0) || !(gameFovX > 0.0f) || !(gameFovY > 0.0f) ||
        gameFovX >= kMaxFov || gameFovY >= kMaxFov) {
        return std::nullopt;
    }
    const double imageAspect = static_cast<double>(width) / static_cast<double>(height);
    if (imageAspect >= aspect / 1.005) {
        return std::nullopt; // no band to cut: the image is as wide as the flat display
    }
    const double tanX = std::tan(gameFovX / kDegrees / 2.0);
    const double tanY = std::tan(gameFovY / kDegrees / 2.0);
    const double flatX = tanY > tanX * 1.01 ? tanX : tanY * aspect;
    const double fovX = 2.0 * std::atan(flatX) * kDegrees;
    const double fovY = 2.0 * std::atan(flatX / imageAspect) * kDegrees;
    if (!(fovX < kMaxFov) || !(fovY < kMaxFov)) {
        return std::nullopt;
    }
    return CinemaFov{static_cast<float>(fovX), static_cast<float>(fovY)};
}

} // namespace evr::xr_math
