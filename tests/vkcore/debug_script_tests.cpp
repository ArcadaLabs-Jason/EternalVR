#include "vkcore/debug_script.hpp"

#include <doctest/doctest.h>

using evr::vkcore::parseDebugScript;

TEST_CASE("an empty debug script runs nothing and is not an error") {
    CHECK(parseDebugScript("").steps.empty());
    CHECK(parseDebugScript("  ").error.empty());
}

TEST_CASE("debug script steps keep their times and commands in order") {
    const auto script = parseDebugScript(" 50:notarget | 55.5: nextActiveAI ; ai_teleportToPlayer ");
    REQUIRE(script.error.empty());
    REQUIRE(script.steps.size() == 2);
    CHECK(script.steps[0].seconds == 50.0);
    REQUIRE(script.steps[0].commands.size() == 1);
    CHECK(script.steps[0].commands[0] == "notarget");
    CHECK(script.steps[1].seconds == 55.5);
    REQUIRE(script.steps[1].commands.size() == 2);
    CHECK(script.steps[1].commands[0] == "nextActiveAI");
    CHECK(script.steps[1].commands[1] == "ai_teleportToPlayer");
}

TEST_CASE("a command keeps its arguments and empty commands are dropped") {
    const auto script = parseDebugScript("40:setviewpos 36 -1604 17.5 90;;");
    REQUIRE(script.steps.size() == 1);
    REQUIRE(script.steps[0].commands.size() == 1);
    CHECK(script.steps[0].commands[0] == "setviewpos 36 -1604 17.5 90");
}

TEST_CASE("a malformed debug script is rejected whole with the failing step") {
    CHECK(parseDebugScript("notarget").error == "step 1: no ':' between the time and the commands");
    CHECK(parseDebugScript("10:a|x:b").error ==
          "step 2: the time is not a finite non-negative number of seconds");
    CHECK(parseDebugScript("-1:a").error ==
          "step 1: the time is not a finite non-negative number of seconds");
    CHECK(parseDebugScript("10:a|5:b").error == "step 2: the time is earlier than the step before it");
    CHECK(parseDebugScript("10: ; ").error == "step 1: no command");
    CHECK(parseDebugScript("10:a|5:b").steps.empty());
}

TEST_CASE("debug script: online commands and maps that are not single-player are refused") {
    CHECK(parseDebugScript("1:connect 10.0.0.1").steps.empty());
    CHECK_FALSE(parseDebugScript("1:connect 10.0.0.1").error.empty());
    CHECK_FALSE(parseDebugScript("1:ai_Show|2:JoinShellLobby").error.empty());
    CHECK_FALSE(parseDebugScript("1:map game/pvp/pvp_inferno").error.empty());
    CHECK_FALSE(parseDebugScript("1:devmap").error.empty());
    CHECK(parseDebugScript("1:map game/sp/e1m1_intro/e1m1_intro").error.empty());
}

TEST_CASE("debug script: empty steps are dropped and the time must be finite") {
    CHECK(parseDebugScript("1:ai_Show||2:god|").steps.size() == 2);
    CHECK_FALSE(parseDebugScript("inf:ai_Show").error.empty());
}
