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
using evr::input::hasVetoTouchBindings;
using evr::input::isRestTouchAction;
using evr::input::parseControllerData;
using evr::input::RestTouchOptions;
using evr::input::withoutRestTouch;
using evr::input::withoutVetoTouch;
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
        // With the wheel on, Touch gets the sensors that tell the thumb is elsewhere (below); nothing else.
        ControllerData added = data;
        CHECK(addRestTouchBindings(added, {}) == (touch ? 6U : 0U));
    }
    CHECK(pathOf(builtin(Controller::OculusTouch), XrActionId::ThumbRest, Hand::Left) ==
          "/user/hand/left/input/thumbrest/touch");
}

TEST_CASE("beside a thumb rest the face buttons' and the stick's touch are bound, and are no rest") {
    ControllerData touch = builtin(Controller::OculusTouch);
    REQUIRE(addRestTouchBindings(touch, {}) == 6);
    CHECK(pathOf(touch, XrActionId::PrimaryTouch, Hand::Left) == "/user/hand/left/input/x/touch");
    CHECK(pathOf(touch, XrActionId::SecondaryTouch, Hand::Left) == "/user/hand/left/input/y/touch");
    CHECK(pathOf(touch, XrActionId::PrimaryTouch, Hand::Right) == "/user/hand/right/input/a/touch");
    CHECK(pathOf(touch, XrActionId::SecondaryTouch, Hand::Right) == "/user/hand/right/input/b/touch");
    CHECK(pathOf(touch, XrActionId::ThumbstickTouch, Hand::Left) == "/user/hand/left/input/thumbstick/touch");
    CHECK(pathOf(touch, XrActionId::ThumbstickTouch, Hand::Right) ==
          "/user/hand/right/input/thumbstick/touch");
    // Face touch asked for changes nothing on Touch: the same six, the thumb rest still the rest.
    ControllerData faceTouch = builtin(Controller::OculusTouch);
    CHECK(addRestTouchBindings(faceTouch, {true, true}) == 6);
    CHECK(faceTouch.suggested == touch.suggested);
    CHECK(addRestTouchBindings(touch, {}) == 0);
    CHECK(isRestTouchAction(XrActionId::ThumbstickTouch));

    // Refused with them: suggested again without any touch binding of the wheel.
    const ControllerData without = withoutRestTouch(touch);
    CHECK_FALSE(hasRestTouchBindings(without));
    CHECK(without.find(XrActionId::ThumbstickTouch, Hand::Right) == nullptr);
    CHECK(without.find(XrActionId::PrimaryTouch, Hand::Left) == nullptr);
    CHECK(without.suggested.size() == builtin(Controller::OculusTouch).suggested.size() - 2);
}

TEST_CASE("a player's Touch file from before the wheel gets its thumb rests back, in the data's order") {
    ControllerData data = withoutLines(Controller::OculusTouch, "thumbrest");
    REQUIRE(data.ok());
    REQUIRE_FALSE(hasRestTouchBindings(data));
    CHECK(addRestTouchBindings(data, {}) == 8);
    CHECK(pathOf(data, XrActionId::ThumbRest, Hand::Right) == "/user/hand/right/input/thumbrest/touch");
    CHECK(handHasRest(data, Hand::Left, false));
    // Ordered by hand, then action, as the parser orders them: as the built-in file with the same additions.
    ControllerData parsed = builtin(Controller::OculusTouch);
    addRestTouchBindings(parsed, {});
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
        // The face buttons' touch on both hands, each stick's touch (the thumb elsewhere), and on Index each
        // trackpad's touch.
        CHECK(addRestTouchBindings(data, {true, true}) == (controller == Controller::ValveIndex ? 8U : 6U));
        CHECK(handHasRest(data, Hand::Right, true));
        CHECK_FALSE(handHasRest(data, Hand::Right, false));
        CHECK(pathOf(data, XrActionId::ThumbstickTouch, Hand::Left) ==
              "/user/hand/left/input/thumbstick/touch");
    }
    ControllerData index = builtin(Controller::ValveIndex);
    addRestTouchBindings(index, {true, true});
    CHECK(pathOf(index, XrActionId::PrimaryTouch, Hand::Left) == "/user/hand/left/input/a/touch");
    CHECK(pathOf(index, XrActionId::SecondaryTouch, Hand::Right) == "/user/hand/right/input/b/touch");
    CHECK(pathOf(index, XrActionId::TrackpadTouch, Hand::Left) == "/user/hand/left/input/trackpad/touch");
    CHECK(pathOf(index, XrActionId::TrackpadTouch, Hand::Right) == "/user/hand/right/input/trackpad/touch");
    ControllerData pico = builtin(Controller::Pico4);
    addRestTouchBindings(pico, {true, true});
    CHECK(pathOf(pico, XrActionId::PrimaryTouch, Hand::Left) == "/user/hand/left/input/x/touch");
    CHECK(pathOf(pico, XrActionId::SecondaryTouch, Hand::Left) == "/user/hand/left/input/y/touch");
    CHECK(pathOf(pico, XrActionId::TrackpadTouch, Hand::Left).empty());
    // Touch has no trackpad, and its face buttons' touch is not the rest there.
    ControllerData touch = builtin(Controller::OculusTouch);
    addRestTouchBindings(touch, {true, true});
    CHECK(touch.find(XrActionId::TrackpadTouch, Hand::Left) == nullptr);

    // The Steam Frame's bumper holds the wheel: no touch binding at all there.
    ControllerData frame = builtin(Controller::SteamFrame);
    CHECK(addRestTouchBindings(frame, {true, true}) == 0);
    CHECK_FALSE(hasRestTouchBindings(frame));
    // Controllers whose face buttons have no touch sensor, or that have a trackpad in their place, get
    // nothing, not even the stick's touch (there is no rest to tell it from).
    for (const Controller controller : {Controller::HpReverbG2, Controller::WindowsMixedReality,
                                        Controller::ViveCosmos, Controller::ViveWand}) {
        CAPTURE(evr::game::controllerName(controller));
        ControllerData data = builtin(controller);
        CHECK(addRestTouchBindings(data, {true, true}) == 0);
    }
}

TEST_CASE(
    "a profile refused with the touch bindings is suggested first without the sensors beside the rest") {
    // Touch: the stick's and the face buttons' touch go, the thumb rests stay.
    ControllerData touch = builtin(Controller::OculusTouch);
    const std::size_t builtinSize = touch.suggested.size();
    addRestTouchBindings(touch, {});
    REQUIRE(hasVetoTouchBindings(touch));
    const ControllerData rests = withoutVetoTouch(touch);
    CHECK_FALSE(hasVetoTouchBindings(rests));
    CHECK(rests.suggested.size() == builtinSize);
    CHECK(handHasRest(rests, Hand::Left, false));
    CHECK(handHasRest(rests, Hand::Right, false));
    CHECK(rests.find(XrActionId::PrimaryTouch, Hand::Right) == nullptr);
    CHECK(rests.find(XrActionId::ThumbstickTouch, Hand::Left) == nullptr);

    // Index with face touch: the face buttons' touch is the rest there and stays; the stick's and the
    // trackpad's touch go.
    ControllerData index = builtin(Controller::ValveIndex);
    addRestTouchBindings(index, {true, true});
    const ControllerData faceRests = withoutVetoTouch(index);
    CHECK(handHasRest(faceRests, Hand::Left, true));
    CHECK(faceRests.find(XrActionId::PrimaryTouch, Hand::Left) != nullptr);
    CHECK(faceRests.find(XrActionId::TrackpadTouch, Hand::Left) == nullptr);
    CHECK(faceRests.find(XrActionId::ThumbstickTouch, Hand::Right) == nullptr);
    CHECK(faceRests.suggested.size() == index.suggested.size() - 4);
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
