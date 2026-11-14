#include "platform/mp_policy/guard_refusal.hpp"

#include <doctest/doctest.h>

#include <string>

using evr::mp_policy::DetectionPoints;
using evr::mp_policy::guardRefusalReason;

namespace {

constexpr const char* kSupported = "Steam build 25216728 or Game Pass 1.0.56.0";

DetectionPoints allFound() {
    DetectionPoints p;
    p.mapLoad = p.joins = p.invites = p.lobbySession = p.gameSession = p.mainMenu = true;
    return p;
}

} // namespace

TEST_CASE("guard refusal: a build EternalVR does not know names the supported builds") {
    // The stand-in exe of the package QA's mp-guard-standin: every point missing on an unknown build.
    CHECK(guardRefusalReason(DetectionPoints{}, false, false, kSupported) ==
          "this DOOM Eternal version is not one this EternalVR supports (Steam build 25216728 or Game Pass "
          "1.0.56.0); VR stays off until an EternalVR update");
}

TEST_CASE("guard refusal: a build EternalVR does not know is not supported, whatever points were found") {
    // Signatures can still match in another build (a patched exe): the sentence stays the version one.
    DetectionPoints some = allFound();
    some.mainMenu = false; // the menu's screen ids belong to the known builds only
    some.invites = false;
    const std::string reason = guardRefusalReason(some, false, false, kSupported);
    CHECK(reason.rfind("this DOOM Eternal version is not one this EternalVR supports (", 0) == 0);
    CHECK(reason.find("could not find") == std::string::npos);
    CHECK(guardRefusalReason(allFound(), false, true, kSupported) == reason);
}

TEST_CASE("guard refusal: on a known build each missing point is named") {
    DetectionPoints p = allFound();
    CHECK(p.all());
    p.mapLoad = false;
    CHECK_FALSE(p.all());
    CHECK(
        guardRefusalReason(p, true, false, kSupported) ==
        "the mod could not find the map load in the game, which it watches to keep VR out of online play; VR "
        "stays off");

    p.mainMenu = false;
    CHECK(guardRefusalReason(p, true, false, kSupported) ==
          "the mod could not find the map load and the main menu screens in the game, which it watches to "
          "keep VR out of online play; VR stays off");

    p.joins = false;
    p.gameSession = false;
    CHECK(
        guardRefusalReason(p, true, false, kSupported) ==
        "the mod could not find the map load, the Steam joins, the BATTLEMODE game session and the main menu "
        "screens in the game, which it watches to keep VR out of online play; VR stays off");
}

TEST_CASE("guard refusal: the Game Pass join path is named as the Xbox invites") {
    DetectionPoints p = allFound();
    p.joins = false;
    CHECK(guardRefusalReason(p, true, true, kSupported).find("the Xbox invites in the game") !=
          std::string::npos);
    CHECK(guardRefusalReason(p, true, false, kSupported).find("the Steam joins in the game") !=
          std::string::npos);
}

TEST_CASE("guard refusal: every single missing point is named and nothing else") {
    const char* const names[] = {"the map load",
                                 "the Steam joins",
                                 "the accepted invite",
                                 "the BATTLEMODE lobby",
                                 "the BATTLEMODE game session",
                                 "the main menu screens"};
    bool DetectionPoints::*const fields[] = {&DetectionPoints::mapLoad,     &DetectionPoints::joins,
                                             &DetectionPoints::invites,     &DetectionPoints::lobbySession,
                                             &DetectionPoints::gameSession, &DetectionPoints::mainMenu};
    for (int i = 0; i < 6; ++i) {
        DetectionPoints p = allFound();
        p.*fields[i] = false;
        const std::string reason = guardRefusalReason(p, true, false, kSupported);
        CAPTURE(reason);
        CHECK(reason == std::string("the mod could not find ") + names[i] +
                            " in the game, which it watches to keep VR out of online play; VR stays off");
    }
}

TEST_CASE("guard refusal: with every point found the old general sentence is kept") {
    CHECK(guardRefusalReason(allFound(), true, false, kSupported) ==
          "the mod could not find everything it needs in the game; VR stays off");
}
