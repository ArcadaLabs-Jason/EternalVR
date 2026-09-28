#include "common/mat4.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <limits>
#include <numbers>
#include <ostream>

using evr::Mat4;
using evr::Quat;
using evr::Vec4;
using evr::test::approxEqual;

TEST_CASE("storage is column-major") {
    Mat4 m;
    m.set(1, 3, 7.0f);
    // Row 1 of column 3 is element 3 * 4 + 1.
    CHECK(m.m[13] == 7.0f);
    CHECK(m.at(1, 3) == 7.0f);
}

TEST_CASE("translation lives in the last column") {
    const Mat4 m = evr::makeRigidTransform(Quat::identity(), {1.0f, 2.0f, 3.0f});
    CHECK(m.m[12] == 1.0f);
    CHECK(m.m[13] == 2.0f);
    CHECK(m.m[14] == 3.0f);
    CHECK(approxEqual(m * Vec4{0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 2.0f, 3.0f, 1.0f}));
    // Directions (w = 0) are not translated.
    CHECK(approxEqual(m * Vec4{1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}));
}

TEST_CASE("product applies the right-hand matrix first") {
    const Mat4 translate = evr::makeRigidTransform(Quat::identity(), {1.0f, 0.0f, 0.0f});
    const Mat4 rotate =
        evr::makeRigidTransform(Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, std::numbers::pi_v<float>), {});
    const Vec4 origin{0.0f, 0.0f, 0.0f, 1.0f};

    // Translate, then rotate 180 degrees about Y: +X becomes -X.
    CHECK(approxEqual((rotate * translate) * origin, {-1.0f, 0.0f, 0.0f, 1.0f}));
    // Rotate (no effect on the origin), then translate.
    CHECK(approxEqual((translate * rotate) * origin, {1.0f, 0.0f, 0.0f, 1.0f}));
}

TEST_CASE("identity is the multiplicative identity") {
    const Mat4 m =
        evr::makeRigidTransform(Quat::fromAxisAngle({1.0f, 1.0f, 0.0f}, 0.4f), {3.0f, -2.0f, 1.0f});
    CHECK(approxEqual(m * Mat4::identity(), m));
    CHECK(approxEqual(Mat4::identity() * m, m));
}

TEST_CASE("inverse of a general matrix") {
    // Column-major: an invertible 3x3 block plus a translation.
    const Mat4 m{{2, 0, 1, 0, 1, 3, 0, 0, 0, 1, 4, 0, 5, 6, 7, 1}};
    const auto inv = evr::inverse(m);
    REQUIRE(inv.has_value());
    CHECK(approxEqual(m * *inv, Mat4::identity()));
    CHECK(approxEqual(*inv * m, Mat4::identity()));
}

TEST_CASE("inverse needs row pivoting when the diagonal starts at zero") {
    Mat4 swapXY;
    swapXY.set(0, 1, 1.0f);
    swapXY.set(1, 0, 1.0f);
    swapXY.set(2, 2, 1.0f);
    swapXY.set(3, 3, 1.0f);
    const auto inv = evr::inverse(swapXY);
    REQUIRE(inv.has_value());
    CHECK(approxEqual(*inv, swapXY));
}

TEST_CASE("inverse of a singular matrix is empty") {
    Mat4 singular = Mat4::identity();
    singular.set(2, 2, 0.0f);
    CHECK_FALSE(evr::inverse(singular).has_value());
}

TEST_CASE("inverse of a small-scale well-conditioned matrix") {
    // A rotation scaled far below any fixed threshold is as invertible as the rotation itself.
    const float scale = 1e-14f;
    Mat4 small = evr::makeRigidTransform(Quat::fromAxisAngle({1.0f, 2.0f, 0.5f}, 0.7f), {});
    for (float& value : small.m) {
        value *= scale;
    }
    const auto inv = evr::inverse(small);
    REQUIRE(inv.has_value());
    CHECK(approxEqual(small * *inv, Mat4::identity()));
}

TEST_CASE("inverse of a large-scale singular matrix is empty") {
    Mat4 singular = Mat4::identity();
    for (float& value : singular.m) {
        value *= 1e6f;
    }
    // Two equal rows.
    singular.set(1, 0, singular.at(0, 0));
    singular.set(1, 1, 0.0f);
    CHECK_FALSE(evr::inverse(singular).has_value());
}

TEST_CASE("inverse of a matrix with non-finite entries is empty") {
    Mat4 m = Mat4::identity();
    m.set(0, 3, std::numeric_limits<float>::quiet_NaN());
    CHECK_FALSE(evr::inverse(m).has_value());
    m.set(0, 3, std::numeric_limits<float>::infinity());
    CHECK_FALSE(evr::inverse(m).has_value());
    CHECK_FALSE(evr::inverse(Mat4{}).has_value());
}
