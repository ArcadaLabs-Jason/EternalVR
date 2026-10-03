#include "features/foveation/foveation_region.hpp"

#include "xr_math/projection.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>
#include <ostream>

using evr::Quat;
using evr::foveation::foveationRegion;
using evr::foveation::FoveationRegion;
using evr::xr_math::Fov;

namespace {

constexpr float kQuarterPi = std::numbers::pi_v<float> / 4.0f;
constexpr evr::Vec3 kUp{0.0f, 1.0f, 0.0f};

float radians(float degrees) {
    return degrees * std::numbers::pi_v<float> / 180.0f;
}

float tanDegrees(float degrees) {
    return std::tan(radians(degrees));
}

// Quest 3 through VDXR, as the owner's capture logged it: left -54, right 40, up 44, down -55 degrees for
// eye L, mirrored for eye R. Head-forward sits near the nasal edge and the top.
const Fov kQuest3Left{radians(-54.0f), radians(40.0f), radians(44.0f), radians(-55.0f)};
const Fov kQuest3Right{radians(-40.0f), radians(54.0f), radians(44.0f), radians(-55.0f)};

// The region's area on the eye's tangent plane: NDC spans the tangent width and height in 2 units.
double tangentArea(const FoveationRegion& r, const Fov& fov) {
    const double width = std::tan(fov.angleRight) - std::tan(fov.angleLeft);
    const double height = std::tan(fov.angleUp) - std::tan(fov.angleDown);
    const double ndcArea = std::numbers::pi / 4.0 * (static_cast<double>(r.radiusLeft) + r.radiusRight) *
                           (static_cast<double>(r.radiusTop) + r.radiusBottom);
    return ndcArea * width / 2.0 * height / 2.0;
}

// How far the region reaches toward each edge, as a fraction of the distance from head-forward to it.
struct Reach {
    float left;
    float right;
    float top;
    float bottom;
};

Reach reachOf(const FoveationRegion& r) {
    return {r.radiusLeft / (1.0f + r.centerX), r.radiusRight / (1.0f - r.centerX),
            r.radiusTop / (1.0f + r.centerY), r.radiusBottom / (1.0f - r.centerY)};
}

} // namespace

TEST_CASE("a symmetric square FOV gives the cone's circle") {
    const Fov symmetric{-kQuarterPi, kQuarterPi, kQuarterPi, -kQuarterPi};
    const auto region = foveationRegion(symmetric, Quat::identity(), 24.0f);
    REQUIRE(region.has_value());
    CHECK(region->centerX == doctest::Approx(0.0f));
    CHECK(region->centerY == doctest::Approx(0.0f));
    // Tangent extents are +-1, so NDC and tangent units coincide.
    CHECK(region->radiusLeft == doctest::Approx(tanDegrees(24.0f)));
    CHECK(region->radiusRight == doctest::Approx(tanDegrees(24.0f)));
    CHECK(region->radiusTop == doctest::Approx(tanDegrees(24.0f)));
    CHECK(region->radiusBottom == doctest::Approx(tanDegrees(24.0f)));
}

TEST_CASE("a symmetric wide FOV gives an ellipse of the cone's area, its radii in the FOV's proportion") {
    const Fov wide{radians(-50.0f), radians(50.0f), radians(40.0f), radians(-40.0f)};
    const auto region = foveationRegion(wide, Quat::identity(), 24.0f);
    REQUIRE(region.has_value());
    CHECK(region->centerX == doctest::Approx(0.0f));
    CHECK(region->centerY == doctest::Approx(0.0f));
    // The same NDC radius on every side: wider in tangent units across, by the FOV's aspect.
    CHECK(region->radiusLeft == doctest::Approx(region->radiusTop));
    CHECK(region->radiusRight == doctest::Approx(region->radiusBottom));
    const double cone = std::numbers::pi * tanDegrees(24.0f) * tanDegrees(24.0f);
    CHECK(tangentArea(*region, wide) == doctest::Approx(cone));
}

TEST_CASE("Quest 3: the region is anchored at head-forward, toward the nose and the top") {
    const auto left = foveationRegion(kQuest3Left, Quat::identity(), 24.0f);
    REQUIRE(left.has_value());
    // The nose is to the right of the left eye; a narrower top puts head-forward above the image centre
    // (Vulkan NDC y is down).
    CHECK(left->centerX > 0.1f);
    CHECK(left->centerY < -0.05f);
    // Head-forward is where the projection puts it: tan 0 across tangents -1.376 to 0.839.
    const float tl = std::tan(kQuest3Left.angleLeft);
    const float tr = std::tan(kQuest3Left.angleRight);
    CHECK(left->centerX == doctest::Approx(-(tr + tl) / (tr - tl)).epsilon(1e-4));
}

TEST_CASE("Quest 3: the region reaches the same fraction of the way to every edge") {
    for (const float angle : {12.0f, 24.0f, 40.0f, 46.0f}) {
        CAPTURE(angle);
        const auto region = foveationRegion(kQuest3Left, Quat::identity(), angle);
        REQUIRE(region.has_value());
        const Reach reach = reachOf(*region);
        CHECK(reach.left == doctest::Approx(reach.right));
        CHECK(reach.left == doctest::Approx(reach.top));
        CHECK(reach.left == doctest::Approx(reach.bottom));
        // The temporal and bottom sides are the longer ones, so their radii are larger.
        CHECK(region->radiusLeft > region->radiusRight);
        CHECK(region->radiusBottom > region->radiusTop);
    }
}

TEST_CASE(
    "Quest 3: the band beyond the half-rate region is as deep on the nasal side as on the temporal one") {
    // Balanced: half rate to 40 degrees. A circle of 40 degrees around head-forward would leave no band at
    // the nasal edge (40 degrees out) and one from 40 to 54 degrees at the temporal edge.
    const auto half = foveationRegion(kQuest3Left, Quat::identity(), 40.0f);
    REQUIRE(half.has_value());
    const float nasalBand = 1.0f - (half->centerX + half->radiusRight);
    const float temporalBand = (half->centerX - half->radiusLeft) + 1.0f;
    const float nasalExtent = 1.0f - half->centerX;
    const float temporalExtent = 1.0f + half->centerX;
    CHECK(nasalBand > 0.0f);
    CHECK(nasalBand / nasalExtent == doctest::Approx(temporalBand / temporalExtent));
    const float topBand = (half->centerY - half->radiusTop) + 1.0f;
    const float bottomBand = 1.0f - (half->centerY + half->radiusBottom);
    CHECK(topBand / (1.0f + half->centerY) == doctest::Approx(bottomBand / (1.0f - half->centerY)));
}

TEST_CASE("Quest 3: each preset angle keeps the cone's area") {
    for (const float angle : {12.0f, 18.0f, 24.0f, 30.0f, 34.0f, 40.0f, 46.0f}) {
        CAPTURE(angle);
        const auto region = foveationRegion(kQuest3Left, Quat::identity(), angle);
        REQUIRE(region.has_value());
        const double cone = std::numbers::pi * tanDegrees(angle) * tanDegrees(angle);
        CHECK(tangentArea(*region, kQuest3Left) == doctest::Approx(cone).epsilon(1e-4));
    }
}

TEST_CASE("the two eyes' regions are mirror images") {
    const auto left = foveationRegion(kQuest3Left, Quat::identity(), 24.0f);
    const auto right = foveationRegion(kQuest3Right, Quat::identity(), 24.0f);
    REQUIRE(left.has_value());
    REQUIRE(right.has_value());
    CHECK(right->centerX == doctest::Approx(-left->centerX));
    CHECK(right->centerY == doctest::Approx(left->centerY));
    CHECK(right->radiusLeft == doctest::Approx(left->radiusRight));
    CHECK(right->radiusRight == doctest::Approx(left->radiusLeft));
    CHECK(right->radiusTop == doctest::Approx(left->radiusTop));
    CHECK(right->radiusBottom == doctest::Approx(left->radiusBottom));
}

TEST_CASE("outward canting moves the region to where head-forward projects, with the cone's area") {
    // The left eye canted outward means rotated to the left, a positive yaw: head-forward is then 0.1 rad to
    // the right of the eye's axis, on the nasal side.
    constexpr float kCant = 0.1f;
    const auto straight = foveationRegion(kQuest3Left, Quat::identity(), 24.0f);
    const auto canted = foveationRegion(kQuest3Left, Quat::fromAxisAngle(kUp, kCant), 24.0f);
    REQUIRE(straight.has_value());
    REQUIRE(canted.has_value());
    // Head-forward's tangents in the eye: (tan 0.1, 0) canted, (0, 0) straight. Across the tangent extent
    // [left, right] x maps to [-1, 1]; y down, [up, down] to [-1, 1].
    const double left = std::tan(kQuest3Left.angleLeft);
    const double right = std::tan(kQuest3Left.angleRight);
    const double up = std::tan(kQuest3Left.angleUp);
    const double down = std::tan(kQuest3Left.angleDown);
    const auto ndcX = [&](double tx) {
        return (2.0 * tx - (right + left)) / (right - left);
    };
    const double ndcY = (up + down) / (up - down); // tangent 0, above the middle: negative
    CHECK(straight->centerX == doctest::Approx(ndcX(0.0)).epsilon(1e-4));
    CHECK(canted->centerX == doctest::Approx(ndcX(std::tan(static_cast<double>(kCant)))).epsilon(1e-4));
    CHECK(canted->centerX > straight->centerX);
    CHECK(canted->centerY == doctest::Approx(ndcY).epsilon(1e-4));
    CHECK(straight->centerY == doctest::Approx(ndcY).epsilon(1e-4));
    // The area is the cone's, wherever the centre is, and each side reaches the fraction it gives.
    const double cone = std::numbers::pi * tanDegrees(24.0f) * tanDegrees(24.0f);
    CHECK(tangentArea(*canted, kQuest3Left) == doctest::Approx(cone).epsilon(1e-4));
    const double fraction = 2.0 * tanDegrees(24.0f) / std::sqrt((right - left) * (up - down));
    const Reach reach = reachOf(*canted);
    CHECK(reach.left == doctest::Approx(fraction).epsilon(1e-4));
    CHECK(reach.right == doctest::Approx(fraction).epsilon(1e-4));
    CHECK(reach.top == doctest::Approx(fraction).epsilon(1e-4));
    CHECK(reach.bottom == doctest::Approx(fraction).epsilon(1e-4));
    // Toward the nose the region shrinks with the way left to go, toward the temple it grows.
    CHECK(canted->radiusRight < straight->radiusRight);
    CHECK(canted->radiusLeft > straight->radiusLeft);
}

TEST_CASE("smaller half-angles give smaller regions") {
    const auto balanced = foveationRegion(kQuest3Left, Quat::identity(), 24.0f);
    const auto aggressive = foveationRegion(kQuest3Left, Quat::identity(), 18.0f);
    REQUIRE(balanced.has_value());
    REQUIRE(aggressive.has_value());
    CHECK(aggressive->radiusLeft < balanced->radiusLeft);
    CHECK(aggressive->radiusRight < balanced->radiusRight);
    CHECK(aggressive->radiusTop < balanced->radiusTop);
    CHECK(aggressive->radiusBottom < balanced->radiusBottom);
}

TEST_CASE("degenerate inputs yield no region") {
    CHECK_FALSE(foveationRegion(kQuest3Left, Quat::identity(), 0.0f).has_value());
    CHECK_FALSE(foveationRegion(kQuest3Left, Quat::identity(), 90.0f).has_value());
    CHECK_FALSE(foveationRegion(kQuest3Left, Quat::identity(), std::nanf("")).has_value());
    // Eye turned so far that head-forward is behind it.
    CHECK_FALSE(foveationRegion(kQuest3Left, Quat::fromAxisAngle(kUp, 2.0f), 24.0f).has_value());
    // Eye turned so far that head-forward is beside its image (the left edge is 54 degrees out).
    CHECK_FALSE(foveationRegion(kQuest3Left, Quat::fromAxisAngle(kUp, radians(-60.0f)), 24.0f).has_value());
}

TEST_CASE("an eye FOV with no projection yields no region") {
    CHECK_FALSE(foveationRegion({0.5f, 0.5f, 0.8f, -0.8f}, Quat::identity(), 24.0f).has_value());
    CHECK_FALSE(foveationRegion({-0.9f, 0.9f, 0.8f, 0.8f}, Quat::identity(), 24.0f).has_value());
    CHECK_FALSE(foveationRegion({-2.0f, 0.9f, 0.8f, -0.8f}, Quat::identity(), 24.0f).has_value());
}
