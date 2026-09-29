#include "features/input/forced_angles.hpp"

#include <doctest/doctest.h>

#include <ostream>
#include <string>

using evr::input::ForcedAngleGate;
using evr::input::ForcedAngleSignals;
using evr::input::ForcedReason;
using evr::input::forcedReasonName;
using evr::input::kInhibitViewMask;

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
