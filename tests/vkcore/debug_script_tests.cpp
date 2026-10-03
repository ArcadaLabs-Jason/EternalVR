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
    // A single-player map passes the policy but is not a test command.
    const auto map = parseDebugScript("1:map game/sp/e1m1_intro/e1m1_intro");
    CHECK(map.error.empty());
    CHECK(map.steps.empty());
    CHECK(map.refused.size() == 1);
}

TEST_CASE("debug script: empty steps are dropped and the time must be finite") {
    CHECK(parseDebugScript("1:ai_Show||2:god|").steps.size() == 2);
    CHECK_FALSE(parseDebugScript("inf:ai_Show").error.empty());
}

TEST_CASE("debug script: the QA suite's token schedule passes the allow-list whole") {
    const auto script =
        parseDebugScript("3:god|3.5:sync_printInteractionAndAnimationName 1|4:g_debugTriggers 1|"
                         "5:setviewpos -34.98 -250.31 33.2 45|6:where|7:selectDebugEntity");
    CHECK(script.error.empty());
    CHECK(script.refused.empty());
    REQUIRE(script.steps.size() == 6);
    CHECK(script.steps[1].commands[0] == "sync_printInteractionAndAnimationName 1");
    CHECK(script.steps[3].commands[0] == "setviewpos -34.98 -250.31 33.2 45");
}

TEST_CASE("debug script: command names are matched without case, cvars take a value") {
    const auto script = parseDebugScript("1:GOD;SetViewPos 1 2 3 4|2:r_sharpening 2.5;give armor/sp_armor_5");
    CHECK(script.refused.empty());
    REQUIRE(script.steps.size() == 2);
    CHECK(script.steps[0].commands.size() == 2);
    CHECK(script.steps[1].commands.size() == 2);
}

TEST_CASE("debug script: commands off the allow-list are left out and listed; the rest runs") {
    const auto script =
        parseDebugScript("1:god;bind F5 connect 10.0.0.1|2:alias x god|3:exec evil.cfg|4:where");
    CHECK(script.error.empty());
    REQUIRE(script.refused.size() == 3);
    CHECK(script.refused[0] == "step 1: bind F5 connect 10.0.0.1 (not a test command)");
    CHECK(script.refused[1] == "step 2: alias x god (not a test command)");
    CHECK(script.refused[2] == "step 3: exec evil.cfg (not a test command)");
    REQUIRE(script.steps.size() == 2);
    CHECK(script.steps[0].commands.size() == 1);
    CHECK(script.steps[0].commands[0] == "god");
    CHECK(script.steps[1].commands[0] == "where");
}

TEST_CASE("debug script: quotes, newlines and other characters cannot carry a second command") {
    const auto quoted = parseDebugScript("1:give \"health\"");
    CHECK(quoted.steps.empty());
    REQUIRE(quoted.refused.size() == 1);
    CHECK(quoted.refused[0] ==
          "step 1: give \"health\" (a character other than letters, digits, spaces and _ - . /)");
    CHECK(parseDebugScript("1:god\nexec evil.cfg").refused.size() == 1);
    CHECK(parseDebugScript("1:god\rwhere").refused.size() == 1);
    CHECK(parseDebugScript("1:+god").refused.size() == 1);
    CHECK(parseDebugScript("1:god$x").refused.size() == 1);
    CHECK(parseDebugScript("1:godmode").refused.size() == 1);
    CHECK(parseDebugScript("1:g").refused.size() == 1);
}

TEST_CASE("debug script: a step left out whole still counts for the order of times") {
    CHECK(parseDebugScript("10:bind a b|5:god").error ==
          "step 2: the time is earlier than the step before it");
    const auto script = parseDebugScript("10:bind a b|12:god");
    CHECK(script.error.empty());
    REQUIRE(script.steps.size() == 1);
    CHECK(script.steps[0].seconds == 12.0);
}
