#include "features/arm/hand_offset.hpp"

#include "features/arm/arm_fixture.hpp"
#include "xr_math/head_aim.hpp"

#include <doctest/doctest.h>

using evr::Vec3;
using evr::arm::gripAxisFromWristAxis;
using evr::arm::gripFromWrist;
using evr::arm::HandOffset;
using evr::arm::plausiblePose;
using evr::arm::wristAxisFromGripAxis;
using evr::arm::wristFromGrip;
using evr::test::approxAxis;
using evr::test::approxEqual;
using evr::test::approxPose;
using evr::xr_math::axisFromAngles;
using evr::xr_math::IdAngles;
using evr::xr_math::IdViewAxis;
using evr::xr_math::ModelPose;

TEST_CASE("a grip pointing forward gives a hand with the thumb up, the fingers forward and the palm right") {
    const ModelPose wrist = wristFromGrip({{0.4f, 0.2f, -0.3f}, {}}, {});
    CHECK(approxEqual(wrist.position, Vec3{0.4f, 0.2f, -0.3f}));
    CHECK(approxEqual(wrist.axis.forward, Vec3{0.0f, 0.0f, 1.0f})); // x: the thumb side, up
    CHECK(approxEqual(wrist.axis.left, Vec3{1.0f, 0.0f, 0.0f}));    // y: toward the fingers, forward
    CHECK(approxEqual(wrist.axis.up, Vec3{0.0f, 1.0f, 0.0f}));      // z: the back of the hand, left
    CHECK(plausiblePose(wrist));
}

TEST_CASE("the grip-to-wrist mapping is a rotation and undoes itself") {
    const IdViewAxis grip = axisFromAngles({25.0f, -70.0f, 40.0f});
    const IdViewAxis wrist = wristAxisFromGripAxis(grip);
    CHECK(plausiblePose({{}, wrist}));
    CHECK(approxAxis(gripAxisFromWristAxis(wrist), grip));
}

TEST_CASE("the offset moves the wrist in the grip's own frame") {
    const ModelPose grip{{1.0f, 2.0f, 3.0f}, axisFromAngles({0.0f, 90.0f, 0.0f})}; // facing +Y (left)
    const ModelPose wrist = wristFromGrip(grip, {{-0.08f, 0.035f, 0.0f}, {}});
    // 8 cm behind the grip is -Y, 3.5 cm to its left is -X.
    CHECK(approxEqual(wrist.position, Vec3{1.0f - 0.035f, 2.0f - 0.08f, 3.0f}));
}

TEST_CASE("a rotation offset turns the hand about the grip's axes") {
    // An idAngles roll of -90 degrees about the grip's forward (its left going down) turns the palm, which
    // faces the grip's right, up.
    const ModelPose wrist = wristFromGrip({{}, {}}, {{}, IdAngles{0.0f, 0.0f, -90.0f}});
    const Vec3 palm = -wrist.axis.up; // the palm faces the hand's -z
    CHECK(approxEqual(palm, Vec3{0.0f, 0.0f, 1.0f}));
    CHECK(approxEqual(wrist.axis.left, Vec3{1.0f, 0.0f, 0.0f})); // the fingers still point forward
}

TEST_CASE("wrist and grip round-trip through any offset") {
    const ModelPose grip{{0.3f, -0.1f, 1.2f}, axisFromAngles({-15.0f, 130.0f, 70.0f})};
    for (const HandOffset& offset : {HandOffset{}, HandOffset{{-0.08f, 0.035f, 0.0f}, {}},
                                     HandOffset{{0.02f, -0.05f, 0.03f}, IdAngles{30.0f, -20.0f, 45.0f}}}) {
        const ModelPose wrist = wristFromGrip(grip, offset);
        CHECK(plausiblePose(wrist));
        CHECK(approxPose(gripFromWrist(wrist, offset), grip));
    }
}
