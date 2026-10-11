#include "features/input/binding_compiler.hpp"
#include "features/input/button_labels.hpp"
#include "features/input/controller_bindings.hpp"
#include "features/input/input_frames.hpp"
#include "features/input/input_mapper.hpp"
#include "features/menu/menu_router.hpp"
#include "game/eternal/controller_data.hpp"

#include <doctest/doctest.h>

#include <ostream>
#include <string>

// A tutorial popup's "[K_SPACE] TO DISMISS" names the button bound to jump (prompt_hooks.cpp, kLayerKeys:
// SPACE is jump's label). In every built-in control map (data/input/controllers/*.toml) that button, pressed
// in a popup, sends one Space through the real mapper and the router, whatever button it is: A on Touch, the
// left trackpad click on the Vive wand.

using evr::game::Controller;
using evr::game::controllerName;
using evr::game::GameAction;
using evr::game::kControllers;
using evr::input::BindingProfile;
using evr::input::buildBindingProfile;
using evr::input::ControllerData;
using evr::input::HandState;
using evr::input::InputFrame;
using evr::input::InputMapper;
using evr::input::LabelInput;
using evr::input::MapperContext;
using evr::input::parseControllerData;
using evr::input::PromptControl;
using evr::input::promptControls;
using evr::input::PromptPress;
using evr::menu::CursorPixel;
using evr::menu::kKeyContinue;
using evr::menu::MenuRouter;
using evr::menu::RouterEvent;
using evr::menu::RouterHand;
using evr::menu::RouterInput;

namespace {

constexpr double kFrame = 1.0 / 90.0;

// Presses `control` on `frame`; false for a control the router cannot see pressed (a stick gesture).
bool press(InputFrame& frame, const PromptControl& control) {
    HandState& hand = control.hand == evr::input::Hand::Left ? frame.left : frame.right;
    switch (control.input) {
    case LabelInput::Trigger:
        hand.trigger = 1.0f;
        return true;
    case LabelInput::Grip:
        hand.grip = 1.0f;
        return true;
    case LabelInput::StickClick:
        hand.stickClick = true;
        return true;
    case LabelInput::Primary:
        hand.primaryButton = true;
        return true;
    case LabelInput::Secondary:
        hand.secondaryButton = true;
        return true;
    case LabelInput::Face3:
        hand.face3Button = true;
        return true;
    case LabelInput::Face4:
        hand.face4Button = true;
        return true;
    case LabelInput::Shoulder:
        hand.shoulderButton = true;
        return true;
    case LabelInput::Menu:
        hand.menuButton = true;
        return true;
    case LabelInput::Stick:
    case LabelInput::Count:
        break;
    }
    return false;
}

// What the presenter hands the router for one hand (presenter_menu.cpp, routerHand).
RouterHand routerHand(const HandState& h) {
    RouterHand r;
    r.trigger = h.trigger;
    r.grip = h.grip;
    r.stick = h.stick;
    r.primary = h.primaryButton;
    r.secondary = h.secondaryButton;
    r.stickClick = h.stickClick;
    return r;
}

// The Spaces one press of the jump prompt's button sends in a popup: the mapper under the menu's hold
// publishes the actions (controllers::heldActions), the router presses the keys.
int spacesForJumpPrompt(const BindingProfile& profile, const PromptControl& control) {
    InputMapper mapper(profile);
    MenuRouter router(profile.weaponHand);
    MapperContext context;
    context.menuHold = true;
    int spaces = 0;
    for (int i = 0; i < 90; ++i) {
        const double t = 1.0 + i * kFrame;
        InputFrame frame = evr::test::restingFrame();
        // Held 0.6 s: past the hold time of a hold binding.
        if (i >= 9 && i < 63) {
            REQUIRE(press(frame, control));
        }
        RouterInput in;
        in.seconds = t;
        in.menuActive = true;
        in.popup = true;
        in.width = 1000;
        in.height = 500;
        in.gameCursor = CursorPixel{500, 250};
        in.actions = mapper.update(frame, context, static_cast<float>(kFrame)).down;
        in.hands[0] = routerHand(frame.left);
        in.hands[1] = routerHand(frame.right);
        for (const RouterEvent& e : router.update(in).events) {
            spaces += e.kind == RouterEvent::Kind::KeyDown && e.key == kKeyContinue ? 1 : 0;
        }
    }
    return spaces;
}

} // namespace

TEST_CASE("router: popup: the button a Space prompt names sends one Space, in every built-in map") {
    int checked = 0;
    for (const Controller controller : kControllers) {
        const ControllerData data = parseControllerData(evr::game::builtinControllerData(controller));
        REQUIRE(data.ok());
        for (const auto& [handedness, entries] : data.maps) {
            CAPTURE(std::string(controllerName(controller)));
            CAPTURE(std::string(evr::input::handednessName(handedness)));
            const BindingProfile profile = buildBindingProfile(entries).profile;
            const auto controls = promptControls(GameAction::Jump, profile);
            REQUIRE_FALSE(controls.empty());
            // The prompt names the first control bound to jump (input::actionPromptText).
            const PromptControl& named = controls.front();
            REQUIRE(named.press != PromptPress::StickUp);
            REQUIRE(named.press != PromptPress::StickDown);
            REQUIRE(named.press != PromptPress::StickDownHold);
            CHECK(spacesForJumpPrompt(profile, named) == 1);
            ++checked;
        }
    }
    CHECK(checked == 24); // 8 families, 3 maps each
}
