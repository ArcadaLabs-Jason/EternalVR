#include "features/input/dashboard_pause.hpp"

#include <doctest/doctest.h>

using evr::game::GameAction;
using evr::input::applyDashboardPause;
using evr::input::BindingProfile;
using evr::input::ButtonBinding;
using evr::input::ButtonInput;
using evr::input::Hand;
using evr::input::PressKind;
using evr::input::runtimeTakesMenuButton;

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

TEST_CASE("dashboard pause: which runtimes") {
    CHECK(runtimeTakesMenuButton("SteamVR/OpenXR"));
    CHECK_FALSE(runtimeTakesMenuButton("VirtualDesktopXR"));
    CHECK_FALSE(runtimeTakesMenuButton("OpenXR Simulator Runtime"));
}

TEST_CASE("dashboard pause: Y hold pauses on the Menu hand") {
    BindingProfile p = touchLike();
    CHECK(applyDashboardPause(p) == 1);
    CHECK(p.buttons[2].action == GameAction::Pause);
    // The tap and the other hand stay as they were.
    CHECK(p.buttons[1].action == GameAction::SwitchWeaponMod);
    CHECK(p.buttons[3].action == GameAction::Dash);
}

TEST_CASE("dashboard pause: remapped buttons are left alone") {
    BindingProfile noMenuPause = touchLike();
    noMenuPause.buttons[0].action = GameAction::Automap;
    CHECK(applyDashboardPause(noMenuPause) == 0);
    CHECK(noMenuPause.buttons[2].action == GameAction::MissionInfo);

    BindingProfile otherHold = touchLike();
    otherHold.buttons[2].action = GameAction::Dossier;
    CHECK(applyDashboardPause(otherHold) == 0);
    CHECK(otherHold.buttons[2].action == GameAction::Dossier);
}
