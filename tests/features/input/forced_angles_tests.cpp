#include "features/input/forced_angles.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

using evr::input::aimsWithHead;
using evr::input::ClimbCvarAction;
using evr::input::climbCvarAction;
using evr::input::ClimbFrames;
using evr::input::climbLookSwitch;
using evr::input::ForcedAngleGate;
using evr::input::ForcedAngleSignals;
using evr::input::ForcedReason;
using evr::input::forcedReasonName;
using evr::input::kClimbLookCvars;
using evr::input::kInhibitViewMask;
using evr::input::SavedCvarValue;
using evr::input::yieldsAimOnly;

namespace {

ForcedAngleSignals quiet() {
    return {};
}

ForcedAngleSignals forcedCall() {
    ForcedAngleSignals s;
    s.foreignSetViewAngles = true;
    return s;
}

} // namespace

TEST_CASE("free play never yields") {
    ForcedAngleGate gate;
    for (int i = 0; i < 100; ++i) {
        CHECK_FALSE(gate.update(quiet()));
    }
    CHECK(gate.reason() == ForcedReason::None);
    CHECK(gate.yieldedFrames() == 0);
    CHECK(gate.episodes() == 0);
}

TEST_CASE("a forced SetViewAngles yields that frame and the resume frames after it") {
    ForcedAngleGate gate(2);
    CHECK(gate.update(forcedCall()));
    CHECK(gate.reason() == ForcedReason::SetViewAngles);
    CHECK(gate.update(quiet()));
    CHECK(gate.reason() == ForcedReason::Settling);
    CHECK(gate.update(quiet()));
    CHECK_FALSE(gate.update(quiet()));
    CHECK(gate.reason() == ForcedReason::None);
    CHECK(gate.episodes() == 1);
    CHECK(gate.yieldedFrames() == 3);
}

TEST_CASE("a glory kill (calls every frame) yields throughout and counts as one episode") {
    ForcedAngleGate gate(2);
    for (int i = 0; i < 90; ++i) {
        CHECK(gate.update(forcedCall()));
    }
    CHECK(gate.update(quiet()));
    CHECK(gate.update(quiet()));
    CHECK_FALSE(gate.update(quiet()));
    CHECK(gate.episodes() == 1);
}

TEST_CASE("the view inhibit bits yield; other inhibit bits do not") {
    ForcedAngleGate gate(0);
    ForcedAngleSignals s;
    s.inhibitFlags = 0x8; // VIEW
    CHECK(gate.update(s));
    CHECK(gate.reason() == ForcedReason::Inhibit);
    s.inhibitFlags = 0x100; // VIEW_ONCE
    CHECK(gate.update(s));
    s.inhibitFlags = 0x10 | 0x20 | 0x40 | 0x4000; // buttons, weapon change, dash, jump
    CHECK_FALSE(gate.update(s));
    CHECK((kInhibitViewMask & 0x10) == 0);
}

TEST_CASE("a cutscene yields and names itself before the other signals") {
    ForcedAngleGate gate(1);
    ForcedAngleSignals s = forcedCall();
    s.cutscene = true;
    s.inhibitFlags = 0x8;
    CHECK(gate.update(s));
    CHECK(gate.reason() == ForcedReason::Cutscene);
}

TEST_CASE("a hands animation that moves the camera yields, after the game's own signals") {
    ForcedAngleGate gate(1);
    ForcedAngleSignals s;
    s.cameraAnimation = true;
    CHECK(gate.update(s));
    CHECK(gate.reason() == ForcedReason::CameraAnimation);
    CHECK(std::string(forcedReasonName(gate.reason())) == "camera animation");
    s.inhibitFlags = 0x8;
    CHECK(gate.update(s));
    CHECK(gate.reason() == ForcedReason::Inhibit);
    CHECK(gate.update(quiet()));
    CHECK(gate.reason() == ForcedReason::Settling);
    CHECK_FALSE(gate.update(quiet()));
}

TEST_CASE("separate forced moments are separate episodes") {
    ForcedAngleGate gate(1);
    gate.update(forcedCall());
    gate.update(quiet());
    gate.update(quiet());
    gate.update(forcedCall());
    CHECK(gate.episodes() == 2);
}

TEST_CASE("a signal during the resume frames restarts the wait") {
    ForcedAngleGate gate(2);
    gate.update(forcedCall());
    gate.update(quiet());
    CHECK(gate.update(forcedCall()));
    CHECK(gate.update(quiet()));
    CHECK(gate.update(quiet()));
    CHECK_FALSE(gate.update(quiet()));
    CHECK(gate.episodes() == 1);
}

TEST_CASE("a climbable wall yields all but the aim, which follows the head") {
    ForcedAngleGate gate(2);
    ForcedAngleSignals s;
    s.wallClimb = true;
    s.inhibitFlags = 0x7; // the wall-climb mechanic's bits without the view bit (its takeover off)
    for (int i = 0; i < 30; ++i) {
        CHECK(gate.update(s));
        CHECK(gate.reason() == ForcedReason::WallClimb);
    }
    CHECK(aimsWithHead(gate.reason()));
    CHECK(std::string(forcedReasonName(gate.reason())) == "climbable wall");
    CHECK(gate.episodes() == 1);
    // Off the wall: the resume frames, in which nothing is written, then free play.
    CHECK(gate.update(quiet()));
    CHECK(gate.reason() == ForcedReason::Settling);
    CHECK_FALSE(aimsWithHead(gate.reason()));
    CHECK(gate.update(quiet()));
    CHECK_FALSE(gate.update(quiet()));
}

TEST_CASE("on a wall the game's SetViewAngles calls leave the aim on the head") {
    ForcedAngleGate gate(2);
    ForcedAngleSignals s;
    s.wallClimb = true;
    s.inhibitFlags = 0x7;
    // The climb animation's deltas every tick, then the let-go once: the climb state holds throughout.
    for (int i = 0; i < 40; ++i) {
        s.foreignSetViewAngles = (i % 2) == 0;
        CHECK(gate.update(s));
        CHECK(gate.reason() == ForcedReason::WallClimb);
        CHECK(aimsWithHead(gate.reason()));
    }
    CHECK(gate.episodes() == 1);
}

TEST_CASE("on a wall a cutscene still takes the view") {
    ForcedAngleGate gate(1);
    ForcedAngleSignals s;
    s.wallClimb = true;
    s.foreignSetViewAngles = true;
    s.cutscene = true;
    CHECK(gate.update(s));
    CHECK(gate.reason() == ForcedReason::Cutscene);
    CHECK_FALSE(aimsWithHead(gate.reason()));
}

TEST_CASE("on a wall the view inhibit bits still take the view") {
    ForcedAngleGate gate(1);
    ForcedAngleSignals s;
    s.wallClimb = true;
    s.foreignSetViewAngles = true;
    s.inhibitFlags = 0x8; // the view bit, as the mechanic sets it with its takeover on
    CHECK(gate.update(s));
    CHECK(gate.reason() == ForcedReason::Inhibit);
    CHECK_FALSE(aimsWithHead(gate.reason()));
    s.inhibitFlags = 0x7;
    s.cameraAnimation = true; // a hands animation does not take the head's aim away on the wall
    CHECK(gate.update(s));
    CHECK(gate.reason() == ForcedReason::WallClimb);
}

TEST_CASE("off a wall a foreign SetViewAngles still yields everything") {
    ForcedAngleGate gate(1);
    ForcedAngleSignals s = forcedCall();
    s.cameraAnimation = true;
    CHECK(gate.update(s));
    CHECK(gate.reason() == ForcedReason::SetViewAngles);
    CHECK_FALSE(aimsWithHead(gate.reason()));
}

TEST_CASE("only the climbable wall aims with the head") {
    for (const ForcedReason r :
         {ForcedReason::None, ForcedReason::SetViewAngles, ForcedReason::Inhibit, ForcedReason::Cutscene,
          ForcedReason::CameraAnimation, ForcedReason::WeaponWheel, ForcedReason::Settling}) {
        CHECK_FALSE(aimsWithHead(r));
    }
    CHECK(aimsWithHead(ForcedReason::WallClimb));
}

namespace {

ForcedAngleSignals wheel(std::uint32_t inhibitFlags) {
    ForcedAngleSignals s;
    s.weaponWheel = true;
    s.inhibitFlags = inhibitFlags;
    return s;
}

} // namespace

TEST_CASE("the weapon wheel's inhibit bits yield the aim only, with no settling after") {
    ForcedAngleGate gate(2);
    CHECK(gate.update(wheel(0x18)));
    CHECK(gate.reason() == ForcedReason::WeaponWheel);
    CHECK(yieldsAimOnly(gate.reason()));
    CHECK(gate.update(wheel(0x8)));
    CHECK(gate.reason() == ForcedReason::WeaponWheel);
    CHECK_FALSE(gate.update(quiet())); // released: nothing forced the view
    CHECK(gate.reason() == ForcedReason::None);
    CHECK(gate.episodes() == 1);
}

TEST_CASE("the wheel held does not soften other inhibit bits, a cutscene or a forced call") {
    ForcedAngleGate gate(2);
    CHECK(gate.update(wheel(0x1F87F)));
    CHECK(gate.reason() == ForcedReason::Inhibit);
    CHECK(gate.update(wheel(0x118)));
    CHECK(gate.reason() == ForcedReason::Inhibit);
    ForcedAngleSignals call = wheel(0x18);
    call.foreignSetViewAngles = true;
    CHECK(gate.update(call));
    CHECK(gate.reason() == ForcedReason::SetViewAngles);
    ForcedAngleSignals scene = wheel(0x18);
    scene.cutscene = true;
    CHECK(gate.update(scene));
    CHECK(gate.reason() == ForcedReason::Cutscene);
}

TEST_CASE("the wheel's bits outliving the button keep the wheel's reason until they clear") {
    ForcedAngleGate gate(2);
    CHECK(gate.update(wheel(0x18)));
    ForcedAngleSignals lingering;
    lingering.inhibitFlags = 0x18; // the button is up, the game has not taken the release yet
    CHECK(gate.update(lingering));
    CHECK(gate.reason() == ForcedReason::WeaponWheel);
    CHECK_FALSE(gate.update(quiet()));
    CHECK(gate.episodes() == 1);
}

TEST_CASE("the wheel on a climbable wall or in a camera animation yields everything") {
    ForcedAngleGate gate(2);
    ForcedAngleSignals climb = wheel(0x18);
    climb.wallClimb = true;
    CHECK(gate.update(climb));
    CHECK(gate.reason() == ForcedReason::Inhibit);
    ForcedAngleSignals anim = wheel(0x18);
    anim.cameraAnimation = true;
    CHECK(gate.update(anim));
    CHECK(gate.reason() == ForcedReason::Inhibit);
}

TEST_CASE("the wheel's bits without the wheel held yield everything") {
    ForcedAngleGate gate(2);
    ForcedAngleSignals s;
    s.inhibitFlags = 0x18;
    CHECK(gate.update(s));
    CHECK(gate.reason() == ForcedReason::Inhibit);
    CHECK_FALSE(yieldsAimOnly(gate.reason()));
}

TEST_CASE("a forced view settling when the wheel opens still settles after it") {
    ForcedAngleGate gate(2);
    CHECK(gate.update(forcedCall()));
    CHECK(gate.update(wheel(0x18)));
    CHECK(gate.reason() == ForcedReason::WeaponWheel);
    CHECK(gate.update(quiet()));
    CHECK(gate.reason() == ForcedReason::Settling);
    CHECK(gate.update(quiet()));
    CHECK_FALSE(gate.update(quiet()));
}

TEST_CASE("only the weapon wheel yields the aim only") {
    for (const ForcedReason r :
         {ForcedReason::None, ForcedReason::SetViewAngles, ForcedReason::Inhibit, ForcedReason::Cutscene,
          ForcedReason::CameraAnimation, ForcedReason::WallClimb, ForcedReason::Settling}) {
        CHECK_FALSE(yieldsAimOnly(r));
    }
    CHECK(yieldsAimOnly(ForcedReason::WeaponWheel));
    CHECK(std::string(forcedReasonName(ForcedReason::WeaponWheel)) == "weapon wheel");
}

TEST_CASE("the climb-look switch is on unless 0 or off") {
    CHECK(climbLookSwitch(L""));
    CHECK(climbLookSwitch(L"1"));
    CHECK(climbLookSwitch(L"on"));
    CHECK(climbLookSwitch(L"00"));
    CHECK_FALSE(climbLookSwitch(L"0"));
    CHECK_FALSE(climbLookSwitch(L"off"));
    CHECK_FALSE(climbLookSwitch(L"OFF"));
}

TEST_CASE("the climb-look cvars turn the wall-climb takeover and its dead zone off") {
    REQUIRE(kClimbLookCvars.size() == 2);
    CHECK(kClimbLookCvars[0].name == std::string_view("wallclimb_takeoverViewAngles"));
    CHECK(kClimbLookCvars[0].value == std::string_view("0"));
    CHECK(kClimbLookCvars[1].name == std::string_view("wallclimb_deadZone_enable"));
    CHECK(kClimbLookCvars[1].value == std::string_view("0"));
}

TEST_CASE("climb frames: on a wall from the first step, held over frames without a tick") {
    ClimbFrames climb(2);
    CHECK_FALSE(climb.update(0));
    CHECK(climb.update(1));
    CHECK(climb.update(0)); // a render between two ticks
    CHECK(climb.update(0));
    CHECK(climb.update(2)); // two ticks since the last frame
    CHECK(climb.climbs() == 1);
    CHECK(climb.update(0));
    CHECK(climb.update(0));
    CHECK_FALSE(climb.update(0)); // off the wall
    CHECK_FALSE(climb.onWall());
    CHECK(climb.frames() == 6);
    CHECK(climb.update(1)); // the next wall
    CHECK(climb.climbs() == 2);
}

TEST_CASE("climb frames without a hold follow the ticks exactly") {
    ClimbFrames climb(0);
    CHECK(climb.update(1));
    CHECK_FALSE(climb.update(0));
    CHECK(climb.update(1));
    CHECK(climb.climbs() == 2);
    CHECK(climb.frames() == 2);
}

TEST_CASE("the climb cvars are held while wanted and allowed, and given back once when the guard trips") {
    CHECK(climbCvarAction(true, true, false) == ClimbCvarAction::Hold);
    CHECK(climbCvarAction(true, true, true) == ClimbCvarAction::Hold);
    // The guard trips: the saved values go back, once (nothing is saved after that).
    CHECK(climbCvarAction(true, false, true) == ClimbCvarAction::Restore);
    CHECK(climbCvarAction(true, false, false) == ClimbCvarAction::None);
    // Not wanted (the switch off, aim view): nothing written, and anything saved goes back.
    CHECK(climbCvarAction(false, true, false) == ClimbCvarAction::None);
    CHECK(climbCvarAction(false, true, true) == ClimbCvarAction::Restore);
}

TEST_CASE("a saved cvar value is the game's value before the first write, handed back once") {
    SavedCvarValue v;
    CHECK_FALSE(v.saved());
    CHECK_FALSE(v.take().has_value());
    v.beforeWrite(1);
    CHECK(v.saved());
    v.beforeWrite(0); // the game put the value back to something else and the layer wrote again
    const std::optional<int> first = v.take();
    REQUIRE(first.has_value());
    CHECK(*first == 1);
    CHECK_FALSE(v.saved());
    CHECK_FALSE(v.take().has_value());
    v.beforeWrite(2); // held again later (a new session): saved afresh
    CHECK(v.take().value_or(-1) == 2);
}

TEST_CASE("the guard tripping mid-session: hold, then one restore of the saved values, then nothing") {
    SavedCvarValue v;
    int writes = 0;
    int restored = -1;
    const auto frame = [&](bool allowed) {
        switch (climbCvarAction(true, allowed, v.saved())) {
        case ClimbCvarAction::Hold:
            v.beforeWrite(1);
            ++writes;
            break;
        case ClimbCvarAction::Restore:
            restored = *v.take();
            break;
        case ClimbCvarAction::None:
            break;
        }
    };
    frame(true);
    frame(true);
    CHECK(writes == 2);
    frame(false);
    CHECK(restored == 1);
    restored = -1;
    frame(false);
    CHECK(restored == -1);
    CHECK(writes == 2);
}
