#include "features/input/dossier_press.hpp"

#include "features/input/binding_compiler.hpp"
#include "features/input/controller_bindings.hpp"
#include "features/input/input_frames.hpp"
#include "features/input/input_mapper.hpp"
#include "game/eternal/controller_data.hpp"

#include <doctest/doctest.h>

#include <array>
#include <ostream>
#include <string>

using evr::game::builtinControllerData;
using evr::game::contains;
using evr::game::Controller;
using evr::game::GameAction;
using evr::game::Handedness;
using evr::input::applyDossierPress;
using evr::input::BindingProfile;
using evr::input::buildBindingProfile;
using evr::input::DossierPress;
using evr::input::GameInput;
using evr::input::Hand;
using evr::input::InputFrame;
using evr::input::InputMapper;
using evr::input::parseControllerData;
using evr::test::restingFrame;

namespace {

constexpr float kFrame = 1.0f / 90.0f;
constexpr std::array kControllers{Controller::OculusTouch, Controller::ValveIndex};
constexpr std::array kAllHandedness{Handedness::Right, Handedness::LeftButtonSwap,
                                    Handedness::LeftButtonAndStickSwap};

BindingProfile builtIn(Controller controller, Handedness handedness) {
    const auto data = parseControllerData(builtinControllerData(controller));
    return buildBindingProfile(data.maps.at(handedness)).profile;
}

// X on the left controller, or A on the right in the full mirror (the face buttons swap there).
Hand dossierHand(Handedness handedness) {
    return handedness == Handedness::LeftButtonAndStickSwap ? Hand::Right : Hand::Left;
}

InputFrame primaryDown(Hand hand) {
    InputFrame frame = restingFrame();
    (hand == Hand::Left ? frame.left : frame.right).primaryButton = true;
    return frame;
}

struct Presses {
    bool tapSwitches = false;
    bool tapOpensDossier = false;
    bool holdSwitches = false;
    bool holdOpensDossier = false;
};

Presses pressesOf(const BindingProfile& profile, Hand hand) {
    Presses out;
    {
        InputMapper mapper(profile);
        mapper.update(primaryDown(hand), {}, kFrame);
        const GameInput tap = mapper.update(restingFrame(), {}, kFrame);
        out.tapSwitches = contains(tap.down, GameAction::SwitchEquipment);
        out.tapOpensDossier = contains(tap.down, GameAction::Dossier);
    }
    InputMapper mapper(profile);
    GameInput held;
    for (int i = 0; i < 40; ++i) { // 0.44 s: past the 0.25 s hold time
        held = mapper.update(primaryDown(hand), {}, kFrame);
    }
    out.holdSwitches = contains(held.down, GameAction::SwitchEquipment);
    out.holdOpensDossier = contains(held.down, GameAction::Dossier);
    return out;
}

} // namespace

TEST_CASE("Dossier on hold (the default): X taps switch equipment, a hold opens the Dossier") {
    for (const Controller controller : kControllers) {
        for (const Handedness handedness : kAllHandedness) {
            CAPTURE(static_cast<int>(controller));
            CAPTURE(static_cast<int>(handedness));
            BindingProfile profile = builtIn(controller, handedness);
            CHECK(applyDossierPress(profile, DossierPress::Hold) == 0);
            const Presses p = pressesOf(profile, dossierHand(handedness));
            CHECK(p.tapSwitches);
            CHECK_FALSE(p.tapOpensDossier);
            CHECK(p.holdOpensDossier);
            CHECK_FALSE(p.holdSwitches);
        }
    }
}

TEST_CASE("Dossier on tap: X taps open the Dossier, a hold switches equipment") {
    for (const Controller controller : kControllers) {
        for (const Handedness handedness : kAllHandedness) {
            CAPTURE(static_cast<int>(controller));
            CAPTURE(static_cast<int>(handedness));
            BindingProfile profile = builtIn(controller, handedness);
            CHECK(applyDossierPress(profile, DossierPress::Tap) == 1);
            const Presses p = pressesOf(profile, dossierHand(handedness));
            CHECK(p.tapOpensDossier);
            CHECK_FALSE(p.tapSwitches);
            CHECK(p.holdSwitches);
            CHECK_FALSE(p.holdOpensDossier);
        }
    }
}

TEST_CASE("a remapped X is left alone by the Dossier swap") {
    BindingProfile profile = builtIn(Controller::OculusTouch, Handedness::Right);
    for (auto& binding : profile.buttons) {
        if (binding.action == GameAction::Dossier) {
            binding.action = GameAction::MissionInfo;
        }
    }
    const BindingProfile before = profile;
    CHECK(applyDossierPress(profile, DossierPress::Tap) == 0);
    CHECK(profile.buttons == before.buttons);
}

TEST_CASE("the Dossier press names are the setting's values") {
    CHECK(std::string(evr::input::dossierPressName(DossierPress::Hold)) == "hold");
    CHECK(std::string(evr::input::dossierPressName(DossierPress::Tap)) == "tap");
}
