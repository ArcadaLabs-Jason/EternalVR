#include "features/arm/left_hand.hpp"

#include "features/arm/arm_fixture.hpp"
#include "xr_math/head_aim.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

using evr::Vec3;
using evr::arm::compose;
using evr::arm::GripPose;
using evr::arm::HandOffset;
using evr::arm::heavyCannonGrip;
using evr::arm::kArmJointCount;
using evr::arm::kHandJointCount;
using evr::arm::kHandJointParents;
using evr::arm::leftGloveShift;
using evr::arm::leftHandTurn;
using evr::arm::mirroredGrip;
using evr::arm::mirroredInRoot;
using evr::arm::mirroredLocal;
using evr::arm::onGunRoot;
using evr::arm::plausiblePose;
using evr::arm::relative;
using evr::arm::rightWristFromGrip;
using evr::arm::RowPlacement;
using evr::arm::rowPlacement;
using evr::arm::sameRootPose;
using evr::arm::SteadyPose;
using evr::arm::wristFromGrip;
using evr::test::approxEqual;
using evr::test::approxPose;
using evr::test::bindArm;
using evr::test::bindRightArm;
using evr::xr_math::applyLocalOffset;
using evr::xr_math::applyLocalOffsetWithSight;
using evr::xr_math::axisFromAngles;
using evr::xr_math::EyeRelativePose;
using evr::xr_math::IdAngles;
using evr::xr_math::IdViewAxis;
using evr::xr_math::ModelPose;

namespace {

// The right glove's wrist point the rows put in the hand (game::kRowsWrist), and the left hand's.
constexpr Vec3 kRightWristPoint{-0.08f, -0.035f, 0.0f};
constexpr Vec3 kLeftWristPoint{-0.08f, 0.035f, 0.0f};

} // namespace

TEST_CASE("the right arm's bind pose in the attach joint's frame is the left arm's mirrored there") {
    // Both attach joints share one pose (the bind pose and every animation the hands rig dumped), so a joint
    // of one arm in that frame mirrors to the other arm's.
    const auto left = bindArm();
    const auto right = bindRightArm();
    const ModelPose& attach = left[0];
    REQUIRE(approxPose(attach, right[0]));
    for (std::size_t j = 1; j < kArmJointCount; ++j) {
        CAPTURE(j);
        const ModelPose m = mirroredInRoot(relative(attach, right[j]));
        CHECK(approxPose(m, relative(attach, left[j]), 2e-5f));
        CHECK(plausiblePose(m));
    }
}

TEST_CASE("a joint in its parent's frame keeps its turn on the other arm, its offset the other way") {
    const auto left = bindArm();
    const auto right = bindRightArm();
    for (std::size_t j = 2; j < kArmJointCount; ++j) {
        CAPTURE(j);
        CHECK(approxPose(mirroredLocal(relative(right[j - 1], right[j])), relative(left[j - 1], left[j]),
                         2e-5f));
    }
}

TEST_CASE("the Heavy Cannon's right grip mirrored puts every left hand joint at the right one's reflection") {
    // Each joint placed down the hand from the mirrored grip lands where the right hand's joint does,
    // reflected across the gun's middle (the root's x-z plane): the fist closes on the same grip from the
    // other side.
    const GripPose& right = heavyCannonGrip();
    const GripPose left = mirroredGrip(right);
    std::array<ModelPose, kHandJointCount> r{};
    std::array<ModelPose, kHandJointCount> l{};
    for (std::size_t i = 0; i < kHandJointCount; ++i) {
        const auto p = kHandJointParents[i];
        r[i] = compose(p < 0 ? right.wrist : r[static_cast<std::size_t>(p)], right.hand[i]);
        l[i] = compose(p < 0 ? left.wrist : l[static_cast<std::size_t>(p)], left.hand[i]);
        CAPTURE(i);
        CHECK(approxEqual(l[i].position, Vec3{r[i].position.x, -r[i].position.y, r[i].position.z}, 1e-5f));
        CHECK(plausiblePose(l[i]));
    }
    CHECK(approxEqual(left.wrist.position,
                      Vec3{right.wrist.position.x, -right.wrist.position.y, right.wrist.position.z}));
}

TEST_CASE("the right controller gets a right glove: thumb up, fingers forward, palm to the left") {
    // A controller held straight ahead: the right hand's wrist 8 cm behind its grip and 3.5 cm toward the
    // back of the hand (to the right), its fingers along the hand's -y, its thumb on -x, its palm on +z
    // (the left hand's axes reflected and negated).
    const ModelPose grip{{0.4f, -0.2f, -0.3f}, {}};
    const ModelPose wrist = rightWristFromGrip(grip, HandOffset{{-0.08f, 0.035f, 0.0f}, {}});
    CHECK(approxEqual(wrist.position, Vec3{0.32f, -0.235f, -0.3f}));
    CHECK(approxEqual(-wrist.axis.left, Vec3{1.0f, 0.0f, 0.0f}));    // fingers forward
    CHECK(approxEqual(-wrist.axis.forward, Vec3{0.0f, 0.0f, 1.0f})); // thumb up
    CHECK(approxEqual(wrist.axis.up, Vec3{0.0f, 1.0f, 0.0f}));       // palm to the left
    CHECK(plausiblePose(wrist));
}

TEST_CASE("a turned right controller turns the right glove as the mirrored left one turns") {
    const ModelPose leftGrip{{0.35f, 0.25f, -0.2f}, axisFromAngles({20.0f, 35.0f, -50.0f})};
    const ModelPose rightGrip{{0.35f, -0.25f, -0.2f}, axisFromAngles({20.0f, -35.0f, 50.0f})};
    const HandOffset offset{{-0.08f, 0.035f, 0.0f}, {5.0f, 10.0f, -15.0f}};
    const ModelPose left = wristFromGrip(leftGrip, offset);
    const ModelPose right = rightWristFromGrip(rightGrip, offset);
    CHECK(approxPose(right, evr::test::mirrored(left)));
}

TEST_CASE("the left glove's wrist lands in the left hand on the combat shotgun (rig sample)") {
    // The hands rig's read of the combat shotgun's idle (game units = metres on the rig): the gun's root and
    // the right glove's wrist, model space. The root is turned 2.2 degrees from the model's axes (the gun's
    // own sight line), so the left glove's grip, reflected across the root's x-z plane, is not the right
    // one's reflection across a plane of the model's: moved to the reflected wrist point by the row alone, it
    // would miss by over a centimetre.
    const ModelPose root{
        {0.26785f, -0.11381f, -0.13931f},
        {{0.99939f, 0.03354f, 0.01016f}, {-0.03331f, 0.9992f, -0.02201f}, {-0.01089f, 0.02166f, 0.99971f}}};
    const ModelPose right{
        {0.09976f, -0.16552f, -0.22571f},
        {{0.29559f, 0.11533f, -0.94833f}, {-0.94444f, -0.11407f, -0.30826f}, {-0.14373f, 0.98676f, 0.0752f}}};
    const Vec3 left = compose(root, mirroredInRoot(relative(root, right))).position;
    // Placed by the combat shotgun's row (no turn) at a grip held straight ahead: the right glove's wrist is
    // at the right hand's wrist point to 4 mm.
    const EyeRelativePose placed{{-0.183f, 0.131f, 0.223f}, {}};
    REQUIRE(length(placed.offset + right.position - kRightWristPoint) < 0.005f);
    const Vec3 shift = leftGloveShift(placed, left, {}, {}, kLeftWristPoint, 1.0f);
    CHECK(length(placed.offset + shift + left - kLeftWristPoint) < 1e-5f);
    // The row reflected across the plane of the model's x and z axes through the root puts it over 1 cm off.
    const Vec3 reflectedRow{-0.183f, -0.131f - 2.0f * root.position.y, 0.223f};
    CHECK(length(reflectedRow + left - kLeftWristPoint) > 0.01f);
}

TEST_CASE("the shift moves a turned model, not turning it, onto the grip's own wrist point") {
    // A model placed turned (a sight row's turn and a Crucible-like row) at a grip whose own orientation is
    // off the aim ray: the left glove's wrist lands on the left hand's wrist point in the grip's frame.
    const EyeRelativePose placed{{0.3f, 0.2f, -0.4f}, axisFromAngles({-24.39f, 23.75f, 10.59f})};
    const Vec3 wrist{0.12f, -0.05f, -0.2f};
    const Vec3 grip{0.31f, 0.18f, -0.25f};
    const IdViewAxis gripAxis = axisFromAngles({30.0f, -10.0f, 5.0f});
    const Vec3 shift = leftGloveShift(placed, wrist, grip, gripAxis, kLeftWristPoint, 1.0f);
    const IdViewAxis& a = placed.axis;
    const Vec3 landed = placed.offset + shift + a.forward * wrist.x + a.left * wrist.y + a.up * wrist.z;
    const Vec3 target = grip + gripAxis.forward * kLeftWristPoint.x + gripAxis.left * kLeftWristPoint.y +
                        gripAxis.up * kLeftWristPoint.z;
    CHECK(approxEqual(landed, target, 1e-5f));
}

namespace {

// The model's placement at the eye on the identity axis (metres) for a translation, turn and sight.
EyeRelativePose placedAt(Vec3 translation, const IdAngles& turn, const std::optional<IdAngles>& sight) {
    return sight ? applyLocalOffsetWithSight({}, translation, turn, *sight, 1.0f)
                 : applyLocalOffset({}, translation, turn, 1.0f);
}

// Where the model's point `point` (metres, the model's frame) lands for a placement.
Vec3 landed(const EyeRelativePose& placed, Vec3 point) {
    return placed.offset + placed.axis.forward * point.x + placed.axis.left * point.y +
           placed.axis.up * point.z;
}

} // namespace

TEST_CASE(
    "a mod's sight row turns the gun about the wrist point under the turn applied, left hand included") {
    // A row with a turn of its own (as the Crucible's), and the Heavy Cannon's two sight rows: the iron
    // sights (the weapon's own, which the offset rows are measured with) and the Precision Bolt's scope (a
    // mod's).
    const Vec3 rowTranslation{-0.27f, 0.114f, 0.204f};
    const IdAngles rowTurn{-4.0f, -6.0f, -3.0f};
    const std::optional<IdAngles> irons = IdAngles{-3.45f, 0.19f, 0.0f};
    const std::optional<IdAngles> scope = IdAngles{-0.15f, 0.15f, 0.0f};
    for (const float weight : {0.0f, 0.3f, 0.5f, 1.0f}) {
        CAPTURE(weight);
        const RowPlacement got =
            rowPlacement(rowTranslation, rowTurn, weight, irons, scope, kRightWristPoint, true);
        const IdAngles applied = leftHandTurn(rowTurn, weight);
        CHECK(approxEqual(got.rotation.pitch, applied.pitch));
        CHECK(approxEqual(got.rotation.yaw, applied.yaw));
        CHECK(approxEqual(got.rotation.roll, applied.roll));
        // The model point the weapon's own row puts at the wrist point, with the turn applied, stays there.
        const EyeRelativePose before = placedAt(rowTranslation, applied, irons);
        const Vec3 d = kRightWristPoint - before.offset;
        const Vec3 point{dot(before.axis.forward, d), dot(before.axis.left, d), dot(before.axis.up, d)};
        CHECK(approxEqual(landed(placedAt(got.translation, got.rotation, scope), point), kRightWristPoint,
                          1e-5f));
    }
}

TEST_CASE("the weapon's own sight row, or an offset given in place of the table's, keeps its translation") {
    const Vec3 t{-0.27f, 0.114f, 0.204f};
    const IdAngles rowTurn{-4.0f, -6.0f, -3.0f};
    const std::optional<IdAngles> irons = IdAngles{-3.45f, 0.19f, 0.0f};
    const std::optional<IdAngles> scope = IdAngles{-0.15f, 0.15f, 0.0f};
    for (const float weight : {0.0f, 0.5f}) {
        const RowPlacement same = rowPlacement(t, rowTurn, weight, irons, irons, kRightWristPoint, true);
        CHECK((same.translation.x == t.x && same.translation.y == t.y && same.translation.z == t.z));
        // ETERNALVR_VIEWMODEL_OFFSET replaces the row untouched, a mod's sight row or not.
        const RowPlacement given = rowPlacement(t, rowTurn, weight, irons, scope, kRightWristPoint, false);
        CHECK((given.translation.x == t.x && given.translation.y == t.y && given.translation.z == t.z));
        CHECK(approxEqual(given.rotation.yaw, leftHandTurn(rowTurn, weight).yaw));
    }
}

TEST_CASE("the row's turn is reflected for the left glove, blended with its share on the grip") {
    const IdAngles crucible{-24.39f, -23.75f, -10.59f};
    const IdAngles none = leftHandTurn(crucible, 0.0f);
    CHECK(approxEqual(none.yaw, crucible.yaw));
    CHECK(approxEqual(none.roll, crucible.roll));
    const IdAngles full = leftHandTurn(crucible, 1.0f);
    CHECK(approxEqual(full.pitch, crucible.pitch));
    CHECK(approxEqual(full.yaw, 23.75f));
    CHECK(approxEqual(full.roll, 10.59f));
    // The left glove's turn is the right one's reflected: the model's forward reflected across its x-z plane.
    const Vec3 f = axisFromAngles(crucible).forward;
    CHECK(approxEqual(axisFromAngles(full).forward, Vec3{f.x, -f.y, f.z}, 1e-5f));
    CHECK(approxEqual(leftHandTurn(crucible, 0.5f).yaw, 0.0f));
}

TEST_CASE("the game's left grip is put on the gun's root only where the two attach joints were read as one") {
    // The off hand's right glove takes the game's left grip, read under the left attach joint, and is written
    // under the gun's root. Read with the two at one pose (every idle the rig dumped) the grip is unchanged;
    // a few millimetres apart it is carried exactly; read while the chaingun's mod held them 13 cm apart, it
    // is refused (written under the gun's root as it was, the glove would sit 13 cm off the gun).
    const ModelPose gunRoot{{0.705f, -0.152f, -0.255f}, axisFromAngles({1.0f, 2.0f, 0.5f})};
    const ModelPose wrist{{-0.35f, 0.12f, 0.05f}, axisFromAngles({10.0f, -80.0f, 30.0f})};
    const auto same = onGunRoot(gunRoot, wrist, gunRoot, 0.01f);
    REQUIRE(same);
    CHECK(approxPose(*same, wrist, 1e-5f));
    const ModelPose near{gunRoot.position + Vec3{0.004f, -0.003f, 0.002f},
                         axisFromAngles({1.5f, 2.5f, 0.0f})};
    const auto carried = onGunRoot(near, wrist, gunRoot, 0.01f);
    REQUIRE(carried);
    CHECK(approxPose(compose(gunRoot, *carried), compose(near, wrist), 1e-5f));
    // The chaingun's mod held (rig: the left attach joint 13.2 cm from the gun's root).
    const ModelPose parted{gunRoot.position + Vec3{-0.12f, 0.05f, 0.02f}, gunRoot.axis};
    CHECK_FALSE(onGunRoot(parted, wrist, gunRoot, 0.01f));
    const ModelPose turned{gunRoot.position, axisFromAngles({1.0f, 6.0f, 0.5f})};
    CHECK_FALSE(onGunRoot(turned, wrist, gunRoot, 0.01f));
}

TEST_CASE("a grip is taken only from reads whose gun root has held still") {
    // The combat shotgun's root at idle (rig), and the same gun still coming up after a melee: read while it
    // moves, the root sits centimetres from its idle place in the model and the gun would be placed that far
    // off the hand (rig: 8 cm after a melee before this rule).
    const ModelPose idle{{0.273f, -0.115f, -0.139f}, {}};
    SteadyPose steady;
    const float maxMove = 0.0005f;
    std::uint32_t serial = 0;
    for (const float drop : {0.20f, 0.12f, 0.05f, 0.01f}) {
        CHECK_FALSE(steady.update({idle.position + Vec3{-drop, 0.0f, -drop}, {}}, ++serial, maxMove));
    }
    CHECK_FALSE(steady.update(idle, ++serial, maxMove)); // the first read at rest
    CHECK_FALSE(steady.update(idle, ++serial, maxMove));
    CHECK(steady.update({idle.position + Vec3{0.0001f, 0.0f, 0.0f}, {}}, ++serial, maxMove)); // the third
    CHECK(steady.update(idle, serial, maxMove));           // the same read again: still taken
    CHECK_FALSE(steady.update(idle, serial + 2, maxMove)); // a read skipped (the arm posed): counted again
    CHECK_FALSE(
        steady.update({idle.position, axisFromAngles({0.0f, 0.5f, 0.0f})}, serial + 3, maxMove)); // turned
}

TEST_CASE("two reads of a root are one pose within 1 cm and 2 degrees") {
    const ModelPose root{{0.26785f, -0.11381f, -0.13931f}, {}};
    CHECK(sameRootPose(root, root, 0.01f));
    ModelPose moved = root;
    moved.position.x += 0.008f;
    CHECK(sameRootPose(root, moved, 0.01f));
    moved.position.x += 0.004f; // 1.2 cm
    CHECK_FALSE(sameRootPose(root, moved, 0.01f));
    ModelPose turned = root;
    turned.axis = axisFromAngles({0.0f, 3.0f, 0.0f});
    CHECK_FALSE(sameRootPose(root, turned, 0.01f));
    turned.axis = axisFromAngles({0.0f, 1.5f, 0.0f});
    CHECK(sameRootPose(root, turned, 0.01f));
}
