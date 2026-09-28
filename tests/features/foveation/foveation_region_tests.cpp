#include "features/foveation/foveation_region.hpp"

#include "xr_math/projection.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <numbers>
#include <ostream>

using evr::Quat;
using evr::foveation::fullRateRegion;
using evr::xr_math::Fov;

namespace {

constexpr float kQuarterPi = std::numbers::pi_v<float> / 4.0f;
constexpr evr::Vec3 kUp{0.0f, 1.0f, 0.0f};

// Typical of current headsets: the temporal side (outer) is wider than the nasal side, and the
// bottom slightly wider than the top.
constexpr Fov kLeftEye{-0.95f, 0.72f, 0.80f, -0.88f};
constexpr Fov kRightEye{-0.72f, 0.95f, 0.80f, -0.88f};

float tanDegrees(float degrees) {
    return std::tan(degrees * std::numbers::pi_v<float> / 180.0f);
}

float radians(float degrees) {
    return degrees * std::numbers::pi_v<float> / 180.0f;
}

// Samples the boundary of the cone of `halfAngleDegrees` around head-forward, projects each sample
// into the eye's image as the renderer does, and returns the largest ellipse value (dx/rx)^2 +
// (dy/ry)^2. The region covers the cone when this is at most 1.
float worstEllipseValue(const Fov& eyeFov, const Quat& eyeInHead, float halfAngleDegrees) {
    const auto region = fullRateRegion(eyeFov, eyeInHead, halfAngleDegrees);
    REQUIRE(region.has_value());
    const auto projection = evr::xr_math::makeProjectionStandard(eyeFov, 0.1f, 100.0f);
    REQUIRE(projection.has_value());

    const float halfAngle = radians(halfAngleDegrees);
    float worst = 0.0f;
    constexpr int kSamples = 3600;
    for (int i = 0; i < kSamples; ++i) {
        const float t = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / kSamples;
        // A head-space direction on the cone around head-forward (-Z).
        const evr::Vec3 inHead{std::sin(halfAngle) * std::cos(t), std::sin(halfAngle) * std::sin(t),
                               -std::cos(halfAngle)};
        const evr::Vec3 inEye = evr::rotate(evr::conjugate(eyeInHead), inHead);
        const evr::Vec3 ndc = evr::perspectiveDivide(*projection * evr::toPoint(inEye));
        const float dx = (ndc.x - region->centerX) / region->radiusX;
        const float dy = (ndc.y - region->centerY) / region->radiusY;
        worst = std::max(worst, dx * dx + dy * dy);
    }
    return worst;
}

} // namespace

TEST_CASE("symmetric FOV centres the region") {
    const Fov symmetric{-kQuarterPi, kQuarterPi, kQuarterPi, -kQuarterPi};
    const auto region = fullRateRegion(symmetric, Quat::identity(), 24.0f);
    REQUIRE(region.has_value());
    CHECK(region->centerX == doctest::Approx(0.0f));
    CHECK(region->centerY == doctest::Approx(0.0f));
    // Tangent extents are +-1, so NDC and tangent units coincide.
    CHECK(region->radiusX == doctest::Approx(tanDegrees(24.0f)));
    CHECK(region->radiusY == doctest::Approx(tanDegrees(24.0f)));
}

TEST_CASE("asymmetric FOV shifts the region toward the nasal side") {
    const auto left = fullRateRegion(kLeftEye, Quat::identity(), 24.0f);
    const auto right = fullRateRegion(kRightEye, Quat::identity(), 24.0f);
    REQUIRE(left.has_value());
    REQUIRE(right.has_value());

    // The nose is to the right of the left eye and to the left of the right eye.
    CHECK(left->centerX > 0.05f);
    CHECK(right->centerX < -0.05f);
    CHECK(left->centerX == doctest::Approx(-right->centerX));

    // Narrower top than bottom: forward sits above the image centre (Vulkan NDC y is down).
    CHECK(left->centerY < 0.0f);
}

TEST_CASE("outward canting moves the region further toward the nose") {
    const auto straight = fullRateRegion(kLeftEye, Quat::identity(), 24.0f);
    // The left eye canted outward means rotated to the left, a positive yaw.
    const auto canted = fullRateRegion(kLeftEye, Quat::fromAxisAngle(kUp, 0.1f), 24.0f);
    REQUIRE(straight.has_value());
    REQUIRE(canted.has_value());
    CHECK(canted->centerX > straight->centerX);
}

TEST_CASE("off-axis regions cover the whole angular cone") {
    const float halfAngle = 24.0f;
    const auto region = fullRateRegion(kLeftEye, Quat::identity(), halfAngle);
    REQUIRE(region.has_value());
    // The cone's left and right edges on the tangent plane, converted to NDC as the projection does.
    const float tanLeft = std::tan(kLeftEye.angleLeft);
    const float tanRight = std::tan(kLeftEye.angleRight);
    const auto toNdcX = [&](float tanX) {
        return (2.0f * tanX - (tanRight + tanLeft)) / (tanRight - tanLeft);
    };

    const float coneRight = toNdcX(tanDegrees(halfAngle));
    const float coneLeft = toNdcX(-tanDegrees(halfAngle));
    CHECK(region->centerX + region->radiusX >= coneRight - 1e-5f);
    CHECK(region->centerX - region->radiusX <= coneLeft + 1e-5f);
}

TEST_CASE("smaller half-angles give smaller regions") {
    const auto balanced = fullRateRegion(kLeftEye, Quat::identity(), 24.0f);
    const auto aggressive = fullRateRegion(kLeftEye, Quat::identity(), 18.0f);
    REQUIRE(balanced.has_value());
    REQUIRE(aggressive.has_value());
    CHECK(aggressive->radiusX < balanced->radiusX);
    CHECK(aggressive->radiusY < balanced->radiusY);
}

TEST_CASE("degenerate inputs yield no region") {
    CHECK_FALSE(fullRateRegion(kLeftEye, Quat::identity(), 0.0f).has_value());
    CHECK_FALSE(fullRateRegion(kLeftEye, Quat::identity(), 90.0f).has_value());
    CHECK_FALSE(fullRateRegion(kLeftEye, Quat::identity(), std::nanf("")).has_value());
    // Eye turned so far that head-forward is behind it.
    CHECK_FALSE(fullRateRegion(kLeftEye, Quat::fromAxisAngle(kUp, 2.0f), 24.0f).has_value());
}

TEST_CASE("canted and off-axis regions contain the whole projected cone") {
    // Float rounding in the projection of the samples; far below a visible difference.
    constexpr float kTolerance = 1e-4f;
    const evr::Vec3 kRight{1.0f, 0.0f, 0.0f};
    for (const Fov& fov : {kLeftEye, kRightEye}) {
        for (const float halfAngle : {18.0f, 24.0f, 30.0f}) {
            for (const float yawDegrees : {-15.0f, -5.0f, 0.0f, 5.0f, 10.0f, 15.0f}) {
                for (const float pitchDegrees : {-8.0f, 0.0f, 6.0f}) {
                    const Quat cant = Quat::fromAxisAngle(kUp, radians(yawDegrees)) *
                                      Quat::fromAxisAngle(kRight, radians(pitchDegrees));
                    CAPTURE(halfAngle);
                    CAPTURE(yawDegrees);
                    CAPTURE(pitchDegrees);
                    CHECK(worstEllipseValue(fov, cant, halfAngle) <= 1.0f + kTolerance);
                }
            }
        }
    }
}

TEST_CASE("the region is not much larger than the cone") {
    // The boundary touches the ellipse somewhere, so the region is as tight as its shape allows.
    const float worst = worstEllipseValue(kLeftEye, Quat::fromAxisAngle(kUp, radians(15.0f)), 24.0f);
    CHECK(worst > 0.99f);
}

TEST_CASE("an eye FOV with no projection yields no region") {
    CHECK_FALSE(fullRateRegion({0.5f, 0.5f, 0.8f, -0.8f}, Quat::identity(), 24.0f).has_value());
    CHECK_FALSE(fullRateRegion({-0.9f, 0.9f, 0.8f, 0.8f}, Quat::identity(), 24.0f).has_value());
    CHECK_FALSE(fullRateRegion({-2.0f, 0.9f, 0.8f, -0.8f}, Quat::identity(), 24.0f).has_value());
}
