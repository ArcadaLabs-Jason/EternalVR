#include "xr_math/upright_space.hpp"

#include <doctest/doctest.h>

#include <cmath>

using evr::xr_math::headingOnly;
using evr::xr_math::Quaternion;
using evr::xr_math::upCosine;
using evr::xr_math::uprightReplacement;

namespace {

constexpr float kPi = 3.14159265f;

Quaternion aboutY(float degrees) {
    const float h = degrees * kPi / 360.0f;
    return {0.0f, std::sin(h), 0.0f, std::cos(h)};
}

Quaternion aboutZ(float degrees) {
    const float h = degrees * kPi / 360.0f;
    return {0.0f, 0.0f, std::sin(h), std::cos(h)};
}

Quaternion multiply(const Quaternion& a, const Quaternion& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// Same rotation (q and -q are equal).
bool sameRotation(const Quaternion& a, const Quaternion& b) {
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    return std::fabs(std::fabs(dot) - 1.0f) < 1e-4f;
}

} // namespace

TEST_CASE("upright space: an upright LOCAL is kept") {
    CHECK(upCosine(Quaternion{}) == doctest::Approx(1.0f));
    CHECK_FALSE(uprightReplacement(Quaternion{}).has_value());
    // Any heading, and a small lean from a runtime's calibration, stay upright.
    CHECK_FALSE(uprightReplacement(aboutY(137.0f)).has_value());
    CHECK_FALSE(uprightReplacement(aboutZ(10.0f)).has_value());
}

TEST_CASE("upright space: LOCAL rolled over is replaced by its heading") {
    // The SteamVR case: LOCAL rolled 180 degrees about its forward axis.
    const Quaternion flipped = aboutZ(180.0f);
    CHECK(upCosine(flipped) == doctest::Approx(-1.0f));
    const auto fixed = uprightReplacement(flipped);
    REQUIRE(fixed.has_value());
    CHECK(sameRotation(*fixed, Quaternion{}));
    // Rolled over and facing 90 degrees to the left: the heading is kept.
    const auto turned = uprightReplacement(multiply(aboutY(90.0f), aboutZ(180.0f)));
    REQUIRE(turned.has_value());
    CHECK(sameRotation(*turned, aboutY(90.0f)));
    // The logged quaternion from the headset session reads as upside down.
    CHECK(upCosine(Quaternion{-0.009f, -0.038f, -0.999f, 0.008f}) < -0.9f);
}

TEST_CASE("upright space: heading from a vertical forward axis and degenerate input") {
    // Pitched straight down (-90 about X): forward is -Y; the heading comes from the up axis.
    const float h = -45.0f * kPi / 180.0f;
    const Quaternion down{std::sin(h), 0.0f, 0.0f, std::cos(h)};
    CHECK(sameRotation(headingOnly(down), Quaternion{}));
    CHECK(sameRotation(headingOnly(Quaternion{0.0f, 0.0f, 0.0f, 0.0f}), Quaternion{}));
}
