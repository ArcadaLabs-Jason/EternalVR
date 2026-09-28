#include "features/foveation/foveation_region.hpp"

#include "xr_math/projection.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <utility>

namespace evr::foveation {

namespace {

constexpr float kRightAngle = std::numbers::pi_v<float> / 2.0f;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

float degreesToRadians(float degrees) {
    return degrees * std::numbers::pi_v<float> / 180.0f;
}

// Points of the cone's boundary on the eye's tangent plane (x / depth, y / depth), relative to where
// the cone's axis lands. Parameterised by the angle t around the axis.
class ConeOnTangentPlane {
public:
    ConeOnTangentPlane(Vec3 axis, float halfAngle)
        : axis_(axis), cosHalf_(std::cos(halfAngle)), sinHalf_(std::sin(halfAngle)) {
        // Any vector not parallel to the axis gives a basis around it; the axis points forward (-Z),
        // so it is never parallel to both X and Y.
        const Vec3 helper = std::fabs(axis.y) < 0.9f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
        side_ = normalize(cross(axis, helper));
        up_ = cross(side_, axis);
        centerX_ = axis.x / -axis.z;
        centerY_ = axis.y / -axis.z;
    }

    // Offset from the axis point, in tangent units.
    [[nodiscard]] std::pair<double, double> offsetAt(double t) const {
        const Vec3 ring = side_ * static_cast<float>(std::cos(t)) + up_ * static_cast<float>(std::sin(t));
        const Vec3 d = axis_ * cosHalf_ + ring * sinHalf_;
        const double depth = -d.z;
        return {d.x / depth - centerX_, d.y / depth - centerY_};
    }

private:
    Vec3 axis_;
    float cosHalf_;
    float sinHalf_;
    Vec3 side_;
    Vec3 up_;
    double centerX_ = 0.0;
    double centerY_ = 0.0;
};

// Largest value of a smooth periodic function of t over [0, 2pi). A dense scan finds the peak and a
// golden-section search around the best sample refines it, so the result is exact to float
// precision rather than to the scan spacing. A cone boundary is a smooth closed curve, so every
// function evaluated on it here is smooth.
template <typename Function>
double maxAroundCone(Function f) {
    constexpr std::size_t kScanSamples = 256;
    constexpr double kStep = kTwoPi / kScanSamples;
    double bestT = 0.0;
    double best = f(0.0);
    for (std::size_t i = 1; i < kScanSamples; ++i) {
        const double t = kStep * static_cast<double>(i);
        if (const double value = f(t); value > best) {
            best = value;
            bestT = t;
        }
    }

    const double inverseGolden = (std::sqrt(5.0) - 1.0) / 2.0;
    double lo = bestT - kStep;
    double hi = bestT + kStep;
    for (int i = 0; i < 40; ++i) {
        const double a = hi - inverseGolden * (hi - lo);
        const double b = lo + inverseGolden * (hi - lo);
        if (f(a) < f(b)) {
            lo = a;
        } else {
            hi = b;
        }
    }
    return std::max(best, f(0.5 * (lo + hi)));
}

} // namespace

std::optional<FoveationRegion>
fullRateRegion(const xr_math::Fov& eyeFov, const Quat& eyeOrientationInHead, float halfAngleDegrees) {
    const float halfAngle = degreesToRadians(halfAngleDegrees);
    // Written as a positive range test so that NaN fails it.
    const bool halfAngleValid = halfAngle > 0.0f && halfAngle < kRightAngle;
    if (!halfAngleValid) {
        return std::nullopt;
    }

    // Head-forward expressed in the eye's own space.
    const Vec3 forward = normalize(rotate(conjugate(eyeOrientationInHead), Vec3{0.0f, 0.0f, -1.0f}));
    // The whole cone must stay in front of the eye: the angle from the eye's axis to head-forward
    // plus the half-angle must stay below 90 degrees.
    const float offAxis = std::acos(std::clamp(-forward.z, -1.0f, 1.0f));
    if (!(offAxis + halfAngle < kRightAngle)) {
        return std::nullopt;
    }

    // Project the centre with the eye's own projection so the NDC convention (Vulkan, y down) is
    // guaranteed to match the rendered image. Depth parameters do not affect x and y.
    const std::optional<Mat4> projection = xr_math::makeProjectionStandard(eyeFov, 0.1f, 100.0f);
    if (!projection) {
        return std::nullopt;
    }
    const Vec3 centerNdc = perspectiveDivide(*projection * toPoint(forward));

    // The cone's extent from the centre along each axis, on the tangent plane.
    const ConeOnTangentPlane cone(forward, halfAngle);
    const double extentX = std::max(maxAroundCone([&](double t) { return cone.offsetAt(t).first; }),
                                    maxAroundCone([&](double t) { return -cone.offsetAt(t).first; }));
    const double extentY = std::max(maxAroundCone([&](double t) { return cone.offsetAt(t).second; }),
                                    maxAroundCone([&](double t) { return -cone.offsetAt(t).second; }));

    // An ellipse with those extents as radii does not cover an off-axis cone, whose outline is tilted
    // and not centred on the axis point. Growing both radii by the same factor keeps the shape and
    // makes the worst boundary point land exactly on the ellipse.
    const double worst = maxAroundCone([&](double t) {
        const auto [dx, dy] = cone.offsetAt(t);
        return (dx / extentX) * (dx / extentX) + (dy / extentY) * (dy / extentY);
    });
    const double growth = std::sqrt(std::max(1.0, worst));

    // NDC spans 2 units across the frustum's tangent width and height.
    const xr_math::FovTangents tangents = xr_math::toTangents(eyeFov);
    const double ndcPerTangentX = 2.0 / (tangents.right - tangents.left);
    const double ndcPerTangentY = 2.0 / (tangents.up - tangents.down);

    return FoveationRegion{
        centerNdc.x,
        centerNdc.y,
        static_cast<float>(extentX * growth * ndcPerTangentX),
        static_cast<float>(extentY * growth * ndcPerTangentY),
    };
}

} // namespace evr::foveation
