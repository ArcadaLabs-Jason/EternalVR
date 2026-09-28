#include "common/pose.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <numbers>
#include <ostream>

using evr::Pose;
using evr::Quat;
using evr::Vec3;
using evr::test::approxEqual;

namespace {

constexpr float kHalfPi = std::numbers::pi_v<float> / 2.0f;

Pose samplePose() {
    return {Quat::fromAxisAngle({0.2f, 1.0f, -0.3f}, 0.8f), {0.5f, 1.6f, -2.0f}};
}

} // namespace

TEST_CASE("transformPoint rotates then translates") {
    const Pose pose{Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, kHalfPi), {10.0f, 0.0f, 0.0f}};
    // Local forward (-Z) turns to -X, then moves by +10 X.
    CHECK(approxEqual(transformPoint(pose, {0.0f, 0.0f, -1.0f}), {9.0f, 0.0f, 0.0f}));
    CHECK(approxEqual(transformDirection(pose, {0.0f, 0.0f, -1.0f}), {-1.0f, 0.0f, 0.0f}));
}

TEST_CASE("pose composed with its inverse is the identity") {
    const Pose pose = samplePose();
    const Vec3 point{1.0f, -2.0f, 3.0f};
    CHECK(approxEqual(transformPoint(compose(pose, inverse(pose)), point), point));
    CHECK(approxEqual(transformPoint(compose(inverse(pose), pose), point), point));
}

TEST_CASE("compose applies the child first") {
    const Pose head{Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, kHalfPi), {0.0f, 1.7f, 0.0f}};
    const Pose eyeInHead{Quat::identity(), {0.032f, 0.0f, 0.0f}};
    const Vec3 point{0.0f, 0.0f, -1.0f};

    const Vec3 expected = transformPoint(head, transformPoint(eyeInHead, point));
    CHECK(approxEqual(transformPoint(compose(head, eyeInHead), point), expected));
}

TEST_CASE("matrix form matches the pose") {
    const Pose pose = samplePose();
    const Vec3 point{0.3f, 0.4f, -5.0f};
    const evr::Vec4 viaMatrix = toMatrix(pose) * evr::toPoint(point);
    CHECK(approxEqual(evr::perspectiveDivide(viaMatrix), transformPoint(pose, point)));
}

TEST_CASE("view matrix maps the camera position to the origin") {
    const Pose camera = samplePose();
    const evr::Vec4 atCamera = toViewMatrix(camera) * evr::toPoint(camera.position);
    CHECK(approxEqual(atCamera, {0.0f, 0.0f, 0.0f, 1.0f}));
}
