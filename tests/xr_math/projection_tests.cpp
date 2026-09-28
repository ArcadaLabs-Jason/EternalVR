#include "xr_math/projection.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <ostream>

using evr::Vec3;
using evr::test::approxEqual;
using evr::xr_math::Fov;

namespace {

constexpr float kQuarterPi = std::numbers::pi_v<float> / 4.0f;

// 90 degrees each way, so tangents are exactly +-1 and view-space points map to simple NDC values.
constexpr Fov kSymmetric90{-kQuarterPi, kQuarterPi, kQuarterPi, -kQuarterPi};

Vec3 projectToNdc(const evr::Mat4& projection, Vec3 viewPoint) {
    return evr::perspectiveDivide(projection * evr::toPoint(viewPoint));
}

evr::Mat4 standardProjection(const Fov& fov, float nearZ, float farZ) {
    const auto projection = evr::xr_math::makeProjectionStandard(fov, nearZ, farZ);
    REQUIRE(projection.has_value());
    return *projection;
}

evr::Mat4 reversedInfiniteProjection(const Fov& fov, float nearZ) {
    const auto projection = evr::xr_math::makeProjectionReversedInfinite(fov, nearZ);
    REQUIRE(projection.has_value());
    return *projection;
}

} // namespace

TEST_CASE("forward axis projects to the image centre") {
    const auto projection = standardProjection(kSymmetric90, 0.1f, 100.0f);
    const Vec3 ndc = projectToNdc(projection, {0.0f, 0.0f, -5.0f});
    CHECK(approxEqual(ndc.x, 0.0f));
    CHECK(approxEqual(ndc.y, 0.0f));
}

TEST_CASE("frustum edges map to the NDC borders with Vulkan's y-down convention") {
    const auto projection = standardProjection(kSymmetric90, 0.1f, 100.0f);
    const float depth = 3.0f;

    CHECK(approxEqual(projectToNdc(projection, {depth, 0.0f, -depth}).x, 1.0f));
    CHECK(approxEqual(projectToNdc(projection, {-depth, 0.0f, -depth}).x, -1.0f));
    // View-space up is the top of the image, which is NDC y = -1 in Vulkan.
    CHECK(approxEqual(projectToNdc(projection, {0.0f, depth, -depth}).y, -1.0f));
    CHECK(approxEqual(projectToNdc(projection, {0.0f, -depth, -depth}).y, 1.0f));
}

TEST_CASE("asymmetric frustum edges map to the NDC borders") {
    const Fov fov{-0.9f, 0.7f, 0.8f, -0.85f};
    const auto projection = standardProjection(fov, 0.1f, 100.0f);
    const float depth = 2.0f;

    const Vec3 leftEdge{std::tan(fov.angleLeft) * depth, 0.0f, -depth};
    const Vec3 rightEdge{std::tan(fov.angleRight) * depth, 0.0f, -depth};
    const Vec3 topEdge{0.0f, std::tan(fov.angleUp) * depth, -depth};
    const Vec3 bottomEdge{0.0f, std::tan(fov.angleDown) * depth, -depth};

    CHECK(approxEqual(projectToNdc(projection, leftEdge).x, -1.0f));
    CHECK(approxEqual(projectToNdc(projection, rightEdge).x, 1.0f));
    CHECK(approxEqual(projectToNdc(projection, topEdge).y, -1.0f));
    CHECK(approxEqual(projectToNdc(projection, bottomEdge).y, 1.0f));

    // Forward is off-centre: toward the narrower side (right) and the narrower side (top).
    const Vec3 forward = projectToNdc(projection, {0.0f, 0.0f, -depth});
    CHECK(forward.x > 0.0f);
    CHECK(forward.y < 0.0f);
}

TEST_CASE("standard depth maps near to 0 and far to 1") {
    const float nearZ = 0.05f;
    const float farZ = 500.0f;
    const auto projection = standardProjection(kSymmetric90, nearZ, farZ);

    CHECK(approxEqual(projectToNdc(projection, {0.0f, 0.0f, -nearZ}).z, 0.0f));
    CHECK(approxEqual(projectToNdc(projection, {0.0f, 0.0f, -farZ}).z, 1.0f));
    const float midDepth = projectToNdc(projection, {0.0f, 0.0f, -10.0f}).z;
    CHECK(midDepth > 0.0f);
    CHECK(midDepth < 1.0f);
}

TEST_CASE("reversed infinite depth maps near to 1 and infinity to 0") {
    const float nearZ = 0.05f;
    const auto projection = reversedInfiniteProjection(kSymmetric90, nearZ);

    CHECK(approxEqual(projectToNdc(projection, {0.0f, 0.0f, -nearZ}).z, 1.0f));

    // A point at infinity is a direction (w = 0 in view space); its clip depth is exactly 0.
    const evr::Vec4 atInfinity = projection * evr::toDirection({0.0f, 0.0f, -1.0f});
    CHECK(atInfinity.z == 0.0f);
    CHECK(atInfinity.w > 0.0f);

    // Depth decreases monotonically with distance and approaches 0.
    const float at10 = projectToNdc(projection, {0.0f, 0.0f, -10.0f}).z;
    const float at1000 = projectToNdc(projection, {0.0f, 0.0f, -1000.0f}).z;
    CHECK(at10 > at1000);
    CHECK(at1000 > 0.0f);
    CHECK(approxEqual(at1000, nearZ / 1000.0f, 1e-7f));
}

TEST_CASE("depth mode does not affect x and y") {
    const Fov fov{-0.9f, 0.7f, 0.8f, -0.85f};
    const auto standard = standardProjection(fov, 0.1f, 100.0f);
    const auto reversed = reversedInfiniteProjection(fov, 0.1f);
    const Vec3 point{0.4f, -0.3f, -2.0f};

    const Vec3 a = projectToNdc(standard, point);
    const Vec3 b = projectToNdc(reversed, point);
    CHECK(approxEqual(a.x, b.x));
    CHECK(approxEqual(a.y, b.y));
}

TEST_CASE("degenerate inputs have no projection") {
    using evr::xr_math::makeProjectionReversedInfinite;
    using evr::xr_math::makeProjectionStandard;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float rightAngle = std::numbers::pi_v<float> / 2.0f;

    // No width or no height.
    CHECK_FALSE(makeProjectionStandard({0.5f, 0.5f, 0.5f, -0.5f}, 0.1f, 100.0f).has_value());
    CHECK_FALSE(makeProjectionStandard({0.5f, -0.5f, 0.5f, -0.5f}, 0.1f, 100.0f).has_value());
    CHECK_FALSE(makeProjectionStandard({-0.5f, 0.5f, -0.2f, -0.2f}, 0.1f, 100.0f).has_value());
    // Half-angles of 90 degrees or more have no tangent-plane extent.
    CHECK_FALSE(makeProjectionStandard({-rightAngle, 0.5f, 0.5f, -0.5f}, 0.1f, 100.0f).has_value());
    CHECK_FALSE(makeProjectionStandard({-0.5f, 2.0f, 0.5f, -0.5f}, 0.1f, 100.0f).has_value());
    CHECK_FALSE(makeProjectionReversedInfinite({-0.5f, 0.5f, 1.7f, -0.5f}, 0.1f).has_value());
    // Near and far planes.
    CHECK_FALSE(makeProjectionStandard(kSymmetric90, 0.0f, 100.0f).has_value());
    CHECK_FALSE(makeProjectionStandard(kSymmetric90, -0.1f, 100.0f).has_value());
    CHECK_FALSE(makeProjectionStandard(kSymmetric90, 1.0f, 1.0f).has_value());
    CHECK_FALSE(makeProjectionStandard(kSymmetric90, 1.0f, 0.5f).has_value());
    CHECK_FALSE(makeProjectionReversedInfinite(kSymmetric90, 0.0f).has_value());
    // NaN and infinity anywhere.
    CHECK_FALSE(makeProjectionStandard({nan, 0.5f, 0.5f, -0.5f}, 0.1f, 100.0f).has_value());
    CHECK_FALSE(makeProjectionStandard(kSymmetric90, nan, 100.0f).has_value());
    CHECK_FALSE(makeProjectionStandard(kSymmetric90, 0.1f, nan).has_value());
    CHECK_FALSE(
        makeProjectionStandard(kSymmetric90, 0.1f, std::numeric_limits<float>::infinity()).has_value());
    CHECK_FALSE(
        makeProjectionReversedInfinite(kSymmetric90, std::numeric_limits<float>::infinity()).has_value());
}

TEST_CASE("every projection that is built is finite") {
    const auto projection = evr::xr_math::makeProjectionStandard({-1.5f, 1.5f, 1.5f, -1.5f}, 0.01f, 1e6f);
    REQUIRE(projection.has_value());
    for (const float value : projection->m) {
        CHECK(std::isfinite(value));
    }
}
