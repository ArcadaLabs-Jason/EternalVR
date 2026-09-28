#include "features/roomscale/body_follow.hpp"
#include "features/roomscale/room_anchor.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <ostream>

using evr::Pose;
using evr::Quat;
using evr::Vec3;
using evr::test::approxEqual;
using namespace evr::roomscale;

namespace {

constexpr double kFrame = 1.0 / 150.0; // the rig's game frames in stereo

BodyFollowSettings on() {
    BodyFollowSettings s;
    s.enabled = true;
    return s;
}

float flatLength(Vec3 v) {
    return std::sqrt(v.x * v.x + v.z * v.z);
}

// The game's response to a move command, roughly as measured on the rig: nothing below 30 of 127, a creep
// at 0.15 m/s to 70, a walk at 2.3 m/s to 100, a run above; in the walk tier the body speeds up at about
// 5 m/s2, and without a command it slows by a tenth of its speed every 10 ms until friction stops it.
float tierSpeed(float command) {
    if (command < 30.0f) {
        return 0.0f;
    }
    if (command < 70.0f) {
        return 0.15f;
    }
    return command < 100.0f ? 2.3f : 9.0f;
}

// A body moved by that game, `reach` of the way (0: a wall), one frame late like the game (the command
// goes out after the camera hook and the body moves in the next tick).
struct Sim {
    BodyFollowSettings settings = on();
    BodyFollow follow{on()};
    Vec3 head; // the head relative to the body, room metres
    Vec3 velocity;
    Vec3 pending;
    double lastCommand = -1.0;
    double seconds = 0.0;
    float reach = 1.0f;
    Vec3 absorbedTotal;
    Vec3 movedTotal;

    FollowStep tick(FollowTick extra = {}) {
        seconds += kFrame;
        FollowTick t = extra;
        t.seconds = seconds;
        t.gapRoom = head;
        t.displacement = pending;
        t.commanded = lastCommand >= 0.0 && seconds - lastCommand < kResumeSeconds; // as the glue does
        const FollowStep step = follow.update(t);
        movedTotal = movedTotal + pending;
        head = head - step.absorbed;
        absorbedTotal = absorbedTotal + step.absorbed;
        // The game: speed up toward the command's tier, or slow down.
        const Vec3 wish{step.request.move.right, 0.0f, -step.request.move.forward};
        const float command = flatLength(wish) * 127.0f;
        const Vec3 target = command > 0.0f ? wish * (tierSpeed(command) / flatLength(wish)) : Vec3{};
        const Vec3 change = target - velocity;
        if (flatLength(target) > flatLength(velocity)) {
            const float most = 5.0f * static_cast<float>(kFrame);
            velocity = velocity + (flatLength(change) > most ? change * (most / flatLength(change)) : change);
        } else {
            velocity = velocity + change * std::min(1.0f, 10.0f * static_cast<float>(kFrame));
            if (flatLength(velocity) < 0.02f) {
                velocity = {}; // friction stops it
            }
        }
        if (reach == 0.0f) {
            velocity = {};
        }
        pending = velocity * static_cast<float>(kFrame);
        if (command > 0.0f) {
            lastCommand = seconds;
        }
        return step;
    }

    void run(double forSeconds) {
        const double end = seconds + forSeconds;
        while (seconds < end) {
            tick();
        }
    }
};

} // namespace

TEST_CASE("the controller: deadzone, hysteresis, then walk, creep or coast") {
    const BodyFollowSettings s = on();
    CHECK_FALSE(followRequest({0.0f, 0.0f, -0.03f}, 0.0f, false, s).engaged);
    const FollowRequest r = followRequest({0.0f, 0.0f, -0.2f}, 0.0f, false, s);
    CHECK(r.engaged);
    CHECK(r.tier == FollowTier::Walk);
    CHECK(r.move.forward == doctest::Approx(85.0f / 127.0f));
    CHECK(r.move.right == doctest::Approx(0.0f));
    // Right and a little back, the full walk command along the gap.
    const FollowRequest side = followRequest({0.3f, 0.0f, 0.4f}, 0.0f, false, s);
    CHECK(side.move.right / -side.move.forward == doctest::Approx(0.75f));
    CHECK(std::hypot(side.move.right, side.move.forward) == doctest::Approx(85.0f / 127.0f));
    // Height never counts.
    CHECK_FALSE(followRequest({0.0f, 0.5f, 0.0f}, 0.0f, false, s).engaged);
    // Once going, it keeps going down to 40 % of the deadzone.
    CHECK(followRequest({0.0f, 0.0f, -0.017f}, 0.0f, true, s).engaged);
    CHECK_FALSE(followRequest({0.0f, 0.0f, -0.015f}, 0.0f, true, s).engaged);
    // A body at 0.8 m/s coasts 0.08 m: walk while it would stop short of the stop radius, creep while it
    // would stop short of the head, then coast.
    CHECK(followRequest({0.0f, 0.0f, -0.2f}, 0.8f, true, s).tier == FollowTier::Walk);
    const FollowRequest near = followRequest({0.0f, 0.0f, -0.09f}, 0.8f, true, s);
    CHECK(near.tier == FollowTier::Creep);
    CHECK(near.move.forward == doctest::Approx(60.0f / 127.0f));
    CHECK(followRequest({0.0f, 0.0f, -0.07f}, 0.8f, true, s).tier == FollowTier::Coast);
    // A body moving away from the head coasts nothing.
    CHECK(followRequest({0.0f, 0.0f, -0.05f}, -2.0f, true, s).tier == FollowTier::Walk);
    // Nonsense in, nothing out.
    CHECK_FALSE(followRequest({NAN, 0.0f, 0.0f}, 0.0f, false, s).engaged);
    CHECK_FALSE(followRequest({0.0f, 0.0f, -0.2f}, NAN, false, s).engaged);
}

TEST_CASE("settings out of range fall back to the defaults") {
    BodyFollowSettings s;
    s.deadzoneMetres = 0.0f;
    s.walkCommand = 200;
    s.creepCommand = 5;
    s.coastSeconds = NAN;
    s.maxSpeed = 50.0f;
    const BodyFollowSettings clean = sanitized(s);
    const BodyFollowSettings defaults;
    CHECK(clean.deadzoneMetres == defaults.deadzoneMetres);
    CHECK(clean.walkCommand == defaults.walkCommand);
    CHECK(clean.creepCommand == defaults.creepCommand);
    CHECK(clean.coastSeconds == defaults.coastSeconds);
    CHECK(clean.maxSpeed == defaults.maxSpeed);
}

TEST_CASE("a world displacement in room axes through the body frame") {
    // Body facing world +X: forward is room -Z, left is room -X.
    const Vec3 fwd{1.0f, 0.0f, 0.0f};
    const Vec3 left{0.0f, 1.0f, 0.0f};
    CHECK(approxEqual(displacementInRoom({1.0f, 0.0f, 0.0f}, fwd, left, 1.0f), {0.0f, 0.0f, -1.0f}));
    CHECK(approxEqual(displacementInRoom({0.0f, 1.0f, 0.0f}, fwd, left, 1.0f), {-1.0f, 0.0f, 0.0f}));
    CHECK(approxEqual(displacementInRoom({0.0f, 0.0f, 0.5f}, fwd, left, 1.0f), {0.0f, 0.5f, 0.0f}));
    // Body turned 90 degrees left (facing world +Y): a move along +Y is still forward.
    CHECK(approxEqual(displacementInRoom({0.0f, 2.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, 2.0f),
                      {0.0f, 0.0f, -1.0f}));
}

TEST_CASE("the anchor shifted by the body's move takes that move out of the head's room position") {
    RoomAnchor anchor;
    anchor.yaw = 0.7f;
    anchor.origin = {0.3f, 1.6f, -0.2f};
    const Pose head{Quat::identity(), {0.9f, 1.7f, -0.8f}};
    const Vec3 before = toRoom(anchor, head).position;
    const Vec3 delta{0.1f, 0.4f, -0.25f};
    const Vec3 after = toRoom(shiftedBy(anchor, delta), head).position;
    CHECK(approxEqual(after, before - Vec3{delta.x, 0.0f, delta.z}));
    CHECK(shiftedBy(anchor, delta).yaw == anchor.yaw);
    CHECK(shiftedBy(anchor, {NAN, 0.0f, 0.0f}).origin == anchor.origin);
}

TEST_CASE("steps of 10 and 20 cm close within 2 cm in half a second (40 cm in 0.7 s), without overshoot") {
    for (const float stepMetres : {0.10f, 0.20f, 0.40f}) {
        CAPTURE(stepMetres);
        Sim sim;
        sim.run(0.5); // past the start-up block
        sim.head = {0.0f, 0.0f, -stepMetres};
        double closed = -1.0;
        float past = 0.0f; // how far the body got past the head
        const double start = sim.seconds;
        while (sim.seconds < start + 3.0) {
            sim.tick();
            if (closed < 0.0 && flatLength(sim.head) <= 0.02f) {
                closed = sim.seconds - start;
            }
            past = std::max(past, sim.head.z);
        }
        CHECK(closed >= 0.0);
        CHECK(closed <= (stepMetres <= 0.2f ? 0.5 : 0.7));
        CHECK(past <= 0.02f);
        CHECK(flatLength(sim.head) <= 0.4f * sim.settings.deadzoneMetres + 0.005f);
        // The anchor took what the body moved, but for the last millimetre of a coast that outlasts the
        // resume delay.
        CHECK(approxEqual(sim.absorbedTotal, sim.movedTotal, 1e-3f));
        // Standing inside the deadzone never creeps.
        const Vec3 held = sim.head;
        sim.run(5.0);
        CHECK(approxEqual(sim.head, held, 1e-6f));
    }
}

TEST_CASE("a steady walk is tracked with a bounded lag") {
    Sim sim;
    sim.run(0.5);
    float maxGap = 0.0f;
    for (int i = 0; i < 1500; ++i) {
        sim.head = sim.head + Vec3{0.0f, 0.0f, -1.2f * static_cast<float>(kFrame)}; // 1.2 m/s
        sim.tick();
        if (i > 150) {
            maxGap = std::max(maxGap, flatLength(sim.head));
        }
    }
    CHECK(maxGap < 0.35f);
}

TEST_CASE("a wall stops the body and the gap stays as head offset") {
    Sim sim;
    sim.run(0.5);
    sim.reach = 0.0f;
    sim.head = {0.3f, 0.0f, 0.0f};
    sim.run(2.0);
    CHECK(approxEqual(sim.head, {0.3f, 0.0f, 0.0f}, 1e-6f));
    CHECK(approxEqual(sim.absorbedTotal, {}, 1e-6f));
}

TEST_CASE("the anchor takes the body's move only while follow commands, and no more than follow moves it") {
    BodyFollow follow(on());
    FollowTick t;
    t.gapRoom = {0.0f, 0.0f, -0.3f};
    t.commanded = true;
    t.displacement = Vec3{};
    for (int i = 0; i < 100; ++i) {
        t.seconds += kFrame;
        follow.update(t);
    }
    const auto at = [&](Vec3 d, bool commanded) {
        t.seconds += kFrame;
        t.displacement = d;
        t.commanded = commanded;
        return follow.update(t);
    };
    CHECK(approxEqual(at({0.0f, 0.0f, -0.01f}, true).absorbed, {0.0f, 0.0f, -0.01f}));
    CHECK(approxEqual(at({0.0f, 0.0f, -0.01f}, false).absorbed, {})); // the game moved it, not follow
    // Back and forth jitter nets out: moves away are taken too, and sliding along a wall.
    CHECK(approxEqual(at({0.0f, 0.0f, 0.001f}, true).absorbed, {0.0f, 0.0f, 0.001f}));
    CHECK(approxEqual(at({0.002f, 0.0f, 0.0f}, true).absorbed, {0.002f, 0.0f, 0.0f}));
    // Faster than follow moves the body: capped at 1.5 x 3 m/s for one frame plus 5 mm.
    const FollowStep capped = at({0.0f, 0.0f, -0.035f}, true);
    CHECK(capped.block == FollowBlock::None);
    CHECK(capped.absorbed.z == doctest::Approx(-(1.5f * 3.0f / 150.0f + 0.005f)));
}

TEST_CASE("every block stops the request and the anchor, then follow resumes after the delay") {
    const auto blocked = [](auto set, FollowBlock expected, Vec3 displacement = {0.0f, 0.0f, -0.01f}) {
        BodyFollow follow(on());
        FollowTick t;
        t.gapRoom = {0.0f, 0.0f, -0.3f};
        t.commanded = true;
        t.displacement = Vec3{};
        for (int i = 0; i < 100; ++i) {
            t.seconds += kFrame;
            follow.update(t);
        }
        t.seconds += kFrame;
        t.displacement = displacement;
        set(t);
        const FollowStep step = follow.update(t);
        CHECK(step.block == expected);
        CHECK(step.cause == expected);
        CHECK(approxEqual(step.absorbed, {}));
        CHECK_FALSE(step.request.engaged);
        // Clear again: settling for the resume delay, then following.
        t = FollowTick{t.seconds, {0.0f, 0.0f, -0.3f}, Vec3{}, 1.0f, true};
        t.seconds += kFrame;
        CHECK(follow.update(t).block == FollowBlock::Settling);
        for (int i = 0; i < 50; ++i) {
            t.seconds += kFrame;
            follow.update(t);
        }
        t.seconds += kFrame;
        const FollowStep resumed = follow.update(t);
        CHECK(resumed.block == FollowBlock::None);
        CHECK(resumed.request.engaged);
    };
    blocked([](FollowTick& t) { t.hookInstalled = false; }, FollowBlock::Off);
    blocked([](FollowTick& t) { t.anchored = false; }, FollowBlock::NotAnchored);
    blocked([](FollowTick& t) { t.seated = true; }, FollowBlock::Seated);
    blocked([](FollowTick& t) { t.menu = true; }, FollowBlock::Menu);
    blocked([](FollowTick& t) { t.cutscene = true; }, FollowBlock::Cutscene);
    blocked([](FollowTick& t) { t.forcedView = true; }, FollowBlock::Cutscene);
    blocked([](FollowTick& t) { t.stick = true; }, FollowBlock::Stick);
    blocked([](FollowTick& t) { t.jumpOrDash = true; }, FollowBlock::JumpOrDash);
    blocked([](FollowTick&) {}, FollowBlock::Airborne, {0.0f, 0.02f, -0.01f}); // 3 m/s up
    blocked([](FollowTick&) {}, FollowBlock::Fast, {0.0f, 0.0f, -0.05f});      // 7.5 m/s
    blocked([](FollowTick&) {}, FollowBlock::Teleport, {0.0f, 0.0f, -12.0f});  // a checkpoint
    blocked([](FollowTick& t) { t.displacement.reset(); }, FollowBlock::NoOrigin);
}

TEST_CASE("off when turned off, and the first frame has no displacement to follow") {
    BodyFollowSettings offSettings;
    offSettings.enabled = false;
    BodyFollow off(offSettings);
    FollowTick t;
    t.seconds = 1.0;
    t.gapRoom = {0.0f, 0.0f, -0.5f};
    t.displacement = Vec3{};
    CHECK(off.update(t).block == FollowBlock::Off);
    BodyFollow follow(on());
    CHECK(follow.update(t).block == FollowBlock::NoOrigin);
    // A long gap between frames (a menu without game views) is not a displacement either.
    t.seconds += 1.0;
    CHECK(follow.update(t).block == FollowBlock::NoOrigin);
}

TEST_CASE("a teleport never reaches the anchor, and the step after it is not followed at once") {
    Sim sim;
    sim.run(0.5);
    sim.head = {0.0f, 0.0f, -0.3f};
    sim.tick();
    sim.pending = {0.0f, 0.0f, -40.0f}; // the level moved the player
    const FollowStep step = sim.tick();
    CHECK(step.block == FollowBlock::Teleport);
    CHECK(approxEqual(step.absorbed, {}));
    CHECK(sim.tick().block == FollowBlock::Settling);
}
