#include "features/input/offhand_policy.hpp"

#include "features/input/controller_settings.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

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

TEST_CASE("offhand policy: left-handed, the game's off hand follows the right controller on a row's weapon") {
    using evr::input::leftHandOffMode;
    // The chaingun's row: its game left grip, reflected, closes on nothing, so the right glove follows the
    // right controller and the free off hand's rules decide when.
    CHECK(leftHandOffMode(OffhandMode::Game, true) == OffhandMode::Free);
    CHECK(decideArm(idle(), leftHandOffMode(OffhandMode::Game, true)).controller);
    // Every other weapon keeps the reflected grip; the free off hand is free on every weapon.
    CHECK(leftHandOffMode(OffhandMode::Game, false) == OffhandMode::Game);
    CHECK(leftHandOffMode(OffhandMode::Free, false) == OffhandMode::Free);
    CHECK(leftHandOffMode(OffhandMode::Free, true) == OffhandMode::Free);
    CHECK(leftHandOffMode(OffhandMode::Probe, true) == OffhandMode::Probe);
}

TEST_CASE("offhand policy: left-handed, the game's grips are read only outside the game's own arm actions") {
    using evr::input::readsGameGrip;
    using evr::input::WeaponArmMode;
    // An idle gun: the grip read is the held item's.
    CHECK(readsGameGrip(idle(), WeaponArmMode::Ik));
    // With the weapon arm the game's no grip is read at all: the left-handed arms are the layer's in ik only.
    CHECK_FALSE(readsGameGrip(idle(), WeaponArmMode::Game));
    // The raise of a first equip, a melee's fists, a switch, the chainsaw's quick action: the pose is the
    // action's.
    ArmSignals raise = idle();
    raise.pendingAction = action(HandsAction::BringUp);
    CHECK_FALSE(readsGameGrip(raise, WeaponArmMode::Ik));
    ArmSignals melee = idle();
    melee.pendingAction = action(HandsAction::Melee);
    CHECK_FALSE(readsGameGrip(melee, WeaponArmMode::Ik));
    ArmSignals punch = idle();
    punch.handsFlags = hands_flag::kMeleeAnim;
    CHECK_FALSE(readsGameGrip(punch, WeaponArmMode::Ik));
    ArmSignals change = idle();
    change.handsFlags = hands_flag::kChangingWeapon;
    CHECK_FALSE(readsGameGrip(change, WeaponArmMode::Ik));
    ArmSignals saw = idle();
    saw.destHandsState = evr::input::kHandsStateChainsawRev;
    CHECK_FALSE(readsGameGrip(saw, WeaponArmMode::Ik));
    ArmSignals glory = idle();
    glory.forcedView = true;
    CHECK_FALSE(readsGameGrip(glory, WeaponArmMode::Ik));
    ArmSignals lost = idle();
    lost.offHandTracked = false;
    CHECK_FALSE(readsGameGrip(lost, WeaponArmMode::Ik));
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

TEST_CASE("weapon arm policy: IK on an idle arm, the game's for every arm animation and in game mode") {
    using evr::input::decideWeaponArm;
    using evr::input::WeaponArmMode;
    CHECK(decideWeaponArm(idle(), WeaponArmMode::Ik).controller);
    const auto game = decideWeaponArm(idle(), WeaponArmMode::Game);
    CHECK_FALSE(game.controller);
    CHECK(game.reason == ArmReason::ModeGame);
    const auto with = [](auto change) {
        ArmSignals s = idle();
        change(s);
        return s;
    };
    const std::pair<ArmSignals, ArmReason> cases[] = {
        {with([](ArmSignals& s) { s.forcedView = true; }), ArmReason::ForcedView},
        {with([](ArmSignals& s) { s.syncActive = true; }), ArmReason::Sync},
        {with([](ArmSignals& s) { s.hiddenReasons = 1; }), ArmReason::HandsHidden},
        {with([](ArmSignals& s) { s.pendingAction = action(HandsAction::MeleeRight); }), ArmReason::Action},
        {with([](ArmSignals& s) { s.pendingAction = action(HandsAction::ThrowItem); }), ArmReason::Action},
        {with([](ArmSignals& s) { s.pendingAction = action(HandsAction::BringUp); }), ArmReason::Action},
        {with([](ArmSignals& s) { s.pendingAction = action(HandsAction::CustomAnim); }), ArmReason::Action},
        {with([](ArmSignals& s) { s.handsFlags = hands_flag::kChangingWeapon; }), ArmReason::BusyFlags},
        {with([](ArmSignals& s) { s.offHandTracked = false; }), ArmReason::Untracked},
        {with([](ArmSignals& s) { s.modelPlaced = false; }), ArmReason::Untracked},
    };
    for (const auto& [signals, reason] : cases) {
        const auto d = decideWeaponArm(signals, WeaponArmMode::Ik);
        CHECK_FALSE(d.controller);
        CHECK(d.reason == reason);
    }
    // Firing stays with the IK.
    ArmSignals firing = idle();
    firing.handsFlags = (1ull << 10) | (1ull << 13);
    CHECK(decideWeaponArm(firing, WeaponArmMode::Ik).controller);
}

TEST_CASE("arms item: while the hands swim the arms hold the swim fists' pose, whatever the held item") {
    using evr::input::armsItem;
    using evr::input::handsSwim;
    // The hands' state and flags as the hands rig's layer logs show them (ETERNALVR_OFFHAND_TRACE lines):
    struct Sample {
        std::int32_t state;
        std::uint64_t flags;
        bool swim;
    };
    const Sample samples[] = {
        {0, 0x0000200080000030ull, false}, // on land, the shotgun held
        {0, 0x00012008D00000F0ull, true},  // in the water, the swim fists held
        {12, 0x00012008D0000070ull,
         true}, // at the surface, the shotgun held, the arms swimming (HS_SWIM_IDLE)
        {12, 0x00012000D2000050ull, true}, // the first tick of a lower there: the flag clear for that tick
        {0, 0x00012000F0000050ull, false}, // a lower in progress at the surface: neither (the game's arms)
    };
    for (const Sample& s : samples) {
        CHECK(handsSwim(s.state, s.flags) == s.swim);
    }
    // Stand-ins for the decl pointers: the held gun and the player's swim fists.
    const int gun = 0;
    const int swim = 0;
    const int heldSwim = 0; // the swim fists held, as another decl pointer of that name
    CHECK(armsItem(&gun, false, true, &swim) == &swim);
    CHECK(armsItem(&swim, true, true, &swim) == &swim);
    CHECK(armsItem(&heldSwim, true, true, &swim) == &heldSwim);
    CHECK(armsItem(&gun, false, false, &swim) == &gun);
    CHECK(armsItem(&gun, false, true, nullptr) == &gun); // no swim fists known: the held item
    CHECK(armsItem(nullptr, false, false, &swim) == nullptr);
}

TEST_CASE("arms item: the swim's hands with a gun held stay the layer's (the policy has no reason to give "
          "them back)") {
    using evr::input::decideWeaponArm;
    using evr::input::WeaponArmMode;
    // The signals the rig logged in the water with the shotgun held: no action, HS_SWIM_IDLE, the flags
    // 0x00012008D0000070.
    ArmSignals s = idle();
    s.pendingAction = 0;
    s.destHandsState = evr::input::kHandsStateSwimIdle;
    s.handsFlags = 0x00012008D0000070ull;
    CHECK(decideWeaponArm(s, WeaponArmMode::Ik).controller);
    CHECK(decideArm(s, OffhandMode::Free).controller);
}

TEST_CASE("grip taking: not while the hands swim, nor for the hold-off after, whatever the arms' item") {
    using evr::input::gripTakingAllowed;
    using evr::input::kNeverSwamSeconds;
    using evr::input::kSwimGripHoldOffSeconds;
    using evr::input::secondsSinceSwim;
    // On land, never swum: the weapon arm's rules decide, with the arms on the held item.
    CHECK(gripTakingAllowed(true, true, false, kNeverSwamSeconds));
    CHECK_FALSE(gripTakingAllowed(false, true, false, kNeverSwamSeconds)); // a raise, a melee, a switch
    CHECK_FALSE(gripTakingAllowed(true, false, false, kNeverSwamSeconds)); // the arms on another item
    // The dive: the hands swim a tick before their pose leaves the gun's. With the swim fists not known the
    // arms' item is the held one (armsIsHeld), and the swim alone keeps the grip from being read.
    CHECK_FALSE(gripTakingAllowed(true, true, true, 0.0f));
    // The exit: the hands' signals clear while the arms may still hold the swim's pose, the gun in the slot
    // and the arms' item the held one again.
    float since = secondsSinceSwim(kNeverSwamSeconds, true, 1.0f / 60.0f);
    CHECK(since == 0.0f);
    since = secondsSinceSwim(since, false, 1.0f / 60.0f);
    CHECK_FALSE(gripTakingAllowed(true, true, false, since));
    // Ticks of 1/60 s until the hold-off has passed: no grip before it, then the land's rules again.
    int ticks = 1;
    while (since < kSwimGripHoldOffSeconds) {
        CHECK_FALSE(gripTakingAllowed(true, true, false, since));
        since = secondsSinceSwim(since, false, 1.0f / 60.0f);
        ++ticks;
    }
    CHECK(ticks >= static_cast<int>(kSwimGripHoldOffSeconds * 60.0f));
    CHECK(gripTakingAllowed(true, true, false, since));
    // A swim again restarts the hold-off.
    since = secondsSinceSwim(since, true, 1.0f / 60.0f);
    CHECK_FALSE(gripTakingAllowed(true, true, false, secondsSinceSwim(since, false, 0.5f)));
}

TEST_CASE("swim clock: a bad step counts as none, and the clock saturates") {
    using evr::input::kNeverSwamSeconds;
    using evr::input::secondsSinceSwim;
    CHECK(secondsSinceSwim(1.0f, false, -0.5f) == 1.0f);
    CHECK(secondsSinceSwim(1.0f, false, std::numeric_limits<float>::quiet_NaN()) == 1.0f);
    CHECK(secondsSinceSwim(1.0f, false, std::numeric_limits<float>::infinity()) == 1.0f);
    CHECK(secondsSinceSwim(kNeverSwamSeconds, false, 10.0f) == kNeverSwamSeconds);
    CHECK(secondsSinceSwim(5.0f, true, 0.1f) == 0.0f);
}
