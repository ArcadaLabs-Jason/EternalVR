#include "features/input/rest_touch_bindings.hpp"

#include "features/input/controller_bindings.hpp"
#include "game/eternal/controller_data.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <ostream>
#include <string>

using evr::game::builtinControllerData;
using evr::game::Controller;
using evr::game::kControllers;
using evr::input::addRestTouchBindings;
using evr::input::ControllerData;
using evr::input::Hand;
using evr::input::handHasRest;
using evr::input::hasRestTouchBindings;
using evr::input::parseControllerData;
using evr::input::RestTouchOptions;
using evr::input::withoutRestTouch;
using evr::input::XrActionId;

namespace {

ControllerData builtin(Controller controller) {
    return parseControllerData(builtinControllerData(controller));
}

std::string pathOf(const ControllerData& data, XrActionId action, Hand hand) {
    const auto* binding = data.find(action, hand);
    return binding ? binding->path : std::string{};
}

// A built-in file without the lines that bind `action`, as a file copied before it existed.
ControllerData withoutLines(Controller controller, const std::string& action) {
    std::string text(builtinControllerData(controller));
    for (const char* hand : {"left", "right"}) {
        const std::string key = "\"gameplay." + std::string(hand) + "." + action + "\"";
        const std::size_t at = text.find(key);
        if (at != std::string::npos) {
            text.erase(at, text.find('\n', at) - at + 1);
        }
    }
    return parseControllerData(text);
}

} // namespace

TEST_CASE("the built-in Touch data binds both thumb rests; no other family has one") {
    for (const Controller controller : kControllers) {
        CAPTURE(evr::game::controllerName(controller));
        const ControllerData data = builtin(controller);
        const bool touch = controller == Controller::OculusTouch;
        CHECK(handHasRest(data, Hand::Left, false) == touch);
        CHECK(handHasRest(data, Hand::Right, false) == touch);
        ControllerData added = data;
        CHECK(addRestTouchBindings(added, {}) == 0);
    }
    CHECK(pathOf(builtin(Controller::OculusTouch), XrActionId::ThumbRest, Hand::Left) ==
          "/user/hand/left/input/thumbrest/touch");
}

TEST_CASE("a player's Touch file from before the wheel gets its thumb rests back, in the data's order") {
    ControllerData data = withoutLines(Controller::OculusTouch, "thumbrest");
    REQUIRE(data.ok());
    REQUIRE_FALSE(hasRestTouchBindings(data));
    CHECK(addRestTouchBindings(data, {}) == 2);
    CHECK(pathOf(data, XrActionId::ThumbRest, Hand::Right) == "/user/hand/right/input/thumbrest/touch");
    CHECK(handHasRest(data, Hand::Left, false));
    // Ordered by hand, then action, as the parser orders them.
    const ControllerData parsed = builtin(Controller::OculusTouch);
    REQUIRE(parsed.suggested.size() == data.suggested.size());
    for (std::size_t i = 0; i < data.suggested.size(); ++i) {
        CHECK(data.suggested[i] == parsed.suggested[i]);
    }
    CHECK(addRestTouchBindings(data, {}) == 0);
}

TEST_CASE("nothing is added with the wheel off") {
    ControllerData data = withoutLines(Controller::OculusTouch, "thumbrest");
    CHECK(addRestTouchBindings(data, {false, true}) == 0);
    CHECK_FALSE(hasRestTouchBindings(data));
}

TEST_CASE("face-button touch is added only when asked for, on the controllers without a thumb rest") {
    for (const Controller controller : {Controller::ValveIndex, Controller::Pico4}) {
        CAPTURE(evr::game::controllerName(controller));
        ControllerData data = builtin(controller);
        CHECK(addRestTouchBindings(data, {}) == 0);
        CHECK_FALSE(handHasRest(data, Hand::Right, true));
        CHECK(addRestTouchBindings(data, {true, true}) == 4);
        CHECK(handHasRest(data, Hand::Right, true));
        CHECK_FALSE(handHasRest(data, Hand::Right, false));
    }
    ControllerData index = builtin(Controller::ValveIndex);
    addRestTouchBindings(index, {true, true});
    CHECK(pathOf(index, XrActionId::PrimaryTouch, Hand::Left) == "/user/hand/left/input/a/touch");
    CHECK(pathOf(index, XrActionId::SecondaryTouch, Hand::Right) == "/user/hand/right/input/b/touch");
    ControllerData pico = builtin(Controller::Pico4);
    addRestTouchBindings(pico, {true, true});
    CHECK(pathOf(pico, XrActionId::PrimaryTouch, Hand::Left) == "/user/hand/left/input/x/touch");
    CHECK(pathOf(pico, XrActionId::SecondaryTouch, Hand::Left) == "/user/hand/left/input/y/touch");

    // Touch has the real sensor, and the Steam Frame's bumper holds the wheel: no face touch on either.
    ControllerData touch = builtin(Controller::OculusTouch);
    CHECK(addRestTouchBindings(touch, {true, true}) == 0);
    ControllerData frame = builtin(Controller::SteamFrame);
    CHECK(addRestTouchBindings(frame, {true, true}) == 0);
    // Controllers whose face buttons have no touch sensor, or that have a trackpad in their place, get
    // nothing.
    for (const Controller controller : {Controller::HpReverbG2, Controller::WindowsMixedReality,
                                        Controller::ViveCosmos, Controller::ViveWand}) {
        CAPTURE(evr::game::controllerName(controller));
        ControllerData data = builtin(controller);
        CHECK(addRestTouchBindings(data, {true, true}) == 0);
    }
}

TEST_CASE("a profile refused with the touch bindings is suggested without them") {
    ControllerData index = builtin(Controller::ValveIndex);
    const std::size_t before = index.suggested.size();
    addRestTouchBindings(index, {true, true});
    REQUIRE(hasRestTouchBindings(index));
    const ControllerData without = withoutRestTouch(index);
    CHECK_FALSE(hasRestTouchBindings(without));
    CHECK(without.suggested.size() == before);
    CHECK_FALSE(handHasRest(without, Hand::Left, true));
}
