#include "features/input/controller_family.hpp"

#include <doctest/doctest.h>

#include <optional>

using evr::game::Controller;
using evr::input::buildControlMapAhead;
using evr::input::pickControllerFamily;

TEST_CASE("controller family: both hands agreeing, or one hand alone, decide") {
    CHECK(pickControllerFamily(Controller::ViveWand, Controller::ViveWand, Controller::OculusTouch) ==
          Controller::ViveWand);
    // The right controller asleep: the left one's family, not the one in use.
    CHECK(pickControllerFamily(Controller::ViveWand, std::nullopt, Controller::OculusTouch) ==
          Controller::ViveWand);
    CHECK(pickControllerFamily(std::nullopt, Controller::ValveIndex, Controller::OculusTouch) ==
          Controller::ValveIndex);
}

TEST_CASE("controller family: neither hand keeps the family in use") {
    CHECK(pickControllerFamily(std::nullopt, std::nullopt, Controller::SteamFrame) == Controller::SteamFrame);
}

TEST_CASE("controller family: hands disagreeing keep the family in use if it is one of them") {
    CHECK(pickControllerFamily(Controller::ValveIndex, Controller::OculusTouch, Controller::ValveIndex) ==
          Controller::ValveIndex);
    CHECK(pickControllerFamily(Controller::ValveIndex, Controller::OculusTouch, Controller::OculusTouch) ==
          Controller::OculusTouch);
    // Neither is in use: the right hand's.
    CHECK(pickControllerFamily(Controller::ValveIndex, Controller::OculusTouch, Controller::ViveWand) ==
          Controller::OculusTouch);
}

TEST_CASE("controller family: the control map is built ahead of gameplay once a controller is known") {
    // The title screen and the main menu: the runtime reports the controllers, no user command is built.
    CHECK(buildControlMapAhead(true, true, false));
    // Scripted input (rig tests) stands in for a controller.
    CHECK(buildControlMapAhead(true, false, true));
}

TEST_CASE("controller family: no control map ahead of gameplay before a controller is known") {
    // No profile yet: the family would only be the default, so the menus wait for the runtime.
    CHECK_FALSE(buildControlMapAhead(true, false, false));
    // No controller data this frame (the session not focused).
    CHECK_FALSE(buildControlMapAhead(false, true, false));
    CHECK_FALSE(buildControlMapAhead(false, true, true));
}
