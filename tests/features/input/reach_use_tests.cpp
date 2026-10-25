#include "features/input/reach_use.hpp"

#include "features/input/hand_motion.hpp"
#include "features/input/input_frames.hpp"

#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <numbers>
#include <vector>

using evr::input::Hand;
using evr::input::InputFrame;
using evr::input::ReachOutput;
using evr::input::ReachSettings;
using evr::input::ReachUse;
using evr::test::HandMotion;
using evr::test::HeadPoint;
using evr::test::kMotionFrame;
using evr::test::kSeatedEyes;
using evr::test::kStandingEyes;

namespace {

ReachSettings reachOn() {
    ReachSettings s;
    s.enabled = true;
    return s;
}

// Where the hands start: held ready in front of the body, inside the re-arm distance. `side` is +1 for the
// right hand, -1 for the left.
HeadPoint ready(float side) {
    return {0.15f * side, -0.35f, 0.2f};
}

// A switch at arm's length, a little below the eyes.
HeadPoint target(float side, float up = -0.15f, float forward = 0.62f) {
    return {0.1f * side, up, forward};
}

// A deliberate reach of `hand`: out to the switch, a moment there, back to ready.
HandMotion reachMotion(Hand hand, float eyes = kStandingEyes, float yaw = 0.0f, HeadPoint to = target(1.0f)) {
    const float side = hand == Hand::Right ? 1.0f : -1.0f;
    HandMotion m(eyes, yaw, ready(-1.0f), ready(1.0f));
    m.hold(0.2f);
    to.right *= side;
    m.move(hand, to, 0.45f).hold(0.1f).move(hand, ready(side), 0.4f).hold(0.3f);
    return m;
}

struct Presses {
    int count = 0;
    std::array<int, 2> byHand{};
    std::vector<std::size_t> frames; // the frame indices of the presses
};

Presses play(ReachUse& reach, const std::vector<InputFrame>& frames, std::array<bool, 2> primed = {}) {
    Presses p;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const ReachOutput out = reach.update(frames[i], kMotionFrame, primed);
        if (out.reached) {
            ++p.count;
            p.byHand[0] += out.hand[0] ? 1 : 0;
            p.byHand[1] += out.hand[1] ? 1 : 0;
            p.frames.push_back(i);
        }
    }
    return p;
}

Presses play(const std::vector<InputFrame>& frames, ReachSettings settings = reachOn()) {
    ReachUse reach(settings);
    return play(reach, frames);
}

} // namespace

TEST_CASE("reach to use is off by default") {
    ReachUse reach;
    CHECK_FALSE(reach.settings().enabled);
    CHECK(play(reach, reachMotion(Hand::Right).frames).count == 0);
}

TEST_CASE("a hand reached out and pulled back presses use once, on the way back") {
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        const HandMotion m = reachMotion(hand);
        const Presses p = play(m.frames);
        CHECK(p.count == 1);
        CHECK(p.byHand[static_cast<std::size_t>(hand)] == 1);
        REQUIRE(p.frames.size() == 1);
        // The press is on the way back: the hand is moving back along the heading (+Z here).
        const InputFrame& pressed = m.frames[p.frames[0]];
        CHECK(pressed.hand(hand).linearVelocity.z > 0.0f);
    }
}

TEST_CASE("use stays down for 0.3 s after a reach, as a button pressed by hand") {
    const HandMotion m = reachMotion(Hand::Right);
    ReachUse reach(reachOn());
    int down = 0;
    int presses = 0;
    bool wasDown = false;
    for (const InputFrame& frame : m.frames) {
        const ReachOutput out = reach.update(frame, kMotionFrame);
        presses += out.reached ? 1 : 0;
        CHECK((out.reached ? out.useDown : true));
        down += out.useDown ? 1 : 0;
        CHECK_FALSE((out.useDown && !wasDown && !out.reached)); // it goes down only with the press
        wasDown = out.useDown;
    }
    CHECK(presses == 1);
    CHECK(down >= 27); // 0.3 s at 90 Hz
    CHECK(down <= 28);
}

TEST_CASE("a reach presses seated, in any facing, and to a low or a high switch") {
    for (const float eyes : {kStandingEyes, kSeatedEyes}) {
        for (const float yaw : {0.0f, 1.2f, -2.5f}) {
            for (const float up : {-0.4f, -0.15f, 0.1f}) {
                CAPTURE(eyes);
                CAPTURE(yaw);
                CAPTURE(up);
                CHECK(play(reachMotion(Hand::Right, eyes, yaw, target(1.0f, up)).frames).count == 1);
                CHECK(play(reachMotion(Hand::Left, eyes, yaw, target(1.0f, up)).frames).count == 1);
            }
        }
    }
}

// The proof the reach does not take aiming for a use: the weapon raised from the hip (or the lap) to aim,
// at several extensions and speeds, held there, and lowered again, with the other hand coming up to steady
// it or staying down; left- and right-handed, standing and seated. None presses use, while the same tracker
// presses for a deliberate reach (positive control).
TEST_CASE("raising the weapon to aim never presses use, standing or seated, either hand") {
    struct Rest {
        float eyes;
        HeadPoint weapon; // the weapon hand's rest, `right` towards its side
    };
    const Rest rests[] = {
        {kStandingEyes, {0.22f, -0.72f, -0.02f}}, // standing, arms hanging by the hips
        {kStandingEyes, {0.2f, -0.55f, 0.12f}},   // standing, gun held low in front
        {kSeatedEyes, {0.18f, -0.45f, 0.25f}},    // seated, hands on the lap
        {kSeatedEyes, {0.25f, -0.55f, 0.0f}},     // seated, arms by the chair
    };
    int raises = 0;
    for (const Hand weapon : {Hand::Right, Hand::Left}) {
        const float side = weapon == Hand::Right ? 1.0f : -1.0f;
        for (const Rest& rest : rests) {
            for (const float extent : {0.45f, 0.55f, 0.65f}) {
                for (const float seconds : {0.25f, 0.4f, 0.6f}) {
                    for (const bool twoHanded : {false, true}) {
                        CAPTURE(side);
                        CAPTURE(rest.eyes);
                        CAPTURE(rest.weapon.forward);
                        CAPTURE(extent);
                        CAPTURE(seconds);
                        CAPTURE(twoHanded);
                        const HeadPoint weaponRest{rest.weapon.right * side, rest.weapon.up,
                                                   rest.weapon.forward};
                        const HeadPoint otherRest{-rest.weapon.right * side, rest.weapon.up,
                                                  rest.weapon.forward};
                        const HeadPoint aim{0.12f * side, -0.22f, extent};
                        const HeadPoint support{0.02f * side, -0.28f, extent - 0.08f};
                        HandMotion m(rest.eyes, 0.0f, weapon == Hand::Left ? weaponRest : otherRest,
                                     weapon == Hand::Right ? weaponRest : otherRest);
                        m.hold(0.3f);
                        if (twoHanded) {
                            m.both(weapon == Hand::Left ? aim : support,
                                   weapon == Hand::Right ? aim : support, seconds);
                        } else {
                            m.move(weapon, aim, seconds);
                        }
                        // Aims for a while, then lowers the weapon (and the other hand) to rest.
                        m.hold(2.0f);
                        m.both(weapon == Hand::Left ? weaponRest : otherRest,
                               weapon == Hand::Right ? weaponRest : otherRest, 0.5f);
                        m.hold(0.3f);
                        // Raised and lowered at once (a look along the sights, no shot): the arm drops from
                        // the shoulder, so the hand falls before it comes back.
                        m.move(weapon, aim, seconds).hold(0.2f);
                        m.move(weapon, {aim.right, aim.up - 0.3f, aim.forward - 0.08f}, 0.3f);
                        m.move(weapon, weaponRest, 0.3f).hold(0.3f);
                        CHECK(play(m.frames).count == 0);
                        ++raises;
                    }
                }
            }
        }
    }
    CHECK(raises == 144);
    // Positive control: the same tracker presses for a reach from each rest.
    for (const Rest& rest : rests) {
        HandMotion m(rest.eyes, 0.0f, {-rest.weapon.right, rest.weapon.up, rest.weapon.forward}, rest.weapon);
        m.hold(0.3f).move(Hand::Right, {0.15f, -0.35f, 0.2f}, 0.4f).hold(0.2f);
        m.move(Hand::Right, target(1.0f), 0.45f).hold(0.1f).move(Hand::Right, ready(1.0f), 0.4f).hold(0.3f);
        CAPTURE(rest.weapon.forward);
        CHECK(play(m.frames).count == 1);
    }
}

TEST_CASE(
    "the known limit: a weapon raised and pulled straight back at once, without a shot, reads as a reach") {
    // Raised to aim, held 0.2 s, pulled straight back towards the chest (not dropped): within the out time
    // (0.8 s) this is a reach's shape. A shot (the trigger) or a longer aim rules it out.
    HandMotion m(kStandingEyes, 0.0f, ready(-1.0f), {0.2f, -0.55f, 0.12f});
    m.hold(0.3f).move(Hand::Right, {0.12f, -0.22f, 0.55f}, 0.4f).hold(0.2f);
    m.move(Hand::Right, {0.15f, -0.3f, 0.2f}, 0.4f).hold(0.3f);
    CHECK(play(m.frames).count == 1);
    HandMotion aimed(kStandingEyes, 0.0f, ready(-1.0f), {0.2f, -0.55f, 0.12f});
    aimed.hold(0.3f).move(Hand::Right, {0.12f, -0.22f, 0.55f}, 0.4f).hold(0.9f);
    aimed.move(Hand::Right, {0.15f, -0.3f, 0.2f}, 0.4f).hold(0.3f);
    CHECK(play(aimed.frames).count == 0);
}

TEST_CASE("a hand held out ahead, then pulled back, is not a reach") {
    HandMotion m(kStandingEyes, 0.0f, ready(-1.0f), ready(1.0f));
    m.hold(0.2f).move(Hand::Right, target(1.0f), 0.45f).hold(1.0f).move(Hand::Right, ready(1.0f), 0.4f);
    CHECK(play(m.frames).count == 0);
}

TEST_CASE("a hand short of the reach distance, too slow or too fast does not press") {
    // Short: out to 0.4 m and back.
    HandMotion shortReach(kStandingEyes, 0.0f, ready(-1.0f), ready(1.0f));
    shortReach.hold(0.2f)
        .move(Hand::Right, target(1.0f, -0.15f, 0.4f), 0.4f)
        .move(Hand::Right, ready(1.0f), 0.4f);
    CHECK(play(shortReach.frames).count == 0);
    // Creeping out over 3 s (forward speed well under 0.6 m/s), then back.
    HandMotion creep(kStandingEyes, 0.0f, ready(-1.0f), ready(1.0f));
    creep.hold(0.2f).move(Hand::Right, target(1.0f), 3.0f).move(Hand::Right, ready(1.0f), 0.4f);
    CHECK(play(creep.frames).count == 0);
    // A punch's speed on the way out (melee already presses use).
    HandMotion jab(kStandingEyes, 0.0f, ready(-1.0f), ready(1.0f));
    jab.hold(0.2f).move(Hand::Right, target(1.0f), 0.12f).move(Hand::Right, ready(1.0f), 0.3f);
    CHECK(play(jab.frames).count == 0);
    // Out in two movements with a pause between (an aim settling forward): no single reach.
    HandMotion twoStep(kStandingEyes, 0.0f, ready(-1.0f), ready(1.0f));
    twoStep.hold(0.2f).move(Hand::Right, target(1.0f, -0.15f, 0.42f), 0.4f).hold(0.3f);
    twoStep.move(Hand::Right, target(1.0f), 0.6f).move(Hand::Right, ready(1.0f), 0.4f);
    CHECK(play(twoStep.frames).count == 0);
}

TEST_CASE("a pulled trigger during the reach ends it") {
    HandMotion m = reachMotion(Hand::Right);
    for (std::size_t i = 30; i < 40; ++i) {
        m.frames[i].right.trigger = 0.9f; // fired on the way out
    }
    CHECK(play(m.frames).count == 0);
}

TEST_CASE("a hand primed for a throw or a swing does not reach") {
    ReachUse reach(reachOn());
    CHECK(play(reach, reachMotion(Hand::Right).frames, {false, true}).count == 0);
    ReachUse other(reachOn());
    CHECK(play(other, reachMotion(Hand::Right).frames, {true, false}).count == 1);
}

TEST_CASE("the hand has to come back before it reaches again, and not within the cooldown") {
    // Out, back part of the way (a press), out again without coming back inside the re-arm distance, back:
    // one press.
    HandMotion partial(kStandingEyes, 0.0f, ready(-1.0f), ready(1.0f));
    partial.hold(0.2f)
        .move(Hand::Right, target(1.0f), 0.45f)
        .move(Hand::Right, target(1.0f, -0.15f, 0.4f), 0.3f);
    partial.move(Hand::Right, target(1.0f), 0.4f).move(Hand::Right, ready(1.0f), 0.4f).hold(0.2f);
    CHECK(play(partial.frames).count == 1);
    // The same hand again after it came back: a second press.
    HandMotion twice = reachMotion(Hand::Right);
    twice.move(Hand::Right, target(1.0f), 0.45f).hold(0.1f).move(Hand::Right, ready(1.0f), 0.4f).hold(0.3f);
    CHECK(play(twice.frames).count == 2);
    // Both hands at once: one press, the cooldown (0.5 s) holds the other back.
    HandMotion both(kStandingEyes, 0.0f, ready(-1.0f), ready(1.0f));
    both.hold(0.2f).both(target(-1.0f), target(1.0f), 0.45f).hold(0.1f).both(ready(-1.0f), ready(1.0f), 0.4f);
    CHECK(play(both.frames).count == 1);
}

TEST_CASE("a hand tracked or resumed out ahead has to come back first") {
    HandMotion m = reachMotion(Hand::Right);
    // Tracking lost on the way out.
    for (std::size_t i = 35; i < 40; ++i) {
        m.frames[i].right.poseValid = false;
    }
    CHECK(play(m.frames).count == 0);
    // Tracking starting with the hand already out there.
    HandMotion out(kStandingEyes, 0.0f, ready(-1.0f), target(1.0f));
    out.hold(0.1f).move(Hand::Right, ready(1.0f), 0.4f);
    CHECK(play(out.frames).count == 0);
}

TEST_CASE("an untracked head or velocity presses nothing") {
    HandMotion noHead = reachMotion(Hand::Right);
    for (InputFrame& frame : noHead.frames) {
        frame.head.poseValid = false;
    }
    CHECK(play(noHead.frames).count == 0);
    HandMotion noVelocity = reachMotion(Hand::Right);
    for (InputFrame& frame : noVelocity.frames) {
        frame.right.velocityValid = false;
    }
    CHECK(play(noVelocity.frames).count == 0);
    HandMotion nan = reachMotion(Hand::Right);
    for (InputFrame& frame : nan.frames) {
        frame.right.linearVelocity.z = std::numeric_limits<float>::quiet_NaN();
    }
    CHECK(play(nan.frames).count == 0);
}

TEST_CASE("while the gestures are held back nothing presses, and the hand has to come back afterwards") {
    const HandMotion m = reachMotion(Hand::Right);
    ReachUse reach(reachOn());
    int presses = 0;
    for (std::size_t i = 0; i < m.frames.size(); ++i) {
        // Held back for a few frames while the hand is on its way out (a popup, the weapon wheel).
        const bool blocked = i >= 38 && i < 42;
        presses += reach.update(m.frames[i], kMotionFrame, {}, blocked).reached ? 1 : 0;
    }
    CHECK(presses == 0);
    // Held back throughout: nothing.
    ReachUse held(reachOn());
    for (const InputFrame& frame : m.frames) {
        CHECK_FALSE(held.update(frame, kMotionFrame, {}, true).reached);
    }
}

TEST_CASE("looking straight down, the reach is still along the heading") {
    HandMotion m = reachMotion(Hand::Right);
    // Pitched 90 degrees down: forward is -Y, and the head's up points along -Z (the heading).
    for (InputFrame& frame : m.frames) {
        frame.head.pose.orientation =
            evr::Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, -std::numbers::pi_v<float> / 2.0f);
    }
    CHECK(play(m.frames).count == 1);
}

TEST_CASE("tuning values that are unusable fall back to the defaults") {
    ReachSettings bad = reachOn();
    bad.reachDistance = std::numeric_limits<float>::quiet_NaN();
    bad.minSpeed = -4.0f;
    bad.withdrawDistance = 2.0f;
    bad.dropAllowance = std::numeric_limits<float>::infinity();
    bad.maxOutSeconds = 0.0f;
    bad.maxTrigger = 7.0f;
    bad.cooldownSeconds = std::numeric_limits<float>::infinity();
    bad.holdSeconds = -1.0f;
    const ReachSettings fixed = ReachUse(bad).settings();
    const ReachSettings defaults;
    CHECK(fixed.enabled);
    CHECK(fixed.reachDistance == defaults.reachDistance);
    CHECK(fixed.minSpeed == defaults.minSpeed);
    CHECK(fixed.withdrawDistance == defaults.withdrawDistance);
    CHECK(fixed.dropAllowance == defaults.dropAllowance);
    CHECK(fixed.maxOutSeconds == defaults.maxOutSeconds);
    CHECK(fixed.maxTrigger == defaults.maxTrigger);
    CHECK(fixed.cooldownSeconds == defaults.cooldownSeconds);
    CHECK(fixed.holdSeconds == defaults.holdSeconds);
}
