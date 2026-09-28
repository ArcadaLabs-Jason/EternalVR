#include "features/menu/panel_pointer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace evr::menu {

namespace {

// A little slack at the edges so a ray exactly on an edge counts despite rounding.
constexpr float kEdgeSlack = 1e-5f;

} // namespace

std::optional<PanelHit> intersectPanel(const Panel& panel, Vec3 origin, Vec3 direction) {
    if (!(panel.width > 0.0f) || !(panel.height > 0.0f) || length(direction) == 0.0f) {
        return std::nullopt;
    }
    const Pose toPanel = inverse(panel.pose);
    const Vec3 o = transformPoint(toPanel, origin);
    const Vec3 d = transformDirection(toPanel, normalize(direction));
    // In front of the visible side and heading toward it.
    if (!(o.z > 0.0f) || !(d.z < 0.0f)) {
        return std::nullopt;
    }
    const float t = -o.z / d.z;
    const Vec3 local = o + d * t;
    const float halfW = panel.width * 0.5f;
    const float halfH = panel.height * 0.5f;
    if (std::fabs(local.x) > halfW + kEdgeSlack || std::fabs(local.y) > halfH + kEdgeSlack) {
        return std::nullopt;
    }
    PanelHit hit;
    hit.u = std::clamp(local.x / panel.width + 0.5f, 0.0f, 1.0f);
    hit.v = std::clamp(0.5f - local.y / panel.height, 0.0f, 1.0f);
    hit.distance = t;
    hit.point = transformPoint(panel.pose, Vec3{local.x, local.y, 0.0f});
    return hit;
}

std::optional<PanelHit> intersectPanel(const Panel& panel, const Pose& aim) {
    return intersectPanel(panel, aim.position, rotate(aim.orientation, Vec3{0.0f, 0.0f, -1.0f}));
}

Pose localFromRoom(const Pose& roomFromLocal, const Pose& room) {
    Pose local = compose(inverse(roomFromLocal), room);
    local.orientation = normalize(local.orientation);
    return local;
}

CursorPixel cursorPixel(float u, float v, std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        return {};
    }
    const auto toPixel = [](float t, std::uint32_t size) {
        const float clamped = std::isfinite(t) ? std::clamp(t, 0.0f, 1.0f) : 0.0f;
        const auto p = static_cast<std::int32_t>(std::floor(clamped * static_cast<float>(size)));
        return std::min(p, static_cast<std::int32_t>(size) - 1);
    };
    return {toPixel(u, width), toPixel(v, height)};
}

Pose dotPose(const Panel& panel, float u, float v, float offset) {
    const Vec3 local{(u - 0.5f) * panel.width, (0.5f - v) * panel.height, offset};
    Pose pose;
    pose.orientation = panel.pose.orientation;
    pose.position = transformPoint(panel.pose, local);
    return pose;
}

Quat quatFromAxes(Vec3 x, Vec3 y, Vec3 z) {
    // The rotation matrix with columns x, y, z (m[row][col]).
    const float m00 = x.x, m01 = y.x, m02 = z.x;
    const float m10 = x.y, m11 = y.y, m12 = z.y;
    const float m20 = x.z, m21 = y.z, m22 = z.z;
    const float trace = m00 + m11 + m22;
    Quat q;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    return normalize(q);
}

std::optional<BeamQuad> beamQuad(Vec3 from, Vec3 to, Vec3 eye, float thickness) {
    const Vec3 along = to - from;
    const float len = length(along);
    if (!(len > 1e-4f) || !(thickness > 0.0f)) {
        return std::nullopt;
    }
    const Vec3 y = along * (1.0f / len);
    const Vec3 centre = from + along * 0.5f;
    // The quad's face points at the eye: the part of (eye - centre) across the beam.
    Vec3 toEye = eye - centre;
    Vec3 z = toEye - y * dot(toEye, y);
    if (length(z) < 1e-4f) {
        // The eye sits on the beam's line: any side will do.
        z = std::fabs(y.y) < 0.9f ? cross(y, Vec3{0.0f, 1.0f, 0.0f}) : cross(y, Vec3{1.0f, 0.0f, 0.0f});
    }
    z = normalize(z);
    const Vec3 x = cross(y, z);
    BeamQuad quad;
    quad.pose.orientation = quatFromAxes(x, y, z);
    quad.pose.position = centre;
    quad.width = thickness;
    quad.length = len;
    return quad;
}

std::vector<std::uint8_t> beamImage(std::uint32_t width, std::uint32_t height) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4, 0);
    if (width == 0 || height == 0) {
        return pixels;
    }
    // A pale blue line: full in the middle, soft to the sides, 25% at the far end, 85% at the hand.
    constexpr float kColour[3] = {0.75f, 0.9f, 1.0f};
    for (std::uint32_t row = 0; row < height; ++row) {
        const float along = height > 1 ? static_cast<float>(row) / static_cast<float>(height - 1) : 1.0f;
        const float fade = 0.25f + 0.6f * along;
        for (std::uint32_t col = 0; col < width; ++col) {
            const float across = (static_cast<float>(col) + 0.5f) / static_cast<float>(width) * 2.0f - 1.0f;
            const float side = std::clamp(1.0f - across * across, 0.0f, 1.0f);
            const float alpha = fade * side;
            std::uint8_t* p = pixels.data() + (static_cast<std::size_t>(row) * width + col) * 4;
            for (int c = 0; c < 3; ++c) {
                p[c] = static_cast<std::uint8_t>(std::lround(kColour[c] * alpha * 255.0f));
            }
            p[3] = static_cast<std::uint8_t>(std::lround(alpha * 255.0f));
        }
    }
    return pixels;
}

} // namespace evr::menu
