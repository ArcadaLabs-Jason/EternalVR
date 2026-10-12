#include "features/foveation/foveation_region.hpp"

#include "xr_math/projection.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace evr::foveation {

namespace {

constexpr float kRightAngle = std::numbers::pi_v<float> / 2.0f;

float degreesToRadians(float degrees) {
    return degrees * std::numbers::pi_v<float> / 180.0f;
}

// Directions in double: the texel tests compare cosines near 1.
struct Dir {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

double dot(const Dir& a, const Dir& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Dir cross(const Dir& a, const Dir& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double tangentX(const xr_math::FovTangents& t, double ndcX) {
    return t.left + (ndcX + 1.0) * 0.5 * (static_cast<double>(t.right) - t.left);
}

double tangentY(const xr_math::FovTangents& t, double ndcY) {
    return t.up - (ndcY + 1.0) * 0.5 * (static_cast<double>(t.up) - t.down);
}

// The cosine of the angle between the unit `f` and `d`.
double cosTo(const Dir& f, const Dir& d) {
    return dot(f, d) / std::sqrt(dot(d, d));
}

// The largest cosine to the unit `f` along the great-circle arc from `a` to `b` (both in front of the eye,
// so the arc is under 180 degrees): at an end, or where f's projection onto the arc's plane falls on it.
double bestCosOnArc(const Dir& f, const Dir& a, const Dir& b) {
    double best = std::max(cosTo(f, a), cosTo(f, b));
    const Dir n = cross(a, b);
    const double nn = dot(n, n);
    if (nn > 0.0) {
        const double s = dot(f, n) / nn;
        const Dir p{f.x - s * n.x, f.y - s * n.y, f.z - s * n.z};
        const double pp = dot(p, p);
        if (pp > 0.0 && dot(cross(a, p), n) >= 0.0 && dot(cross(p, b), n) >= 0.0) {
            // f . p = |p|^2 for the projection of a unit f, so the cosine to p is |p|.
            best = std::max(best, std::sqrt(pp));
        }
    }
    return best;
}

} // namespace

std::optional<FoveationRegion>
foveationRegion(const xr_math::Fov& eyeFov, const Quat& eyeOrientationInHead, float halfAngleDegrees) {
    const float halfAngle = degreesToRadians(halfAngleDegrees);
    // Written as a positive range test so that NaN fails it.
    const bool halfAngleValid = halfAngle > 0.0f && halfAngle < kRightAngle;
    if (!halfAngleValid) {
        return std::nullopt;
    }

    // Head-forward expressed in the eye's own space; it must be in front of the eye.
    const Vec3 forward = normalize(rotate(conjugate(eyeOrientationInHead), Vec3{0.0f, 0.0f, -1.0f}));
    if (!(forward.z < 0.0f)) {
        return std::nullopt;
    }

    // Project the centre with the eye's own projection so the NDC convention (Vulkan, y down) is
    // guaranteed to match the rendered image. Depth parameters do not affect x and y.
    const std::optional<Mat4> projection = xr_math::makeProjectionStandard(eyeFov, 0.1f, 100.0f);
    if (!projection) {
        return std::nullopt;
    }
    const Vec3 center = perspectiveDivide(*projection * toPoint(forward));
    const bool inside = center.x > -1.0f && center.x < 1.0f && center.y > -1.0f && center.y < 1.0f;
    if (!inside) {
        return std::nullopt;
    }
    return FoveationRegion{forward, halfAngle, xr_math::toTangents(eyeFov), center.x, center.y};
}

double angleFromForward(const FoveationRegion& region, float x, float y) {
    const Dir f{region.forward.x, region.forward.y, region.forward.z};
    const Dir d{tangentX(region.tangents, x), tangentY(region.tangents, y), -1.0};
    return std::acos(std::clamp(cosTo(f, d), -1.0, 1.0));
}

bool touches(const FoveationRegion& region, float x0, float x1, float y0, float y1) {
    const xr_math::FovTangents& t = region.tangents;
    if (!(region.halfAngle > 0.0f && region.halfAngle < kRightAngle && t.right > t.left && t.up > t.down &&
          region.forward.z < 0.0f)) {
        return false;
    }
    const Dir f{region.forward.x, region.forward.y, region.forward.z};
    // The rectangle on the plane z = -1; NDC y runs down, the tangent up.
    const double left = tangentX(t, x0);
    const double right = tangentX(t, x1);
    const double top = tangentY(t, y0);
    const double bottom = tangentY(t, y1);
    // Head-forward itself goes through the rectangle: angle 0.
    const double fx = f.x / -f.z;
    const double fy = f.y / -f.z;
    if (fx >= left && fx <= right && fy >= bottom && fy <= top) {
        return true;
    }
    // Otherwise the nearest direction is on an edge: the cone of every angle meets the plane in a convex
    // region around head-forward's projection (a circle without canting, an ellipse with it).
    const Dir topLeft{left, top, -1.0};
    const Dir topRight{right, top, -1.0};
    const Dir bottomRight{right, bottom, -1.0};
    const Dir bottomLeft{left, bottom, -1.0};
    const double best =
        std::max({bestCosOnArc(f, topLeft, topRight), bestCosOnArc(f, topRight, bottomRight),
                  bestCosOnArc(f, bottomRight, bottomLeft), bestCosOnArc(f, bottomLeft, topLeft)});
    return best >= std::cos(static_cast<double>(region.halfAngle));
}

} // namespace evr::foveation
