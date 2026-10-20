#include "features/input/usercmd_hook_use.hpp"

#include "features/input/controller_settings.hpp"

#include <doctest/doctest.h>

#include <optional>
#include <ostream>
#include <string>
#include <string_view>

using evr::input::CommandHookUse;
using evr::input::commandHookUse;
using evr::input::InputPath;
using evr::input::parseControllerSettings;

namespace {

constexpr InputPath kPaths[] = {InputPath::Auto, InputPath::UserCmd, InputPath::XInput};

} // namespace

TEST_CASE("with the controllers on the command path, the hook carries the controllers and body follow") {
    for (const InputPath path : {InputPath::Auto, InputPath::UserCmd}) {
        CHECK(commandHookUse(true, path, true) == CommandHookUse::Controllers);
        CHECK(commandHookUse(true, path, false) == CommandHookUse::Controllers);
    }
}

TEST_CASE("gamepad mode: with the controllers off the hook carries body follow alone") {
    // A player's gamepad-mode session: motion controllers off, body follow on (its default).
    for (const InputPath path : kPaths) {
        CHECK(commandHookUse(false, path, true) == CommandHookUse::BodyFollow);
    }
}

TEST_CASE("under the virtual gamepad the hook carries body follow alone") {
    CHECK(commandHookUse(true, InputPath::XInput, true) == CommandHookUse::BodyFollow);
}

TEST_CASE("with body follow off nothing outside the controllers' command path installs the hook") {
    for (const InputPath path : kPaths) {
        CHECK(commandHookUse(false, path, false) == CommandHookUse::None);
    }
    CHECK(commandHookUse(true, InputPath::XInput, false) == CommandHookUse::None);
}

TEST_CASE("the launcher's gamepad mode (ETERNALVR_CONTROLLERS=0) gets the hook for body follow") {
    const auto settings = parseControllerSettings([](std::string_view name) -> std::optional<std::string> {
        if (name == "ETERNALVR_CONTROLLERS") {
            return std::string("0");
        }
        return std::nullopt;
    });
    REQUIRE_FALSE(settings.settings.enabled);
    CHECK(commandHookUse(settings.settings.enabled, settings.settings.path, true) ==
          CommandHookUse::BodyFollow);
}
