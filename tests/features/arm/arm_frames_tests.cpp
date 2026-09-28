#include "features/arm/arm_frames.hpp"

#include "features/arm/arm_fixture.hpp"
#include "xr_math/head_aim.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

using evr::Vec3;
using evr::arm::blendPose;
using evr::arm::compose;
using evr::arm::frameFrom;
using evr::arm::fromQuat;
using evr::arm::parentFor;
using evr::arm::plausiblePose;
using evr::arm::relative;
using evr::arm::rotateAbout;
using evr::arm::toQuat;
using evr::arm::twistAbout;
using evr::test::approxAxis;
using evr::test::approxEqual;
using evr::test::approxPose;
using evr::xr_math::axisFromAngles;
using evr::xr_math::IdViewAxis;
using evr::xr_math::ModelPose;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

const ModelPose kParent{{0.3f, -0.2f, 1.1f}, axisFromAngles({20.0f, -35.0f, 10.0f})};
const ModelPose kChild{{0.1f, 0.4f, 1.3f}, axisFromAngles({-50.0f, 80.0f, -25.0f})};

} // namespace

TEST_CASE("a pose relative to a parent composes back") {
    const ModelPose local = relative(kParent, kChild);
    CHECK(approxPose(compose(kParent, local), kChild));
    CHECK(plausiblePose(local));
}

TEST_CASE("the parent that carries a child's offset to the child") {
    const ModelPose local = relative(kParent, kChild);
    CHECK(approxPose(parentFor(kChild, local), kParent));
    // A new child pose gives the parent that holds it with the same offset.
    const ModelPose moved{{-0.2f, 0.5f, 0.9f}, axisFromAngles({5.0f, 170.0f, 60.0f})};
    CHECK(approxPose(compose(parentFor(moved, local), local), moved));
}

TEST_CASE("a frame from a direction and a normal") {
    IdViewAxis frame;
    REQUIRE(frameFrom({2.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, frame));
    CHECK(approxAxis(frame, IdViewAxis{}));
    CHECK_FALSE(frameFrom({}, {0.0f, 1.0f, 0.0f}, frame));
    CHECK_FALSE(frameFrom({1.0f, 0.0f, 0.0f}, {-3.0f, 0.0f, 0.0f}, frame));
}

TEST_CASE("quaternions round-trip through the axis") {
    for (const auto& angles :
         {evr::xr_math::IdAngles{0.0f, 0.0f, 0.0f}, evr::xr_math::IdAngles{10.0f, 170.0f, -30.0f},
          evr::xr_math::IdAngles{-89.0f, -120.0f, 179.0f}, evr::xr_math::IdAngles{0.0f, 180.0f, 0.0f}}) {
        const IdViewAxis axis = axisFromAngles(angles);
        CHECK(approxAxis(fromQuat(toQuat(axis)), axis));
    }
}

TEST_CASE("the twist about an axis") {
    const Vec3 along = normalize(Vec3{0.2f, 1.0f, -0.3f});
    const IdViewAxis from = kChild.axis;
    CHECK(approxEqual(twistAbout(from, rotateAbout(from, along, 1.0f), along), 1.0f));
    CHECK(approxEqual(twistAbout(from, rotateAbout(from, along, -2.5f), along), -2.5f));
    CHECK(approxEqual(twistAbout(from, from, along), 0.0f));
    // A swing about a perpendicular axis carries no twist.
    const Vec3 across = normalize(cross(along, Vec3{1.0f, 0.0f, 0.0f}));
    CHECK(approxEqual(twistAbout(from, rotateAbout(from, across, 0.7f), along), 0.0f));
    // Half a turn either way is the same.
    CHECK(approxEqual(std::fabs(twistAbout(from, rotateAbout(from, along, kPi), along)), kPi, 1e-3f));
}

TEST_CASE("blending poses") {
    const ModelPose a{{0.0f, 0.0f, 0.0f}, {}};
    const ModelPose b{{1.0f, 2.0f, 3.0f}, rotateAbout(IdViewAxis{}, {0.0f, 0.0f, 1.0f}, kPi / 2.0f)};
    CHECK(approxPose(blendPose(a, b, 0.0f), a));
    CHECK(approxPose(blendPose(a, b, 1.0f), b));
    const ModelPose half = blendPose(a, b, 0.5f);
    CHECK(approxEqual(half.position, Vec3{0.5f, 1.0f, 1.5f}));
    CHECK(approxAxis(half.axis, rotateAbout(IdViewAxis{}, {0.0f, 0.0f, 1.0f}, kPi / 4.0f)));
}

TEST_CASE("the blend's modifier composition is applyJointMod's") {
    // The blend's model-space pass (0x19E2A60) sets the joint's quaternion to mod * joint (Hamilton), and
    // SetJointMod (0x138CF10) makes the modifier's quaternion from the matrix it is given as if the rows
    // were the rotated axes. The joint's axis rows are its quaternion's rotated axes (GetJointTransforms).
    const ModelPose animated{{0.5f, 0.1f, 1.2f}, axisFromAngles({-30.0f, 100.0f, 25.0f})};
    const ModelPose target{{0.2f, 0.4f, 1.0f}, axisFromAngles({45.0f, -10.0f, 120.0f})};
    const evr::xr_math::JointMod mod = evr::xr_math::jointModToward(animated, target);
    const evr::Quat modQuat = toQuat(evr::xr_math::fromRows(mod.rotation));
    const IdViewAxis engine = fromQuat(modQuat * toQuat(animated.axis));
    CHECK(approxAxis(engine, evr::xr_math::applyJointMod(animated, mod).axis));
    CHECK(approxAxis(engine, target.axis));
}

TEST_CASE("a plausible pose is a proper rotation") {
    CHECK(plausiblePose(kChild));
    CHECK_FALSE(plausiblePose({{}, {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}}}));
    CHECK_FALSE(plausiblePose({{}, {{2.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}}));
}
