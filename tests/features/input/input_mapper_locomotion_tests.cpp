#include "features/input/input_mapper.hpp"

#include "features/input/binding_compiler.hpp"
#include "features/input/controller_bindings.hpp"
#include "features/input/input_frames.hpp"
#include "game/eternal/controller_data.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <numbers>
#include <ostream>
#include <string>

// Hand-relative movement: ETERNALVR_LOCOMOTION=left or right follows that hand in every handedness map;
// the older hand follows the hand with the move stick.

using evr::game::builtinControllerData;
using evr::game::Controller;
using evr::game::controllerName;
using evr::game::Handedness;
using evr::game::kControllers;
using evr::input::Axis2;
using evr::input::BindingProfile;
using evr::input::buildBindingProfile;
using evr::input::ControllerData;
using evr::input::Hand;
using evr::input::InputFrame;
using evr::input::InputMapper;
using evr::input::LocomotionFrame;
using evr::input::locomotionHand;
using evr::input::MapperSettings;
using evr::input::parseControllerData;
using evr::test::questTouchProfile;
using evr::test::restingFrame;
using evr::test::yawPose;

namespace {

constexpr float kFrame = 1.0f / 90.0f;
constexpr float kQuarterTurn = std::numbers::pi_v<float> / 2.0f;

// The head faces -Z; the left hand points left (-X) and the right hand right (+X). `moveStick` is
// pushed fully forward.
InputFrame pointingApart(Hand moveStick) {
    InputFrame frame = restingFrame();
    frame.left.aimPose.orientation = yawPose(kQuarterTurn).orientation;
    frame.right.aimPose.orientation = yawPose(-kQuarterTurn).orientation;
    (moveStick == Hand::Left ? frame.left : frame.right).stick = {0.0f, 1.0f};
    return frame;
}

// The move sent to the game (view yaw 0, so +x is right in tracking space and +y forward).
Axis2 moveFor(const BindingProfile& profile, LocomotionFrame locomotion) {
    MapperSettings settings;
    settings.locomotionFrame = locomotion;
    InputMapper mapper(profile, settings);
    REQUIRE(profile.moveStick.has_value());
    return mapper.update(pointingApart(*profile.moveStick), {}, kFrame).move;
}

struct MapCase {
    Handedness handedness;
    Hand weaponHand;
    Hand moveStick;
    const char* summary;
};

} // namespace

TEST_CASE("hand-relative movement follows the move stick's hand in every Touch map") {
    const MapCase cases[] = {
        {Handedness::Right, Hand::Right, Hand::Left,
         "right-handed, move stick left, moving where the left hand points"},
        // Buttons swapped: the weapon goes left but the move stick stays left, so the left hand steers.
        {Handedness::LeftButtonSwap, Hand::Left, Hand::Left,
         "left-handed, move stick left, moving where the left hand points"},
        {Handedness::LeftButtonAndStickSwap, Hand::Left, Hand::Right,
         "left-handed, move stick right, moving where the right hand points"},
    };
    for (const MapCase& c : cases) {
        CAPTURE(static_cast<int>(c.handedness));
        const BindingProfile profile = questTouchProfile(c.handedness);
        CHECK(profile.weaponHand == c.weaponHand);
        REQUIRE(profile.moveStick == c.moveStick);
        CHECK(locomotionHand(profile) == c.moveStick);

        const Axis2 move = moveFor(profile, LocomotionFrame::MoveHand);
        CHECK(move.x == doctest::Approx(c.moveStick == Hand::Left ? -1.0f : 1.0f));
        CHECK(move.y == doctest::Approx(0.0f).epsilon(1e-5));

        MapperSettings settings;
        settings.locomotionFrame = LocomotionFrame::MoveHand;
        CHECK(InputMapper(profile, settings).summary() == c.summary);
    }
}

TEST_CASE("head-relative movement ignores both hands in every Touch map") {
    for (const Handedness handedness :
         {Handedness::Right, Handedness::LeftButtonSwap, Handedness::LeftButtonAndStickSwap}) {
        CAPTURE(static_cast<int>(handedness));
        const BindingProfile profile = questTouchProfile(handedness);
        const Axis2 move = moveFor(profile, LocomotionFrame::Head);
        CHECK(move.x == doctest::Approx(0.0f).epsilon(1e-5));
        CHECK(move.y == doctest::Approx(1.0f));
        CHECK(InputMapper(profile).summary().ends_with(", moving where the head faces"));
    }
}

TEST_CASE("a map with no move stick steers by the hand not holding the weapon") {
    BindingProfile profile = questTouchProfile(Handedness::LeftButtonSwap);
    profile.moveStick.reset();
    CHECK(locomotionHand(profile) == Hand::Right);
    profile.weaponHand = Hand::Right;
    CHECK(locomotionHand(profile) == Hand::Left);
    MapperSettings settings;
    settings.locomotionFrame = LocomotionFrame::MoveHand;
    CHECK(InputMapper(profile, settings).summary() ==
          "right-handed, move stick none, moving where the left hand points");
}

TEST_CASE("every built-in map steers hand-relative movement by its move stick's hand") {
    for (const Controller controller : kControllers) {
        const ControllerData data = parseControllerData(builtinControllerData(controller));
        for (const auto& [handedness, entries] : data.maps) {
            INFO(std::string(controllerName(controller)), " map ", static_cast<int>(handedness));
            const BindingProfile profile = buildBindingProfile(entries).profile;
            REQUIRE(profile.moveStick.has_value());
            CHECK(locomotionHand(profile) == *profile.moveStick);
            // The move stick is on the weapon hand only where the buttons, not the sticks, swap sides.
            CHECK((*profile.moveStick == profile.weaponHand) == (handedness == Handedness::LeftButtonSwap));
        }
    }
}

TEST_CASE("left and right follow that hand whatever the handedness") {
    for (const Handedness handedness :
         {Handedness::Right, Handedness::LeftButtonSwap, Handedness::LeftButtonAndStickSwap}) {
        CAPTURE(static_cast<int>(handedness));
        const BindingProfile profile = questTouchProfile(handedness);
        const std::string stick = *profile.moveStick == Hand::Left ? "left" : "right";
        const std::string start =
            std::string(profile.weaponHand == Hand::Left ? "left" : "right") + "-handed, move stick " + stick;
        for (const Hand hand : {Hand::Left, Hand::Right}) {
            CAPTURE(static_cast<int>(hand));
            const LocomotionFrame frame =
                hand == Hand::Left ? LocomotionFrame::LeftHand : LocomotionFrame::RightHand;
            // The left hand points left (-X), the right hand right (+X): forward on the stick goes that way.
            const Axis2 move = moveFor(profile, frame);
            CHECK(move.x == doctest::Approx(hand == Hand::Left ? -1.0f : 1.0f));
            CHECK(move.y == doctest::Approx(0.0f).epsilon(1e-5));
            MapperSettings settings;
            settings.locomotionFrame = frame;
            CHECK(InputMapper(profile, settings).summary() ==
                  start + ", moving where the " + (hand == Hand::Left ? "left" : "right") + " hand points");
        }
    }
}
