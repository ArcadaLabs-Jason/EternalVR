#include "features/input/offhand_policy.hpp"

#include "features/input/controller_settings.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

using evr::input::ArmBlend;
using evr::input::ArmReason;
using evr::input::ArmSignals;
using evr::input::decideArm;
using evr::input::HandsAction;
using evr::input::OffhandMode;
namespace hands_flag = evr::input::hands_flag;

namespace {

ArmSignals idle() {
    ArmSignals s;
    s.offHandTracked = true;
    s.modelPlaced = true;
    s.pendingAction = 1; // HANDSACTION_IDLE
    return s;
}

std::int32_t action(HandsAction a) {
    return static_cast<std::int32_t>(a);
}

} // namespace

TEST_CASE("offhand policy: the controller drives an idle left arm in free and probe mode only") {
    CHECK(decideArm(idle(), OffhandMode::Free).controller);
    CHECK(decideArm(idle(), OffhandMode::Probe).controller);
    const auto game = decideArm(idle(), OffhandMode::Game);
    CHECK_FALSE(game.controller);
    CHECK(game.reason == ArmReason::ModeGame);
}

TEST_CASE("offhand policy: every left-arm action of the game takes the arm back") {
    struct Case {
        const char* what;
        ArmSignals signals;
        ArmReason reason;
    };
    const auto with = [](auto change) {
        ArmSignals s = idle();
        change(s);
        return s;
    };
    const Case cases[] = {
        {"glory kill (forced view)", with([](ArmSignals& s) { s.forcedView = true; }), ArmReason::ForcedView},
        {"sync kill", with([](ArmSignals& s) { s.syncActive = true; }), ArmReason::Sync},
        {"ledge grab hides the hands", with([](ArmSignals& s) { s.fpHandsDisabled = 2; }),
         ArmReason::HandsHidden},
        {"hidden reasons", with([](ArmSignals& s) { s.hiddenReasons = 1; }), ArmReason::HandsHidden},
        {"hidden state", with([](ArmSignals& s) { s.destHandsState = 4; }), ArmReason::HandsHidden},
        {"Blood Punch", with([](ArmSignals& s) { s.pendingAction = action(HandsAction::MeleeLeft); }),
         ArmReason::Action},
        {"melee", with([](ArmSignals& s) { s.pendingAction = action(HandsAction::Melee); }),
         ArmReason::Action},
        {"grenade throw", with([](ArmSignals& s) { s.pendingAction = action(HandsAction::ThrowItem); }),
         ArmReason::Action},
        {"weapon switch", with([](ArmSignals& s) { s.pendingAction = action(HandsAction::BringDown); }),
         ArmReason::Action},
        {"custom animation", with([](ArmSignals& s) { s.pendingAction = action(HandsAction::CustomAnim); }),
         ArmReason::Action},
        {"chainsaw", with([](ArmSignals& s) { s.pendingAction = 28; }), ArmReason::Action},
        {"hide animated", with([](ArmSignals& s) { s.pendingAction = 23; }), ArmReason::Action},
        {"throwing flag", with([](ArmSignals& s) { s.handsFlags = hands_flag::kThrowing; }),
         ArmReason::BusyFlags},
        {"melee anim flag", with([](ArmSignals& s) { s.handsFlags = hands_flag::kMeleeAnim; }),
         ArmReason::BusyFlags},
        {"switch flag", with([](ArmSignals& s) { s.handsFlags = hands_flag::kChangingWeapon; }),
         ArmReason::BusyFlags},
        {"custom anim flag", with([](ArmSignals& s) { s.handsFlags = hands_flag::kCustomAnim; }),
         ArmReason::BusyFlags},
        {"chainsaw state", with([](ArmSignals& s) { s.destHandsState = 11; }), ArmReason::HandsState},
        {"untracked", with([](ArmSignals& s) { s.offHandTracked = false; }), ArmReason::Untracked},
        {"arms not at the hand", with([](ArmSignals& s) { s.modelPlaced = false; }), ArmReason::Untracked},
    };
    for (const Case& c : cases) {
        CAPTURE(c.what);
        const auto d = decideArm(c.signals, OffhandMode::Free);
        CHECK_FALSE(d.controller);
        CHECK(d.reason == c.reason);
    }
}

TEST_CASE("offhand policy: firing, jumping and landing leave the arm to the controller") {
    for (const std::int32_t a : {3, 4, 5, 14, 15, 17, 19, 25, 42}) {
        ArmSignals s = idle();
        s.pendingAction = a;
        CAPTURE(a);
        CHECK(decideArm(s, OffhandMode::Free).controller);
    }
    ArmSignals firing = idle();
    firing.handsFlags = (1ull << 10) | (1ull << 13); // fire anim, weapon firing
    CHECK(decideArm(firing, OffhandMode::Free).controller);
}

TEST_CASE("offhand policy: the blend hands over quickly and takes back after a hold") {
    ArmBlend b;
    const float blend = 0.15f;
    const float hold = 0.25f;
    CHECK(b.update(true, 0.0f, blend, hold) == doctest::Approx(0.0f));
    // Ramps in over the blend time, eased.
    CHECK(b.update(true, 0.075f, blend, hold) == doctest::Approx(0.5f));
    CHECK(b.update(true, 0.075f, blend, hold) == doctest::Approx(1.0f));
    // The game wants the arm: gone in a third of the blend.
    CHECK(b.update(false, 0.025f, blend, hold) == doctest::Approx(0.5f));
    CHECK(b.update(false, 0.025f, blend, hold) == doctest::Approx(0.0f));
    // Free again: nothing during the hold, then the ramp.
    CHECK(b.update(true, 0.2f, blend, hold) == doctest::Approx(0.0f));
    CHECK(b.update(true, 0.1f, blend, hold) > 0.0f);
    b.reset();
    CHECK(b.weight() == doctest::Approx(0.0f));
    CHECK(b.update(true, 1.0f, 0.0f, 0.0f) == doctest::Approx(1.0f));
}

TEST_CASE("offhand settings: parsed with the controller settings") {
    std::map<std::string, std::string> env{{"ETERNALVR_OFFHAND", "free"},
                                           {"ETERNALVR_OFFHAND_OFFSET", "0.01,0.02,-0.03,0,90,0"},
                                           {"ETERNALVR_OFFHAND_PROBE", "0,0.2,0"},
                                           {"ETERNALVR_OFFHAND_BLEND", "0.3"},
                                           {"ETERNALVR_OFFHAND_TRACE", "1"}};
    const auto lookup = [&env](std::string_view name) -> std::optional<std::string> {
        const auto it = env.find(std::string(name));
        return it == env.end() ? std::nullopt : std::optional<std::string>(it->second);
    };
    auto r = evr::input::parseControllerSettings(lookup);
    CHECK(r.issues.empty());
    CHECK(r.settings.offhand == OffhandMode::Free);
    CHECK(r.settings.offhandOffset.forward == doctest::Approx(0.01f));
    CHECK(r.settings.offhandOffset.yaw == doctest::Approx(90.0f));
    CHECK(r.settings.offhandProbe.left == doctest::Approx(0.2f));
    CHECK(r.settings.offhandBlendSeconds == doctest::Approx(0.3f));
    CHECK(r.settings.offhandTrace);

    env = {
        {"ETERNALVR_OFFHAND", "both"}, {"ETERNALVR_OFFHAND_OFFSET", "x"}, {"ETERNALVR_OFFHAND_BLEND", "5"}};
    r = evr::input::parseControllerSettings(lookup);
    CHECK(r.issues.size() == 3);
    CHECK(r.settings.offhand == OffhandMode::Game);
    CHECK(r.settings.offhandOffset == evr::input::kDefaultOffhandOffset);
    CHECK(r.settings.offhandBlendSeconds == doctest::Approx(0.15f));
    CHECK(std::string(evr::input::offhandModeName(OffhandMode::Probe)) == "probe");
}

TEST_CASE("offhand settings: the arm's wrist offset, shoulder and elbow") {
    using evr::input::ShoulderAnchor;
    std::map<std::string, std::string> env;
    const auto lookup = [&env](std::string_view name) -> std::optional<std::string> {
        const auto it = env.find(std::string(name));
        return it == env.end() ? std::nullopt : std::optional<std::string>(it->second);
    };
    auto r = evr::input::parseControllerSettings(lookup);
    CHECK(r.settings.offhandOffset.forward == doctest::Approx(-0.08f));
    CHECK(r.settings.offhandOffset.left == doctest::Approx(0.035f));
    CHECK(r.settings.offhandShoulder == ShoulderAnchor::Head);
    CHECK(r.settings.offhandShoulderOffset == evr::input::kDefaultOffhandShoulder);
    CHECK(r.settings.offhandElbow == evr::input::kDefaultOffhandElbow);

    env = {{"ETERNALVR_OFFHAND_SHOULDER", "Model"}, {"ETERNALVR_OFFHAND_ELBOW", "0,1,-1"}};
    r = evr::input::parseControllerSettings(lookup);
    CHECK(r.issues.empty());
    CHECK(r.settings.offhandShoulder == ShoulderAnchor::Model);
    CHECK(r.settings.offhandElbow.left == doctest::Approx(1.0f));
    CHECK(r.settings.offhandElbow.up == doctest::Approx(-1.0f));
    CHECK(std::string(evr::input::shoulderAnchorName(ShoulderAnchor::Model)) == "model");

    env = {{"ETERNALVR_OFFHAND_SHOULDER", "-0.05,0.2,-0.25"}};
    r = evr::input::parseControllerSettings(lookup);
    CHECK(r.issues.empty());
    CHECK(r.settings.offhandShoulder == ShoulderAnchor::Head);
    CHECK(r.settings.offhandShoulderOffset.left == doctest::Approx(0.2f));

    env = {{"ETERNALVR_OFFHAND_SHOULDER", "hip"}, {"ETERNALVR_OFFHAND_ELBOW", "0,0,0"}};
    r = evr::input::parseControllerSettings(lookup);
    CHECK(r.issues.size() == 2);
    CHECK(r.settings.offhandShoulder == ShoulderAnchor::Head);
    CHECK(r.settings.offhandElbow == evr::input::kDefaultOffhandElbow);
}
