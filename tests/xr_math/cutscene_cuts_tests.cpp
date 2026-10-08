#include "xr_math/cutscene_cuts.hpp"

#include <doctest/doctest.h>

#include <cmath>

using evr::xr_math::CutEvent;
using evr::xr_math::CutFrame;
using evr::xr_math::CutRebaseState;
using evr::xr_math::cutRebaseStep;
using evr::xr_math::CutStep;
using evr::xr_math::HeadAimState;
using evr::xr_math::headAimStep;
using evr::xr_math::normalize180;
using evr::xr_math::rebaseHeadYaw;

namespace {

constexpr double kFrame = 1.0 / 60.0;

bool near(float a, float b) {
    return std::fabs(normalize180(a - b)) < 1e-3f;
}

// Game frames one after another, a frame time apart unless a gap is asked for.
struct Scene {
    CutRebaseState s;
    double t = 0.0;

    CutStep step(CutFrame f, double after = kFrame) {
        t += after;
        f.seconds = t;
        return cutRebaseStep(s, f);
    }
    // A cutscene frame whose body is the camera's yaw less a fixed head yaw held (head aim's scripted
    // camera).
    CutStep
    camera(float cameraYaw, float headYaw, float held = 0.0f, float pitch = 0.0f, double after = kFrame) {
        CutFrame f;
        f.cutscene = true;
        f.cameraYaw = cameraYaw;
        f.cameraPitch = pitch;
        f.bodyYaw = normalize180(cameraYaw - held);
        f.headYaw = headYaw;
        return step(f, after);
    }
    CutStep play(float bodyYaw, float headYaw) {
        CutFrame f;
        f.bodyYaw = bodyYaw;
        f.headYaw = headYaw;
        return step(f);
    }
};

} // namespace

TEST_CASE("the cutscene's first frame puts the camera's forward where the head looks") {
    Scene c;
    const CutStep step = c.camera(30.0f, 40.0f, 10.0f);
    CHECK(step.event == CutEvent::Start);
    // The view's yaw is body + head: the camera's.
    CHECK(near(step.bodyYaw + 40.0f, 30.0f));
    CHECK(near(step.turn, -30.0f)); // body 20 -> -10
}

TEST_CASE("within a shot the view follows the camera's own turns, a pan of more than 90 degrees too") {
    Scene c;
    c.camera(0.0f, 25.0f);
    CutStep step{};
    for (int i = 1; i <= 60; ++i) {
        step = c.camera(static_cast<float>(i) * 2.5f, 25.0f); // a 150 degree pan over a second
        CHECK(step.event == CutEvent::None);
    }
    CHECK(near(step.bodyYaw + 25.0f, 150.0f));
    // The head turning does not move the body: the view turns with it.
    step = c.camera(150.0f, -50.0f);
    CHECK(step.event == CutEvent::None);
    CHECK(near(step.bodyYaw + 25.0f, 150.0f));
}

TEST_CASE("a reverse shot keeps the action where the player looks instead of swinging the world") {
    Scene c;
    c.camera(0.0f, 0.0f);
    // The player turned 70 degrees to follow something, then the cutscene cuts to a reverse shot.
    c.camera(0.0f, 70.0f);
    const CutStep step = c.camera(170.0f, 70.0f);
    CHECK(step.event == CutEvent::Cut);
    CHECK(near(step.cameraTurn, 170.0f));
    CHECK(near(step.bodyYaw + 70.0f, 170.0f)); // the new shot's forward is where the head looks
    // Without the re-base the head would look 70 degrees off the new shot's forward.
    CHECK(near(step.turn, -70.0f));
    // A cut the other way, and across the +-180 wrap.
    const CutStep back = c.camera(-20.0f, 70.0f);
    CHECK(back.event == CutEvent::Cut);
    CHECK(near(back.bodyYaw + 70.0f, -20.0f));
}

TEST_CASE("a turn of 90 degrees or less in one frame is not a cut") {
    Scene c;
    c.camera(0.0f, 30.0f);
    CHECK(c.camera(90.0f, 30.0f).event == CutEvent::None);
    CHECK(c.camera(-180.0f + 1.0f, 30.0f).event == CutEvent::Cut); // 91 the short way
}

TEST_CASE("a camera looking almost straight up or down flips its yaw without a cut") {
    Scene c;
    c.camera(0.0f, 10.0f, 0.0f, 70.0f);
    // The camera tilts past the vertical: its forward's yaw turns half round in one frame.
    CHECK(c.camera(180.0f, 10.0f, 0.0f, 85.0f).event == CutEvent::None);
    CHECK(c.camera(180.0f, 10.0f, 0.0f, 70.0f).event == CutEvent::None);
    // The frame after a steep one is not a cut either; a level camera's half turn is.
    CHECK(c.camera(0.0f, 10.0f, 0.0f, -85.0f).event == CutEvent::None);
    CHECK(c.camera(180.0f, 10.0f, 0.0f, 0.0f).event == CutEvent::None);
    CHECK(c.camera(0.0f, 10.0f, 0.0f, 0.0f).event == CutEvent::Cut);
}

TEST_CASE("a gap between frames (a map load, a hitch) re-bases like a cutscene's first frame") {
    Scene c;
    c.camera(0.0f, 0.0f);
    c.camera(10.0f, 30.0f);
    // Back-to-back cutscenes across a load: the next one's first frame comes 3 s later.
    const CutStep load = c.camera(120.0f, 30.0f, 0.0f, 0.0f, 3.0);
    CHECK(load.event == CutEvent::Gap);
    CHECK(load.gapSeconds > 2.9);
    CHECK(near(load.bodyYaw + 30.0f, 120.0f));
    // A hitch of 0.6 s while the camera pans: re-based, not a cut.
    CHECK(c.camera(160.0f, 30.0f, 0.0f, 0.0f, 0.6).event == CutEvent::Gap);
    // A short stall is none.
    CHECK(c.camera(165.0f, 30.0f, 0.0f, 0.0f, 0.2).event == CutEvent::None);
}

TEST_CASE("a menu over the cutscene keeps its offset and starts nothing") {
    Scene c;
    c.camera(0.0f, 40.0f); // body -40
    CutFrame menu;
    menu.cutscene = true;
    menu.menu = true;
    menu.cameraYaw = 0.0f;
    menu.bodyYaw = 0.0f;
    menu.headYaw = -60.0f; // the head looks round at the pause menu
    for (int i = 0; i < 120; ++i) {
        const CutStep step = c.step(menu, 0.1); // 12 s paused, no gap counted
        CHECK(step.event == CutEvent::None);
        CHECK(near(step.bodyYaw, -40.0f));
    }
    CHECK(c.camera(0.0f, -60.0f).event == CutEvent::None);
    // A cutscene that starts under a menu starts when the menu goes.
    Scene d;
    menu.bodyYaw = 15.0f;
    CHECK(near(d.step(menu).bodyYaw, 15.0f));
    CHECK(d.camera(0.0f, 20.0f).event == CutEvent::Start);
}

TEST_CASE("the end drops the re-base, and the next cutscene starts afresh") {
    Scene c;
    c.camera(0.0f, 45.0f);
    const CutStep end = c.play(12.0f, 45.0f);
    CHECK(end.event == CutEvent::End);
    CHECK(near(end.bodyYaw, 12.0f));
    CHECK(near(end.turn, 45.0f));
    const CutStep play = c.play(13.0f, 45.0f);
    CHECK(play.event == CutEvent::None);
    CHECK(near(play.bodyYaw, 13.0f));
    CHECK(c.camera(100.0f, 0.0f).event == CutEvent::Start);
}

TEST_CASE("after a cutscene head aim's yaw is re-based: the player's view faces the game's yaw where the "
          "head looks") {
    HeadAimState state;
    // Head aim held 10 degrees of head yaw before the cutscene; the head is at 80 when it ends.
    headAimStep(state, {0.0f, 50.0f, 0.0f}, 0.0f, {0.0f, 10.0f, 0.0f}, false);
    CHECK(near(rebaseHeadYaw(state, 80.0f), -70.0f));
    // The game hands back the player's view at yaw 200: the body is that less the head, so the view
    // (body + head) faces 200, and under head aim nothing is added to the game's aim (under hand aim the
    // hand's yaw is, from there).
    const auto step = headAimStep(state, {0.0f, 200.0f, 0.0f}, 0.0f, {0.0f, 80.0f, 0.0f}, false);
    CHECK(near(step.bodyYaw + 80.0f, 200.0f));
    CHECK(near(step.deltaYaw, 0.0f));
    // A delta the game rewrites to an older value is not taken for one head aim wrote before.
    HeadAimState rewritten;
    headAimStep(rewritten, {0.0f, 0.0f, 0.0f}, 0.0f, {0.0f, 10.0f, 0.0f}, false);
    evr::xr_math::noteWritten(rewritten, 10.0f);
    rebaseHeadYaw(rewritten, 80.0f);
    const auto restored = headAimStep(rewritten, {0.0f, 30.0f, 0.0f}, 10.0f, {0.0f, 80.0f, 0.0f}, true);
    CHECK(near(restored.bodyYaw, 30.0f - 80.0f));
    CHECK_FALSE(restored.restoredWrite);
    // Before any write the head yaw held is 0.
    HeadAimState fresh;
    CHECK(near(rebaseHeadYaw(fresh, 30.0f), -30.0f));
    CHECK(fresh.injected);
}
