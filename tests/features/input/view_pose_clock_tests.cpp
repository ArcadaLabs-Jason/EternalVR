#include "features/input/view_pose_clock.hpp"

#include "features/input/offhand_policy.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

using evr::input::ArmBlend;
using evr::input::ArmSignals;
using evr::input::decideArm;
using evr::input::OffhandMode;
using evr::input::ViewPoseClock;

namespace {

// Microseconds. The layer's limits: the views' stale limit and the hitch cap (controllers_impl.hpp
// kWorldStaleSeconds, kWorldHitchCapSeconds) and the arm's default blend and hold (controller_settings.hpp).
constexpr std::int64_t kStale = 100'000;
constexpr std::int64_t kCap = 1'000'000;
constexpr float kBlend = 0.15f;
constexpr float kHold = 0.25f;
constexpr std::int64_t kFrame = 16'667; // 60 Hz

// The game as the hooks see it: per frame one tick of the hands (the viewmodel hook stamps the tick and
// places the arms model; the off-hand hook reads the view's hand and the placement, before or after the
// viewmodel hook) and then one view (the camera hook writes the hands). The off hand's arm runs through the
// real policy and blend.
struct Game {
    bool offhandFirst = false;
    ViewPoseClock clock;
    ArmBlend blend;
    std::int64_t t = 1'000'000;
    std::int64_t world = 0; // the latest view's stamp (0: none yet)
    bool worldValid = true; // the runtime's location flags
    std::int64_t model = 0; // the latest placement's stamp
    std::int64_t lastOffhand = 0;
    float weight = 0.0f;
    bool controller = false;

    void offhand() {
        const std::int64_t now = t + 300;
        ArmSignals s;
        s.pendingAction = 1;
        s.offHandTracked = worldValid && world != 0 && clock.current(world, now, kStale, kCap);
        s.modelPlaced = model != 0 && clock.current(model, now, kStale, kCap);
        controller = decideArm(s, OffhandMode::Free).controller;
        const float dt = lastOffhand ? static_cast<float>(now - lastOffhand) * 1e-6f : 0.0f;
        lastOffhand = now;
        weight = blend.update(controller, dt, kBlend, kHold);
    }
    void viewmodel() {
        clock.tick(t);
        if (worldValid && world != 0 && clock.current(world, t + 100, kStale, kCap)) {
            model = t + 200;
        }
    }
    // One frame `gap` after the last; `view`: the camera hook runs after the tick.
    void frame(std::int64_t gap = kFrame, bool view = true) {
        t += gap;
        if (offhandFirst) {
            offhand();
            viewmodel();
        } else {
            viewmodel();
            offhand();
        }
        if (view) {
            world = t + 2'000;
        }
    }
    void frames(int n) {
        for (int i = 0; i < n; ++i) {
            frame();
        }
    }
};

// The lowest arm weight over the frames from a hitch of `stallUs` on to 1 s after it.
float lowestThroughHitch(bool offhandFirst, std::int64_t stallUs) {
    Game g;
    g.offhandFirst = offhandFirst;
    g.frames(120); // the controller has the arm, fully blended in
    REQUIRE(g.weight == doctest::Approx(1.0f));
    g.frame(kFrame + stallUs);
    float lowest = g.weight;
    for (int i = 0; i < 60; ++i) {
        g.frame();
        lowest = g.weight < lowest ? g.weight : lowest;
    }
    return lowest;
}

} // namespace

TEST_CASE("view pose clock: a hitch does not hand the arm back, whichever hook reads first") {
    // Measured on the rig: single frames of 125 to 891 ms (a scripted 150 and 400 ms stall, and the hands
    // rig's one-off frames) handed the arm back for about 0.45 s (blend out, hold, blend in). Up to the hitch
    // cap (1 s).
    for (const bool offhandFirst : {false, true}) {
        for (const std::int64_t stall : {150'000, 400'000, 900'000}) {
            CAPTURE(offhandFirst);
            CAPTURE(stall);
            CHECK(lowestThroughHitch(offhandFirst, stall) == doctest::Approx(1.0f));
        }
    }
}

TEST_CASE("view pose clock: after a freeze past the hitch cap the pose from before it is not current") {
    // The views and the ticks both stop (a load, a freeze); a shot (or the off hand) read before the first
    // tick after it must not take the pose from before it.
    for (const std::int64_t freeze : {1'200'000, 3'000'000}) {
        Game g;
        g.frames(120);
        REQUIRE(g.controller);
        const std::int64_t shot = g.t + freeze;
        CAPTURE(freeze);
        CHECK_FALSE(g.clock.current(g.world, shot, kStale, kCap));
        // The first tick after it: handed back, then the next views take it again.
        g.frame(freeze);
        CHECK_FALSE(g.controller);
        g.frames(40);
        CHECK(g.controller);
        CHECK(g.weight == doctest::Approx(1.0f));
    }
    // Within the cap the pose before the hitch is still the newest: current.
    Game g;
    g.frames(120);
    CHECK(g.clock.current(g.world, g.t + 900'000, kStale, kCap));
}

TEST_CASE("view pose clock: frames below 10 fps keep the arm") {
    for (const bool offhandFirst : {false, true}) {
        Game g;
        g.offhandFirst = offhandFirst;
        for (int i = 0; i < 40; ++i) {
            g.frame(125'000); // 8 fps
        }
        CAPTURE(offhandFirst);
        CHECK(g.controller);
        CHECK(g.weight == doctest::Approx(1.0f));
    }
}

TEST_CASE("view pose clock: when the views stop the arm goes back after the stale limit") {
    for (const bool offhandFirst : {false, true}) {
        Game g;
        g.offhandFirst = offhandFirst;
        g.frames(120);
        REQUIRE(g.controller);
        // The camera hook stops (a menu, a lost session); the ticks go on.
        const std::int64_t stopped = g.t + 2'000;
        std::int64_t handedBack = 0;
        for (int i = 0; i < 60 && handedBack == 0; ++i) {
            g.frame(kFrame, false);
            if (!g.controller) {
                handedBack = g.t - stopped;
            }
        }
        CAPTURE(offhandFirst);
        CHECK(handedBack > 0);
        CHECK(handedBack <= kStale + kFrame);
        // The views come back: the controller takes the arm again.
        g.frames(5);
        CHECK(g.controller);
        // A hitch while the views are stopped does not make their old pose current.
        g.frames(1);
        g.frame(kFrame, false);
        g.frame(kFrame, false);
        g.frame(400'000, false);
        CHECK_FALSE(g.controller);
    }
}

TEST_CASE("view pose clock: a lost controller hands the arm back at once") {
    Game g;
    g.frames(120);
    REQUIRE(g.controller);
    g.worldValid = false; // the runtime clears the location's valid bits
    g.frame();
    CHECK_FALSE(g.controller);
}

TEST_CASE("view pose clock: the rules by themselves") {
    ViewPoseClock c;
    // Before two ticks only the age counts.
    CHECK(c.current(1'000, 1'000 + kStale, kStale, kCap));
    CHECK_FALSE(c.current(1'000, 1'001 + kStale, kStale, kCap));
    c.tick(10'000);
    CHECK_FALSE(c.current(9'000, 9'000 + kStale + 1, kStale, kCap));
    c.tick(20'000);
    // Written after the tick before the last: current up to the hitch cap.
    CHECK(c.current(10'001, 10'001 + kCap, kStale, kCap));
    CHECK_FALSE(c.current(10'001, 10'002 + kCap, kStale, kCap));
    CHECK_FALSE(c.current(10'000, 500'000, kStale, kCap));
    // Older than that: by age.
    CHECK(c.current(9'000, 9'000 + kStale, kStale, kCap));
    CHECK_FALSE(c.current(9'000, 9'001 + kStale, kStale, kCap));
    // A stamp after the reader's time (another thread): current.
    CHECK(c.current(30'000, 25'000, kStale, kCap));
}
