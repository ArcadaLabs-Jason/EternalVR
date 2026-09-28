#include "features/roomscale/eye_separation.hpp"
#include "features/roomscale/roomscale_settings.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <map>
#include <ostream>
#include <string>

using evr::Vec3;
using namespace evr::roomscale;
namespace posture = evr::posture;

namespace {

RoomScaleSettingsResult parse(const std::map<std::string, std::string>& env) {
    return parseRoomScaleSettings([&env](std::string_view name) -> std::optional<std::string> {
        const auto it = env.find(std::string(name));
        if (it == env.end()) {
            return std::nullopt;
        }
        return it->second;
    });
}

} // namespace

TEST_CASE("defaults: auto posture, Slayer height, anchor, 2 s recenter, 0.6 m lean") {
    const auto r = parse({});
    CHECK(r.issues.empty());
    CHECK(r.settings.posture == posture::PostureOverride::Auto);
    CHECK(r.settings.height == HeightMode::Slayer);
    CHECK(r.settings.autoAnchor);
    CHECK(r.settings.recenterHoldSeconds == doctest::Approx(2.0f));
    CHECK(r.settings.limits.leanCapMetres == doctest::Approx(0.6f));
    CHECK(r.settings.collision);
    CHECK(r.settings.fade);
    CHECK(r.settings.ipdMetres == 0.0f);
    CHECK_FALSE(r.settings.testOffset);
}

TEST_CASE("every setting is read") {
    const auto r = parse({{"ETERNALVR_POSTURE", " Seated "},
                          {"ETERNALVR_HEIGHT", "real"},
                          {"ETERNALVR_AUTO_ANCHOR", "0"},
                          {"ETERNALVR_RECENTER_HOLD", "0"},
                          {"ETERNALVR_LEAN_CAP", "0.45"},
                          {"ETERNALVR_HEAD_COLLISION", "off"},
                          {"ETERNALVR_HEAD_FADE", "false"},
                          {"ETERNALVR_IPD", "63.5"},
                          {"ETERNALVR_TEST_HEAD_OFFSET", "0.8, 0, -0.1, 4"},
                          {"ETERNALVR_TEST_RECENTER", "20"}});
    CHECK(r.issues.empty());
    CHECK(r.settings.posture == posture::PostureOverride::Seated);
    CHECK(r.settings.height == HeightMode::Real);
    CHECK_FALSE(r.settings.autoAnchor);
    CHECK(r.settings.recenterHoldSeconds == 0.0f);
    CHECK(r.settings.limits.leanCapMetres == doctest::Approx(0.45f));
    CHECK_FALSE(r.settings.collision);
    CHECK_FALSE(r.settings.fade);
    CHECK(r.settings.ipdMetres == doctest::Approx(0.0635f));
    REQUIRE(r.settings.testOffset);
    CHECK(r.settings.testOffset->metres.x == doctest::Approx(0.8f));
    CHECK(r.settings.testOffset->metres.z == doctest::Approx(-0.1f));
    CHECK(r.settings.testOffset->periodSeconds == doctest::Approx(4.0f));
    CHECK(r.settings.testRecenterSeconds == doctest::Approx(20.0f));
}

TEST_CASE("bad values are reported and the defaults kept") {
    const auto r = parse({{"ETERNALVR_POSTURE", "lying"},
                          {"ETERNALVR_HEIGHT", "tall"},
                          {"ETERNALVR_RECENTER_HOLD", "0.1"},
                          {"ETERNALVR_LEAN_CAP", "5"},
                          {"ETERNALVR_IPD", "120"},
                          {"ETERNALVR_TEST_HEAD_OFFSET", "1,2"},
                          {"ETERNALVR_HEAD_FADE", "maybe"}});
    CHECK(r.issues.size() == 7);
    CHECK(r.settings.posture == posture::PostureOverride::Auto);
    CHECK(r.settings.height == HeightMode::Slayer);
    CHECK(r.settings.recenterHoldSeconds == doctest::Approx(2.0f));
    CHECK(r.settings.limits.leanCapMetres == doctest::Approx(0.6f));
    CHECK(r.settings.ipdMetres == 0.0f);
    CHECK_FALSE(r.settings.testOffset);
    CHECK(r.settings.fade);
}

TEST_CASE("body follow: on by default, its tunables and the test steps") {
    const auto defaults = parse({});
    CHECK(defaults.settings.follow.enabled);
    CHECK(defaults.settings.follow.deadzoneMetres == doctest::Approx(0.04f));
    CHECK(defaults.settings.follow.walkCommand == 85);
    CHECK(defaults.settings.follow.creepCommand == 60);
    CHECK(defaults.settings.follow.coastSeconds == doctest::Approx(0.1f));
    CHECK(defaults.settings.follow.maxSpeed == doctest::Approx(3.0f));
    CHECK_FALSE(defaults.settings.testSteps);

    const auto r = parse({{"ETERNALVR_BODY_FOLLOW", "1"},
                          {"ETERNALVR_BODY_FOLLOW_DEADZONE", "0.08"},
                          {"ETERNALVR_BODY_FOLLOW_WALK", "90"},
                          {"ETERNALVR_BODY_FOLLOW_CREEP", "50"},
                          {"ETERNALVR_BODY_FOLLOW_COAST", "0.05"},
                          {"ETERNALVR_BODY_FOLLOW_SPEED", "1.5"},
                          {"ETERNALVR_TEST_STEPS", "0.02, 0.05,0.1,-0.2"},
                          {"ETERNALVR_TEST_STEP_SECONDS", "4"},
                          {"ETERNALVR_TEST_STEP_AXIS", "Right"}});
    CHECK(r.issues.empty());
    CHECK(r.settings.follow.enabled);
    CHECK(r.settings.follow.deadzoneMetres == doctest::Approx(0.08f));
    CHECK(r.settings.follow.walkCommand == 90);
    CHECK(r.settings.follow.creepCommand == 50);
    CHECK(r.settings.follow.coastSeconds == doctest::Approx(0.05f));
    CHECK(r.settings.follow.maxSpeed == doctest::Approx(1.5f));
    REQUIRE(r.settings.testSteps);
    REQUIRE(r.settings.testSteps->metres.size() == 4);
    CHECK(r.settings.testSteps->metres[3] == doctest::Approx(-0.2f));
    CHECK(r.settings.testSteps->holdSeconds == doctest::Approx(4.0f));
    CHECK(r.settings.testSteps->sideways);

    CHECK_FALSE(parse({{"ETERNALVR_BODY_FOLLOW", "0"}}).settings.follow.enabled);
    const auto bad = parse({{"ETERNALVR_BODY_FOLLOW", "sometimes"},
                            {"ETERNALVR_BODY_FOLLOW_DEADZONE", "0"},
                            {"ETERNALVR_BODY_FOLLOW_SPEED", "12"},
                            {"ETERNALVR_TEST_STEPS", "0.05,2"},
                            {"ETERNALVR_TEST_STEP_AXIS", "up"}});
    CHECK(bad.issues.size() == 4); // the axis is read only with steps
    CHECK(bad.settings.follow.enabled);
    CHECK(bad.settings.follow.deadzoneMetres == doctest::Approx(0.04f));
    CHECK(bad.settings.follow.maxSpeed == doctest::Approx(3.0f));
    CHECK_FALSE(bad.settings.testSteps);

    // The command test: signed whole move values, the hold and axis shared with the steps.
    const auto moves = parse({{"ETERNALVR_TEST_MOVE", "40,-40,127"}, {"ETERNALVR_TEST_STEP_SECONDS", "2"}});
    CHECK(moves.issues.empty());
    REQUIRE(moves.settings.testSteps);
    CHECK(moves.settings.testSteps->commands);
    CHECK(moves.settings.testSteps->metres[1] == doctest::Approx(-40.0f));
    CHECK(moves.settings.testSteps->holdSeconds == doctest::Approx(2.0f));
    CHECK(parse({{"ETERNALVR_TEST_MOVE", "40.5"}}).issues.size() == 1);
    CHECK(parse({{"ETERNALVR_TEST_MOVE", "200"}}).issues.size() == 1);
}

TEST_CASE("the test offset is constant or eases in and out") {
    const TestHeadOffset constant{{0.8f, 0.0f, 0.0f}, 0.0f};
    CHECK(testOffsetAt(constant, 12.3).x == doctest::Approx(0.8f));
    const TestHeadOffset eased{{0.8f, 0.0f, 0.0f}, 4.0f};
    CHECK(testOffsetAt(eased, 0.0).x == doctest::Approx(0.0f));
    CHECK(testOffsetAt(eased, 2.0).x == doctest::Approx(0.8f));
    CHECK(std::fabs(testOffsetAt(eased, 4.0).x) < 1e-4f);
}

TEST_CASE("names") {
    CHECK(std::string(postureOverrideName(posture::PostureOverride::Standing)) == "standing");
    CHECK(std::string(postureName(posture::Posture::Unknown)) == "unknown");
    CHECK(std::string(heightModeName(HeightMode::Real)) == "real");
}

TEST_CASE("the eye separation override keeps the midpoint") {
    const std::array<Vec3, 2> eyes{Vec3{-0.032f, 0.0f, 0.001f}, Vec3{0.032f, 0.0f, 0.001f}};
    const auto wide = withSeparation(eyes, 0.070f);
    CHECK(wide[0].x == doctest::Approx(-0.035f));
    CHECK(wide[1].x == doctest::Approx(0.035f));
    CHECK(wide[0].z == doctest::Approx(0.001f));
    // Out of range or unset: the runtime's eyes.
    CHECK(withSeparation(eyes, 0.0f)[1].x == doctest::Approx(0.032f));
    CHECK(withSeparation(eyes, 0.2f)[1].x == doctest::Approx(0.032f));
}
