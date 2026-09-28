#include "common/quat.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <numbers>
#include <ostream>

using evr::Quat;
using evr::Vec3;
using evr::test::approxEqual;

namespace {
constexpr float kHalfPi = std::numbers::pi_v<float> / 2.0f;
constexpr Vec3 kUp{0.0f, 1.0f, 0.0f};
constexpr Vec3 kForward{0.0f, 0.0f, -1.0f};
} // namespace

TEST_CASE("identity rotation leaves vectors unchanged") {
    CHECK(approxEqual(rotate(Quat::identity(), {1.0f, 2.0f, 3.0f}), {1.0f, 2.0f, 3.0f}));
}

TEST_CASE("positive yaw about +Y turns forward to the left") {
    // Right-hand rule: a positive rotation about +Y takes -Z toward -X.
    const Quat yawLeft = Quat::fromAxisAngle(kUp, kHalfPi);
    CHECK(approxEqual(rotate(yawLeft, kForward), {-1.0f, 0.0f, 0.0f}));
}

TEST_CASE("product applies the right-hand rotation first") {
    const Quat yaw = Quat::fromAxisAngle(kUp, kHalfPi);
    const Quat pitch = Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, kHalfPi);

    // Pitch up first takes forward to +Y, which yaw leaves alone.
    CHECK(approxEqual(rotate(yaw * pitch, kForward), {0.0f, 1.0f, 0.0f}));
    // Yaw first takes forward to -X, which pitch about X leaves alone.
    CHECK(approxEqual(rotate(pitch * yaw, kForward), {-1.0f, 0.0f, 0.0f}));
}

TEST_CASE("conjugate undoes a rotation") {
    const Quat q = Quat::fromAxisAngle({1.0f, 2.0f, 3.0f}, 0.7f);
    const Vec3 v{0.3f, -1.2f, 2.5f};
    CHECK(approxEqual(rotate(conjugate(q), rotate(q, v)), v));
}

TEST_CASE("normalize scales to unit length and handles zero") {
    const Quat q = evr::normalize(Quat{0.0f, 0.0f, 0.0f, 2.0f});
    CHECK(approxEqual(q.w, 1.0f));

    const Quat zero = evr::normalize(Quat{0.0f, 0.0f, 0.0f, 0.0f});
    CHECK(zero == Quat::identity());
}
