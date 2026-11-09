// The trigger's touch for the fingers (features/input/rest_touch_bindings.hpp, addTriggerTouchBindings).

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
using evr::input::addBoundTouchBindings;
using evr::input::addRestTouchBindings;
using evr::input::addTriggerTouchBindings;
using evr::input::ControllerData;
using evr::input::Hand;
using evr::input::parseControllerData;
using evr::input::touchRetries;
using evr::input::TouchRetry;
using evr::input::XrActionId;

namespace {

ControllerData builtin(Controller controller) {
    return parseControllerData(builtinControllerData(controller));
}

std::string pathOf(const ControllerData& data, Hand hand) {
    const auto* binding = data.find(XrActionId::TriggerTouch, hand);
    return binding ? binding->path : std::string{};
}

bool hasSensor(Controller controller) {
    return controller == Controller::OculusTouch || controller == Controller::ValveIndex ||
           controller == Controller::Pico4 || controller == Controller::SteamFrame;
}

} // namespace

TEST_CASE(
    "every family with a touch sensor on its trigger reads it on both hands; the others are unchanged") {
    for (const Controller controller : kControllers) {
        CAPTURE(evr::game::controllerName(controller));
        ControllerData data = builtin(controller);
        // The built-in data suggests none: without the layer's addition the sensor reads nothing.
        CHECK(pathOf(data, Hand::Left).empty());
        CHECK(pathOf(data, Hand::Right).empty());
        const std::size_t before = data.suggested.size();
        const std::size_t added = addTriggerTouchBindings(data);
        if (hasSensor(controller)) {
            CHECK(added == 2);
            CHECK(pathOf(data, Hand::Left) == "/user/hand/left/input/trigger/touch");
            CHECK(pathOf(data, Hand::Right) == "/user/hand/right/input/trigger/touch");
        } else {
            CHECK(added == 0);
        }
        CHECK(data.suggested.size() == before + added);
        // Once is enough: a second call adds nothing.
        CHECK(addTriggerTouchBindings(data) == 0);
    }
}

TEST_CASE("a trigger touch a map binds like a button keeps its one binding") {
    ControllerData data = builtin(Controller::OculusTouch);
    data.maps[evr::game::Handedness::Right]["right.trigger_touch.tap"] = "jump";
    REQUIRE(addBoundTouchBindings(data) == 1);
    CHECK(addTriggerTouchBindings(data) == 1); // the left hand only
    std::size_t right = 0;
    for (const auto& b : data.suggested) {
        right += b.action == XrActionId::TriggerTouch && b.hand == Hand::Right ? 1 : 0;
    }
    CHECK(right == 1);
    CHECK(pathOf(data, Hand::Left) == "/user/hand/left/input/trigger/touch");
}

TEST_CASE("a refused profile drops the trigger's touch alone first: the thumb rests and the rest kept") {
    ControllerData touch = builtin(Controller::OculusTouch);
    addRestTouchBindings(touch, {});
    REQUIRE(addTriggerTouchBindings(touch) == 2);
    const std::size_t all = touch.suggested.size();
    const auto steps = touchRetries(touch);
    REQUIRE(steps.size() == 3);
    // First: the two trigger touch bindings and nothing else; the wheel's rests and the veto sensors kept.
    CHECK(steps[0].retry == TouchRetry::WithoutFingers);
    CHECK(steps[0].data.suggested.size() == all - 2);
    CHECK(steps[0].data.find(XrActionId::ThumbRest, Hand::Left) != nullptr);
    CHECK(steps[0].data.find(XrActionId::ThumbRest, Hand::Right) != nullptr);
    CHECK(evr::input::hasVetoTouchBindings(steps[0].data));
    // Every later step is without it too, and the last without every touch.
    CHECK(steps[1].retry == TouchRetry::WithoutVeto);
    CHECK(steps[2].retry == TouchRetry::WithoutRestTouch);
    for (const auto& step : steps) {
        CAPTURE(static_cast<int>(step.retry));
        CHECK(step.data.find(XrActionId::TriggerTouch, Hand::Left) == nullptr);
        CHECK(step.data.find(XrActionId::TriggerTouch, Hand::Right) == nullptr);
    }
    CHECK(steps[1].data.find(XrActionId::ThumbRest, Hand::Right) != nullptr);
    CHECK(steps[2].data.find(XrActionId::ThumbRest, Hand::Right) == nullptr);
}

TEST_CASE("a trigger touch a map binds like a button stays through the first retry, as a bound touch does") {
    ControllerData data = builtin(Controller::OculusTouch);
    data.maps[evr::game::Handedness::Right]["right.trigger_touch.tap"] = "jump";
    addBoundTouchBindings(data);
    addTriggerTouchBindings(data);
    const auto steps = touchRetries(data);
    REQUIRE_FALSE(steps.empty());
    CHECK(steps[0].retry == TouchRetry::WithoutFingers);
    CHECK(steps[0].data.find(XrActionId::TriggerTouch, Hand::Left) == nullptr);
    CHECK(steps[0].data.find(XrActionId::TriggerTouch, Hand::Right) != nullptr);
    CHECK(evr::input::withoutFingerBindings(data).suggested.size() == data.suggested.size() - 1);
}
