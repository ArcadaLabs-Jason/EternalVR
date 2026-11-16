#include "xr_math/edge_margin.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <numbers>

namespace evr::xr_math {

namespace {

// As projection.cpp's test: every half-angle strictly inside (-90, 90) degrees and a span on both axes.
bool projectable(const Fov& fov) {
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

std::optional<Fov> widenFov(const Fov& fov, float factor) {
    if (!(std::isfinite(factor) && factor >= 1.0f) || !projectable(fov)) {
        return std::nullopt;
    }
    const FovTangents t = toTangents(fov);
    const Fov wide = fromTangents({t.left * factor, t.right * factor, t.up * factor, t.down * factor});
    if (!projectable(wide)) {
        return std::nullopt;
    }
    return wide;
}

std::optional<ImageRect> viewRectInImage(const Fov& rendered, const Fov& view) {
    if (!projectable(rendered) || !projectable(view)) {
        return std::nullopt;
    }
    const FovTangents r = toTangents(rendered);
    const FovTangents v = toTangents(view);
    constexpr double kTolerance = 1e-6;
    if (v.left < r.left - kTolerance || v.right > r.right + kTolerance || v.up > r.up + kTolerance ||
        v.down < r.down - kTolerance) {
        return std::nullopt;
    }
    const double width = static_cast<double>(r.right) - r.left;
    const double height = static_cast<double>(r.up) - r.down;
    ImageRect out;
    out.x = (static_cast<double>(v.left) - r.left) / width;
    out.width = (static_cast<double>(v.right) - v.left) / width;
    out.y = (static_cast<double>(r.up) - v.up) / height;
    out.height = (static_cast<double>(v.up) - v.down) / height;
    return out;
}

std::optional<ImageRect> marginViewRect(const Fov& rendered, const Fov& view) {
    if (!projectable(rendered) || !projectable(view)) {
        return std::nullopt;
    }
    const FovTangents r = toTangents(rendered);
    const FovTangents v = toTangents(view);
    constexpr double kTolerance = 1e-6;
    const bool wider = v.left - r.left > kTolerance || r.right - v.right > kTolerance ||
                       r.up - v.up > kTolerance || v.down - r.down > kTolerance;
    return wider ? viewRectInImage(rendered, view) : std::nullopt;
}

PixelBox toPixels(const ImageRect& rect, std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        return {};
    }
    // An edge at fraction f of a side of n pixels, rounded to a pixel edge within [0, n].
    const auto edge = [](double f, std::uint32_t n) {
        const double e = std::round((std::isfinite(f) ? f : 0.0) * static_cast<double>(n));
        return static_cast<std::uint32_t>(std::clamp(e, 0.0, static_cast<double>(n)));
    };
    std::uint32_t x0 = edge(rect.x, width);
    std::uint32_t y0 = edge(rect.y, height);
    const std::uint32_t x1 = std::max(edge(rect.x + rect.width, width), std::min(x0 + 1, width));
    const std::uint32_t y1 = std::max(edge(rect.y + rect.height, height), std::min(y0 + 1, height));
    x0 = std::min(x0, x1 - 1);
    y0 = std::min(y0, y1 - 1);
    return {x0, y0, x1 - x0, y1 - y0};
}

} // namespace evr::xr_math
