#include "features/input/dashboard_pause.hpp"

#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <optional>
#include <ostream>

using evr::game::Controller;
using evr::game::GameAction;
using evr::game::Handedness;
using evr::input::applyDashboardPause;
using evr::input::BindingProfile;
using evr::input::ButtonBinding;
using evr::input::ButtonInput;
using evr::input::CaptureButtons;
using evr::input::captureButtonsFor;
using evr::input::Hand;
using evr::input::PressKind;
using evr::input::runtimeTakesMenuButton;
using evr::test::questTouchProfile;

namespace {

BindingProfile touchLike() {
    BindingProfile p;
    p.buttons = {
        {Hand::Left, ButtonInput::Menu, PressKind::Tap, GameAction::Pause},
        {Hand::Left, ButtonInput::Secondary, PressKind::Tap, GameAction::SwitchWeaponMod},
        {Hand::Left, ButtonInput::Secondary, PressKind::Hold, GameAction::MissionInfo},
        {Hand::Right, ButtonInput::Secondary, PressKind::WhileDown, GameAction::Dash},
    };
    return p;
}

} // namespace

TEST_CASE("dashboard pause: which runtimes and families") {
    CHECK(runtimeTakesMenuButton("SteamVR/OpenXR", Controller::OculusTouch));
    CHECK_FALSE(runtimeTakesMenuButton("VirtualDesktopXR", Controller::OculusTouch));
    CHECK_FALSE(runtimeTakesMenuButton("OpenXR Simulator Runtime", Controller::OculusTouch));
    // Index's Menu input is a trackpad press, and the other families have a system button of their own.
    for (const Controller family : evr::game::kControllers) {
        CAPTURE(evr::game::controllerName(family));
        CHECK(runtimeTakesMenuButton("SteamVR/OpenXR", family) == (family == Controller::OculusTouch));
        CHECK(captureButtonsFor("SteamVR/OpenXR", family) ==
              (family == Controller::OculusTouch ? CaptureButtons::MenuOrSticks : CaptureButtons::Menu));
        CHECK(captureButtonsFor("VirtualDesktopXR", family) == CaptureButtons::Menu);
    }
}

TEST_CASE("dashboard pause: Y hold pauses on the Menu hand") {
    BindingProfile p = touchLike();
    CHECK(applyDashboardPause(p) == Hand::Left);
    CHECK(p.buttons[2].action == GameAction::Pause);
    // The tap and the other hand stay as they were.
    CHECK(p.buttons[1].action == GameAction::SwitchWeaponMod);
    CHECK(p.buttons[3].action == GameAction::Dash);
}

TEST_CASE("dashboard pause: remapped buttons are left alone") {
    BindingProfile noMenuPause = touchLike();
    noMenuPause.buttons[0].action = GameAction::Automap;
    CHECK_FALSE(applyDashboardPause(noMenuPause));
    CHECK(noMenuPause.buttons[2].action == GameAction::MissionInfo);

    BindingProfile otherHold = touchLike();
    otherHold.buttons[2].action = GameAction::Dossier;
    CHECK_FALSE(applyDashboardPause(otherHold));
    CHECK(otherHold.buttons[2].action == GameAction::Dossier);

    // A map that already pauses on another button keeps its mission info.
    BindingProfile ownPause = touchLike();
    ownPause.buttons.push_back({Hand::Right, ButtonInput::Primary, PressKind::Hold, GameAction::Pause});
    CHECK_FALSE(applyDashboardPause(ownPause));
    CHECK(ownPause.buttons[2].action == GameAction::MissionInfo);
}

TEST_CASE("dashboard pause: the mission-info hold on the other hand pauses when the Menu hand has none") {
    BindingProfile p = touchLike();
    p.buttons[1].action = GameAction::Dash;
    p.buttons[1].kind = PressKind::WhileDown;
    p.buttons.erase(p.buttons.begin() + 2);
    p.buttons.push_back({Hand::Right, ButtonInput::Secondary, PressKind::Hold, GameAction::MissionInfo});
    CHECK(applyDashboardPause(p) == Hand::Right);
    CHECK(p.buttons.back().action == GameAction::Pause);
    CHECK(p.buttons[1].action == GameAction::Dash);
}

TEST_CASE("dashboard pause: every built-in Touch map gets a pause on a button SteamVR leaves to the game") {
    for (const Handedness handedness :
         {Handedness::Right, Handedness::LeftButtonSwap, Handedness::LeftButtonAndStickSwap}) {
        CAPTURE(static_cast<int>(handedness));
        BindingProfile p = questTouchProfile(handedness);
        const std::optional<Hand> hand = applyDashboardPause(p);
        REQUIRE(hand);
        // The full mirror has mission info on the right hand's B.
        CHECK(*hand == (handedness == Handedness::LeftButtonAndStickSwap ? Hand::Right : Hand::Left));
        CHECK(std::ranges::any_of(p.buttons, [&](const ButtonBinding& b) {
            return b.hand == *hand && b.input == ButtonInput::Secondary && b.kind == PressKind::Hold &&
                   b.action == GameAction::Pause;
        }));
    }
}
