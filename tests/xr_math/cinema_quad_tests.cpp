#include "xr_math/cinema_quad.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

using evr::Pose;
using evr::Quat;
using evr::Vec3;
using evr::test::approxEqual;
using evr::xr_math::cinemaQuadPose;
using evr::xr_math::cinemaQuadSize;

TEST_CASE("cinema quad keeps the image aspect at the requested width") {
    const auto size = cinemaQuadSize(1920, 1080, 2.4f);
    REQUIRE(size.has_value());
    CHECK(approxEqual(size->width, 2.4f));
    CHECK(approxEqual(size->height, 1.35f));
}

TEST_CASE("cinema quad size rejects empty images and widths") {
    CHECK_FALSE(cinemaQuadSize(0, 1080, 2.4f).has_value());
    CHECK_FALSE(cinemaQuadSize(1920, 0, 2.4f).has_value());
    CHECK_FALSE(cinemaQuadSize(1920, 1080, 0.0f).has_value());
}

TEST_CASE("cinema quad sits straight ahead of an unrotated head at head height") {
    Pose head;
    head.position = Vec3{0.1f, 1.6f, 0.2f};
    const Pose quad = cinemaQuadPose(head, 2.5f);
    CHECK(approxEqual(quad.position, Vec3{0.1f, 1.6f, -2.3f}));
    // The quad faces +Z locally; toward the head means its normal points back along +Z.
    CHECK(approxEqual(evr::rotate(quad.orientation, Vec3{0.0f, 0.0f, 1.0f}), Vec3{0.0f, 0.0f, 1.0f}));
}

TEST_CASE("cinema quad follows yaw and ignores pitch") {
    const float quarter = std::numbers::pi_v<float> / 2.0f;
    // Turned 90 degrees to the left (counter-clockwise about +Y), then pitched down 30 degrees.
    const Quat yaw = Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, quarter);
    const Quat pitch = Quat::fromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, -std::numbers::pi_v<float> / 6.0f);
    Pose head;
    head.orientation = yaw * pitch;
    head.position = Vec3{0.0f, 1.5f, 0.0f};
    const Pose quad = cinemaQuadPose(head, 2.0f);
    CHECK(approxEqual(quad.position, Vec3{-2.0f, 1.5f, 0.0f}));
    const Vec3 normal = evr::rotate(quad.orientation, Vec3{0.0f, 0.0f, 1.0f});
    CHECK(approxEqual(normal, Vec3{1.0f, 0.0f, 0.0f}));
}

TEST_CASE("cinema quad falls back to straight ahead when looking straight up") {
    Pose head;
    head.orientation = Quat::fromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, std::numbers::pi_v<float> / 2.0f);
    const Pose quad = cinemaQuadPose(head, 2.5f);
    CHECK(approxEqual(quad.position, Vec3{0.0f, 0.0f, -2.5f}));
}

using evr::xr_math::cinemaFov;

namespace {

float tanHalf(float degrees) {
    return std::tan(degrees / 57.29578f / 2.0f);
}

} // namespace

TEST_CASE("cinema fov: a tall eye image gets the flat 16:9 view in its centred band") {
    // The rig's cutscene case: the game keeps its vertical FOV (63.09, 95 across at 16:9) and stops narrowing
    // the horizontal at 1:1, so a 2056x2216 image gets 63.09 x 63.09.
    const auto fov = cinemaFov(63.09f, 63.09f, 2056, 2216, 16.0 / 9.0);
    REQUIRE(fov.has_value());
    CHECK(approxEqual(fov->fovX, 95.0f, 0.02f)); // what a flat 16:9 display shows across
    // Square pixels over the whole image: tan(y/2) / tan(x/2) is the image's height / width.
    CHECK(approxEqual(tanHalf(fov->fovY) / tanHalf(fov->fovX), 2216.0f / 2056.0f, 1e-4f));
    // The centred 16:9 band (2056 x 1157 rows) spans the game's own vertical FOV.
    CHECK(approxEqual(tanHalf(fov->fovY) * 1157.0f / 2216.0f, tanHalf(63.09f), 1e-3f));
}

TEST_CASE("cinema fov: 16:10 is narrower across than 16:9 with the same vertical view") {
    const auto wide = cinemaFov(63.09f, 63.09f, 2056, 2216, 16.0 / 9.0);
    const auto tall = cinemaFov(63.09f, 63.09f, 2056, 2216, 16.0 / 10.0);
    REQUIRE(wide.has_value());
    REQUIRE(tall.has_value());
    CHECK(tall->fovX < wide->fovX);
    CHECK(approxEqual(tanHalf(tall->fovX), tanHalf(63.09f) * 1.6f, 1e-4f));
}

TEST_CASE("cinema fov: a game FOV that already kept the width keeps it") {
    // 90 across, extended vertically for the tall image: the game kept the horizontal view.
    const float fovY = 2.0f * std::atan(std::tan(45.0f / 57.29578f) * 2216.0f / 2056.0f) * 57.29578f;
    const auto fov = cinemaFov(90.0f, fovY, 2056, 2216, 16.0 / 9.0);
    REQUIRE(fov.has_value());
    CHECK(approxEqual(fov->fovX, 90.0f, 0.01f));
    CHECK(approxEqual(fov->fovY, fovY, 0.01f));
}

TEST_CASE("cinema fov: nothing to change for full, a wide image or bad input") {
    CHECK_FALSE(cinemaFov(63.09f, 63.09f, 2056, 2216, 0.0).has_value());
    CHECK_FALSE(cinemaFov(95.0f, 63.09f, 3840, 2160, 16.0 / 9.0).has_value());
    CHECK_FALSE(cinemaFov(95.0f, 63.09f, 3840, 2400, 16.0 / 10.0).has_value());
    CHECK_FALSE(cinemaFov(0.0f, 63.09f, 2056, 2216, 16.0 / 9.0).has_value());
    CHECK_FALSE(cinemaFov(63.09f, 63.09f, 0, 2216, 16.0 / 9.0).has_value());
    CHECK_FALSE(cinemaFov(63.09f, 175.0f, 2056, 2216, 16.0 / 9.0).has_value());
    // A very wide cutscene FOV whose extension would reach 170 degrees stays the game's.
    CHECK_FALSE(cinemaFov(160.0f, 160.0f, 1000, 3000, 16.0 / 9.0).has_value());
}
