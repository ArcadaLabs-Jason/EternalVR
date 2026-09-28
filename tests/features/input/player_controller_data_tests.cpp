#include "features/input/player_controller_data.hpp"

#include "game/eternal/controller_data.hpp"

#include <doctest/doctest.h>

#include <array>
#include <ostream>
#include <string>
#include <vector>

using evr::game::builtinControllerData;
using evr::game::Controller;
using evr::game::kControllers;
using evr::input::ControllerData;
using evr::input::controllerDataFileOrder;
using evr::input::controlMapIssues;
using evr::input::joinFolderPath;
using evr::input::parseControllerData;
using evr::input::placePlayerData;

namespace {

ControllerData named(const std::string& profile) {
    ControllerData data;
    data.profilePath = profile;
    return data;
}

constexpr const char* kTouch = "/interaction_profiles/oculus/touch_controller";
constexpr const char* kIndex = "/interaction_profiles/valve/index_controller";

} // namespace

TEST_CASE("a folder's .toml files are read in name order, ignoring case; other files are left out") {
    const auto order = controllerDataFileOrder({"valve_index.toml", "README.txt", "B.TOML",
                                                "oculus_touch.toml", "notes.toml.bak", ".toml", "a.toml"});
    CHECK(order == std::vector<std::string>{"a.toml", "B.TOML", "oculus_touch.toml", "valve_index.toml"});
}

TEST_CASE("an empty folder, or one without .toml files, has nothing to read") {
    CHECK(controllerDataFileOrder({}).empty());
    CHECK(controllerDataFileOrder({"README.txt", "touch.toml.txt"}).empty());
}

TEST_CASE("names equal but for case keep a fixed order") {
    CHECK(controllerDataFileOrder({"touch.toml", "Touch.toml"}) ==
          std::vector<std::string>{"Touch.toml", "touch.toml"});
}

TEST_CASE("a file name is joined to its folder with one separator") {
    CHECK(joinFolderPath(R"(C:\controls)", "touch.toml") == R"(C:\controls\touch.toml)");
    CHECK(joinFolderPath(R"(C:\controls\)", "touch.toml") == R"(C:\controls\touch.toml)");
    CHECK(joinFolderPath("C:/controls/", "touch.toml") == "C:/controls/touch.toml");
}

TEST_CASE("a player's file replaces the built-in data of the profile it names") {
    std::array data{named(kTouch), named(kIndex)};
    std::vector<std::string> sources;
    ControllerData player = named(kIndex);
    player.issues.push_back({}); // a marker that this is the player's data
    const auto placement = placePlayerData(data, sources, "mine.toml", player);
    CHECK(placement.placed);
    CHECK(placement.replaced.empty());
    CHECK(data[0].issues.empty());
    CHECK(data[1].issues.size() == 1);
    CHECK(sources == std::vector<std::string>{"", "mine.toml"});
}

TEST_CASE("a file naming no known profile changes nothing") {
    std::array data{named(kTouch), named(kIndex)};
    std::vector<std::string> sources;
    const auto placement = placePlayerData(data, sources, "other.toml", named("/interaction_profiles/x/y"));
    CHECK_FALSE(placement.placed);
    CHECK(placement.replaced.empty());
    CHECK(data[0].profilePath == kTouch);
    CHECK(data[1].profilePath == kIndex);
    CHECK(sources == std::vector<std::string>{"", ""});
}

TEST_CASE("two files naming one profile: the later one wins and the earlier is named") {
    std::array data{named(kTouch), named(kIndex)};
    std::vector<std::string> sources;
    placePlayerData(data, sources, "a.toml", named(kTouch));
    ControllerData later = named(kTouch);
    later.issues.push_back({});
    const auto placement = placePlayerData(data, sources, "b.toml", later);
    CHECK(placement.placed);
    CHECK(placement.replaced == "a.toml");
    CHECK(data[0].issues.size() == 1);
    CHECK(sources == std::vector<std::string>{"b.toml", ""});
}

TEST_CASE("the built-in control maps compile without issues") {
    for (const Controller family : kControllers) {
        CHECK(controlMapIssues(parseControllerData(builtinControllerData(family))).empty());
    }
}

TEST_CASE("a control map that does not compile is reported with its section") {
    std::string text(builtinControllerData(Controller::OculusTouch));
    const std::string jump = R"("right.primary.press" = "jump")";
    const auto at = text.find(jump);
    REQUIRE(at != std::string::npos);
    text.replace(at, jump.size(), R"("right.primary.press" = "jumpp")");
    const auto issues = controlMapIssues(parseControllerData(text));
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].message.rfind("[map.right] ", 0) == 0);
    CHECK(issues[0].message.find("jumpp") != std::string::npos);
}
