#include "xr_math/offhand_pose.hpp"

#include "support/approx.hpp"
#include "xr_math/head_aim.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <limits>

using evr::Vec3;
using evr::test::approxEqual;
using evr::xr_math::applyJointMod;
using evr::xr_math::axisFromAngles;
using evr::xr_math::blendJointMods;
using evr::xr_math::EyeRelativePose;
using evr::xr_math::IdAngles;
using evr::xr_math::IdViewAxis;
using evr::xr_math::inModelSpace;
using evr::xr_math::JointMod;
using evr::xr_math::jointModToward;
using evr::xr_math::Mat3Rows;
using evr::xr_math::ModelPose;
using evr::xr_math::multiply;
using evr::xr_math::plausibleAnimatedPose;
using evr::xr_math::plausibleJointMod;
using evr::xr_math::toRows;

namespace {

bool approxAxis(const IdViewAxis& a, const IdViewAxis& b, float eps = 1e-4f) {
    return approxEqual(a.forward, b.forward, eps) && approxEqual(a.left, b.left, eps) &&
           approxEqual(a.up, b.up, eps);
}

bool approxRows(const Mat3Rows& a, const Mat3Rows& b, float eps = 1e-4f) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!approxEqual(a[i], b[i], eps)) {
            return false;
        }
    }
    return true;
}

// A point in model space (row vector times the rows) as the game's lag code moves it.
Vec3 apply(Vec3 p, const Mat3Rows& m) {
    return {p.x * m[0] + p.y * m[3] + p.z * m[6], p.x * m[1] + p.y * m[4] + p.z * m[7],
            p.x * m[2] + p.y * m[5] + p.z * m[8]};
}

} // namespace

TEST_CASE("offhand pose: a target in the model's space") {
    // The model yawed 90 degrees left (its forward is the world's +Y), 10 units ahead of the eye.
    EyeRelativePose model;
    model.offset = {10.0f, 0.0f, 0.0f};
    model.axis = axisFromAngles({0.0f, 90.0f, 0.0f});
    EyeRelativePose target;
    target.offset = {10.0f, 5.0f, 2.0f}; // 5 units along the model's forward, 2 up
    target.axis = model.axis;
    const ModelPose m = inModelSpace(model, target);
    CHECK(approxEqual(m.position, {5.0f, 0.0f, 2.0f}));
    CHECK(approxAxis(m.axis, IdViewAxis{})); // the same axis as the model: identity in model space
    // A target turned a further 90 degrees: its forward is the model's left.
    target.axis = axisFromAngles({0.0f, 180.0f, 0.0f});
    const ModelPose turned = inModelSpace(model, target);
    CHECK(approxEqual(turned.axis.forward, {0.0f, 1.0f, 0.0f}));
    CHECK(approxEqual(turned.axis.left, {-1.0f, 0.0f, 0.0f}));
}

TEST_CASE("offhand pose: the modifier reaches the target from the animated pose") {
    ModelPose animated{{4.0f, 6.0f, -3.0f}, axisFromAngles({20.0f, -35.0f, 10.0f})};
    const ModelPose target{{9.0f, -2.0f, 1.0f}, axisFromAngles({-15.0f, 70.0f, -40.0f})};
    const JointMod mod = jointModToward(animated, target);
    CHECK(approxEqual(animated.position + mod.translation, target.position));
    // axis (rows) * rotation = the target's axis
    CHECK(approxRows(multiply(toRows(animated.axis), mod.rotation), toRows(target.axis)));
    CHECK(plausibleJointMod(mod, 100.0f));
    CHECK_FALSE(plausibleJointMod(mod, 5.0f)); // too long a reach
}

TEST_CASE("offhand pose: applying the modifier gives the target back") {
    const ModelPose animated{{0.5f, 0.1f, 1.2f}, axisFromAngles({-30.0f, 100.0f, 25.0f})};
    const ModelPose target{{0.2f, 0.4f, 1.0f}, axisFromAngles({45.0f, -10.0f, 120.0f})};
    const ModelPose reached = applyJointMod(animated, jointModToward(animated, target));
    CHECK(approxEqual(reached.position, target.position));
    CHECK(approxAxis(reached.axis, target.axis));
    // No change leaves the joint where it was animated.
    const ModelPose same = applyJointMod(animated, JointMod{});
    CHECK(approxEqual(same.position, animated.position));
    CHECK(approxAxis(same.axis, animated.axis));
}

TEST_CASE("offhand pose: the lag's own modifier convention") {
    // The game rotates a joint about a pivot by M (row vector times M) and passes the change in position:
    // with the modifier convention above, the rotated axis is axis * M.
    const Mat3Rows m = toRows(axisFromAngles({0.0f, 10.0f, 0.0f}));
    const Vec3 pivot{1.0f, 2.0f, 3.0f};
    const Vec3 joint{11.0f, 2.0f, 3.0f};
    const Vec3 moved = pivot + apply(joint - pivot, m);
    const Vec3 translation = moved - joint;
    CHECK(approxEqual(joint + translation, moved));
    CHECK(std::fabs(translation.y) > 1.0f); // a 10 degree yaw about a pivot 10 units away
}

TEST_CASE("offhand pose: blending the game's modifier with ours") {
    const JointMod game{{1.0f, 0.0f, 0.0f}, toRows(IdViewAxis{})};
    const JointMod ours{{3.0f, 4.0f, 0.0f}, toRows(axisFromAngles({0.0f, 90.0f, 0.0f}))};
    const JointMod none = blendJointMods(game, ours, 0.0f);
    CHECK(approxEqual(none.translation, game.translation));
    CHECK(approxRows(none.rotation, game.rotation));
    const JointMod all = blendJointMods(game, ours, 1.0f);
    CHECK(approxRows(all.rotation, ours.rotation));
    const JointMod half = blendJointMods(game, ours, 0.5f);
    CHECK(approxEqual(half.translation, {2.0f, 2.0f, 0.0f}));
    CHECK(approxRows(half.rotation, toRows(axisFromAngles({0.0f, 45.0f, 0.0f}))));
    CHECK(plausibleJointMod(half, 10.0f));
    const JointMod nan = blendJointMods(game, ours, std::numeric_limits<float>::quiet_NaN());
    CHECK(approxEqual(nan.translation, game.translation));
    // Blending across the long way round still takes the short arc.
    const JointMod back{{0.0f, 0.0f, 0.0f}, toRows(axisFromAngles({0.0f, 170.0f, 0.0f}))};
    const JointMod front{{0.0f, 0.0f, 0.0f}, toRows(axisFromAngles({0.0f, -170.0f, 0.0f}))};
    CHECK(
        approxRows(blendJointMods(back, front, 0.5f).rotation, toRows(axisFromAngles({0.0f, 180.0f, 0.0f}))));
}

TEST_CASE("offhand pose: implausible values never pass") {
    JointMod mod;
    CHECK(plausibleJointMod(mod, 1.0f));
    mod.translation.x = std::numeric_limits<float>::infinity();
    CHECK_FALSE(plausibleJointMod(mod, 1.0f));
    JointMod scaled;
    scaled.rotation[0] = 2.0f;
    CHECK_FALSE(plausibleJointMod(scaled, 1.0f));
    JointMod mirrored;
    mirrored.rotation[8] = -1.0f; // a reflection
    CHECK_FALSE(plausibleJointMod(mirrored, 1.0f));
    // The game's fallback when it could not read the joint: zero position, identity axis.
    CHECK_FALSE(plausibleAnimatedPose(ModelPose{}));
    CHECK(plausibleAnimatedPose(ModelPose{{1.0f, 0.0f, 0.0f}, IdViewAxis{}}));
    CHECK(plausibleAnimatedPose(ModelPose{{}, axisFromAngles(IdAngles{0.0f, 30.0f, 0.0f})}));
}
