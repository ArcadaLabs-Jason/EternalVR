// The grip's curl for the fingers (features/input/rest_touch_bindings.hpp, addGripCurlBindings; DECISIONS
// T-226).

#include "features/input/controller_bindings.hpp"
#include "features/input/rest_touch_bindings.hpp"
#include "game/eternal/controller_data.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <ostream>
#include <string>

using evr::game::builtinControllerData;
using evr::game::Controller;
using evr::game::kControllers;
using evr::input::addGripCurlBindings;
using evr::input::addRestTouchBindings;
using evr::input::addTriggerTouchBindings;
using evr::input::ControllerData;
using evr::input::Hand;
using evr::input::parseControllerData;
using evr::input::touchRetries;
using evr::input::TouchRetry;
using evr::input::withoutFingerBindings;
using evr::input::XrActionId;

namespace {

ControllerData builtin(Controller controller) {
    return parseControllerData(builtinControllerData(controller));
}

std::string pathOf(const ControllerData& data, XrActionId action, Hand hand) {
    const auto* binding = data.find(action, hand);
    return binding ? binding->path : std::string{};
}

// The Index's data with its grip bound to `leaf` on both hands instead of the force (a player's own file).
ControllerData indexWithGrip(const std::string& leaf) {
    std::string text(builtinControllerData(Controller::ValveIndex));
    const std::string force = "\"/input/squeeze/force\"";
    for (std::size_t at = text.find(force); at != std::string::npos; at = text.find(force, at + 1)) {
        text.replace(at, force.size(), "\"" + leaf + "\"");
    }
    ControllerData data = parseControllerData(text);
    REQUIRE(data.ok());
    return data;
}

} // namespace

TEST_CASE("the Index's fingers get the squeeze's value on both hands; its grip still reads the force") {
    ControllerData index = builtin(Controller::ValveIndex);
    CHECK(pathOf(index, XrActionId::GripCurl, Hand::Left).empty()); // the built-in data suggests none
    const std::size_t before = index.suggested.size();
    CHECK(addGripCurlBindings(index) == 2);
    CHECK(index.suggested.size() == before + 2);
    CHECK(pathOf(index, XrActionId::GripCurl, Hand::Left) == "/user/hand/left/input/squeeze/value");
    CHECK(pathOf(index, XrActionId::GripCurl, Hand::Right) == "/user/hand/right/input/squeeze/value");
    // The Flame Belch and the weapon mod keep their deliberate squeeze.
    CHECK(pathOf(index, XrActionId::Grip, Hand::Left) == "/user/hand/left/input/squeeze/force");
    CHECK(pathOf(index, XrActionId::Grip, Hand::Right) == "/user/hand/right/input/squeeze/force");
    CHECK(addGripCurlBindings(index) == 0); // once is enough
}

TEST_CASE("no other built-in family gets one: their grip is the squeeze's value already, or they have none") {
    for (const Controller controller : kControllers) {
        if (controller == Controller::ValveIndex) {
            continue;
        }
        CAPTURE(evr::game::controllerName(controller));
        ControllerData data = builtin(controller);
        const ControllerData before = data;
        CHECK(addGripCurlBindings(data) == 0);
        CHECK(data.suggested == before.suggested);
    }
}

TEST_CASE("a player's file decides by what its grip reads") {
    // The grip on the value: the fingers read the grip, and one input never drives two actions.
    ControllerData value = indexWithGrip("/input/squeeze/value");
    CHECK(addGripCurlBindings(value) == 0);
    // The grip on the whole squeeze (its value through the identifier): the same.
    ControllerData identifier = indexWithGrip("/input/squeeze");
    CHECK(addGripCurlBindings(identifier) == 0);
    // A Pico 4 file with the left grip on the squeeze's click: the left fingers get the value.
    std::string text(builtinControllerData(Controller::Pico4));
    const std::string grip = "\"gameplay.left.grip\" = \"/input/squeeze/value\"";
    REQUIRE(text.find(grip) != std::string::npos);
    text.replace(text.find(grip), grip.size(), "\"gameplay.left.grip\" = \"/input/squeeze/click\"");
    ControllerData pico = parseControllerData(text);
    REQUIRE(pico.ok());
    CHECK(addGripCurlBindings(pico) == 1);
    CHECK(pathOf(pico, XrActionId::GripCurl, Hand::Left) == "/user/hand/left/input/squeeze/value");
    CHECK(pathOf(pico, XrActionId::GripCurl, Hand::Right).empty());
}

TEST_CASE("a refused Index profile drops the fingers' bindings alone first, then keeps them out") {
    ControllerData index = builtin(Controller::ValveIndex);
    addRestTouchBindings(index, {true, true}); // the face touch as the rest, its stick and trackpad touch
    REQUIRE(addTriggerTouchBindings(index) == 2);
    REQUIRE(addGripCurlBindings(index) == 2);
    const std::size_t all = index.suggested.size();
    const auto steps = touchRetries(index);
    REQUIRE_FALSE(steps.empty());
    CHECK(steps[0].retry == TouchRetry::WithoutFingers);
    CHECK(steps[0].data.suggested.size() == all - 4);
    CHECK(steps[0].data.find(XrActionId::Grip, Hand::Left) != nullptr);
    CHECK(steps[0].data.find(XrActionId::PrimaryTouch, Hand::Left) != nullptr);
    for (const auto& step : steps) {
        CAPTURE(static_cast<int>(step.retry));
        CHECK(step.data.find(XrActionId::GripCurl, Hand::Left) == nullptr);
        CHECK(step.data.find(XrActionId::GripCurl, Hand::Right) == nullptr);
        CHECK(step.data.find(XrActionId::TriggerTouch, Hand::Left) == nullptr);
        CHECK(step.data.find(XrActionId::Grip, Hand::Right) != nullptr);
    }
    // The curl alone: the first retry is still the fingers' step.
    ControllerData curlOnly = builtin(Controller::ValveIndex);
    REQUIRE(addGripCurlBindings(curlOnly) == 2);
    const auto curlSteps = touchRetries(curlOnly);
    REQUIRE(curlSteps.size() == 1);
    CHECK(curlSteps[0].retry == TouchRetry::WithoutFingers);
    CHECK(withoutFingerBindings(curlOnly).suggested == builtin(Controller::ValveIndex).suggested);
}
