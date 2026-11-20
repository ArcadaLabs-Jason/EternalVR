#include "features/comfort/ride.hpp"
#include "features/comfort/vignette.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace evr::comfort;

namespace {

constexpr double kFrame = 1.0 / 60.0;

RideSample onGround(float speed) {
    RideSample s;
    s.valid = true;
    s.onGround = true;
    s.carriedSpeed = speed;
    return s;
}

RideSample inAir(float speed = 0.0f) {
    RideSample s = onGround(speed);
    s.onGround = false;
    return s;
}

RideSample excluded(float speed) {
    RideSample s = onGround(speed);
    s.excluded = true;
    return s;
}

// Feeds `sample` every frame from `from` for `seconds`; returns the changes seen, in order, and leaves `now`
// at the last frame's time.
std::vector<RideChange> feed(RideDetector& ride, const RideSample& sample, double& now, double seconds) {
    std::vector<RideChange> changes;
    const double end = now + seconds;
    for (; now < end; now += kFrame) {
        const RideChange c = ride.update(sample, now);
        if (c != RideChange::None) {
            changes.push_back(c);
        }
    }
    return changes;
}

} // namespace

TEST_CASE("ride: the ground moving the player for half a second is a ride, which ends when it stops") {
    const RideTiming timing;
    RideDetector ride(timing);
    double now = 100.0;
    // Standing on a still floor (and walking on it: the floor's own speed stays 0): nothing.
    CHECK(feed(ride, onGround(0.0f), now, 5.0).empty());
    CHECK_FALSE(ride.riding());
    CHECK(ride.amount() == 0.0f);
    // An elevator starts at 6.4 m/s (e2m3's Phobos elevator: 160 m in 25 s).
    const double start = now;
    std::vector<RideChange> c = feed(ride, onGround(6.4f), now, timing.startSeconds - 0.1);
    CHECK(c.empty()); // not yet: a moment's motion is not a ride
    c = feed(ride, onGround(6.4f), now, 0.2);
    REQUIRE(c.size() == 1);
    CHECK(c[0] == RideChange::Started);
    CHECK(ride.riding());
    CHECK(ride.rideStart() == doctest::Approx(start));
    CHECK(ride.startSpeed() == doctest::Approx(6.4f));
    // The amount follows the speed on the move stick's scale.
    CHECK(ride.amount() == doctest::Approx((6.4f - 1.0f) / (6.5f - 1.0f)));
    CHECK(feed(ride, onGround(6.4f), now, 20.0).empty());
    // It slows: still a ride above the stop speed, the amount falling with it.
    CHECK(feed(ride, onGround(2.0f), now, 2.0).empty());
    CHECK(ride.amount() == doctest::Approx((2.0f - 1.0f) / 5.5f));
    CHECK(feed(ride, onGround(0.6f), now, 2.0).empty());
    CHECK(ride.riding());
    CHECK(ride.amount() == 0.0f);
    // Stopped: it ends after stopSeconds.
    c = feed(ride, onGround(0.0f), now, timing.stopSeconds + 0.1);
    REQUIRE(c.size() == 1);
    CHECK(c[0] == RideChange::Ended);
    CHECK(ride.lastEnd() == RideEnd::Stopped);
    CHECK(ride.peakSpeed() == doctest::Approx(6.4f));
    CHECK_FALSE(ride.riding());
    CHECK(ride.amount() == 0.0f);
}

TEST_CASE("ride: a fast tram gives the full amount, and a jump on it keeps the ride") {
    const RideTiming timing;
    RideDetector ride(timing);
    double now = 0.0;
    REQUIRE(feed(ride, onGround(20.0f), now, 1.0).size() == 1);
    CHECK(ride.amount() == 1.0f);
    // A jump (0.6 s in the air): the ride holds and so does its amount.
    CHECK(feed(ride, inAir(20.0f), now, 0.6).empty());
    CHECK(ride.riding());
    CHECK(ride.amount() == 1.0f);
    CHECK(feed(ride, onGround(20.0f), now, 1.0).empty());
    // Off the tram (a fall, the Meathook, a monkey bar) for longer than airSeconds: it ends.
    const std::vector<RideChange> c = feed(ride, inAir(), now, timing.airSeconds + 0.1);
    REQUIRE(c.size() == 1);
    CHECK(ride.lastEnd() == RideEnd::OffGround);
}

TEST_CASE("ride: none for motion off the ground, brief ground motion, or the excluded states") {
    const RideTiming timing;
    {
        // Falling, jumping, the Meathook, swinging: off the ground whatever the reading.
        RideDetector ride(timing);
        double now = 0.0;
        CHECK(feed(ride, inAir(15.0f), now, 5.0).empty());
    }
    {
        // A platform that moves for a moment under the player (shorter than startSeconds), again and again.
        RideDetector ride(timing);
        double now = 0.0;
        for (int i = 0; i < 10; ++i) {
            CHECK(feed(ride, onGround(5.0f), now, timing.startSeconds - 0.15).empty());
            CHECK(feed(ride, onGround(0.0f), now, 0.3).empty());
        }
        // Slower than the start speed, however long.
        CHECK(feed(ride, onGround(timing.startSpeed - 0.1f), now, 10.0).empty());
    }
    {
        // A cutscene, a climb, swimming, a piloted demon or death: never, and they end one at once.
        RideDetector ride(timing);
        double now = 0.0;
        CHECK(feed(ride, excluded(10.0f), now, 5.0).empty());
        REQUIRE(feed(ride, onGround(10.0f), now, 1.0).size() == 1);
        CHECK(ride.update(excluded(10.0f), now) == RideChange::Ended);
        CHECK(ride.lastEnd() == RideEnd::Excluded);
        CHECK(ride.amount() == 0.0f);
        // And the time before it does not count toward the next.
        now += kFrame;
        CHECK(feed(ride, onGround(10.0f), now, timing.startSeconds - 0.1).empty());
    }
}

TEST_CASE("ride: a lost reading or a gap in the frames ends it and starts over") {
    const RideTiming timing;
    RideDetector ride(timing);
    double now = 0.0;
    REQUIRE(feed(ride, onGround(8.0f), now, 1.0).size() == 1);
    RideSample bad = onGround(8.0f);
    bad.valid = false;
    CHECK(ride.update(bad, now) == RideChange::Ended);
    CHECK(ride.lastEnd() == RideEnd::Lost);
    REQUIRE(feed(ride, onGround(8.0f), now, 1.0).size() == 1);
    // A menu or a load: no frames for a second.
    now += 1.0;
    CHECK(ride.update(onGround(8.0f), now) == RideChange::Ended);
    CHECK(ride.lastEnd() == RideEnd::Lost);
    // The clock going back counts as a gap too.
    REQUIRE(feed(ride, onGround(8.0f), now, 1.0).size() == 1);
    CHECK(ride.update(onGround(8.0f), now - 5.0) == RideChange::Ended);
    // A time that is not finite changes nothing.
    CHECK(ride.update(onGround(8.0f), std::numeric_limits<double>::quiet_NaN()) == RideChange::None);
    // Not-finite speeds count as a still ground.
    RideDetector other(timing);
    double t = 0.0;
    CHECK(feed(other, onGround(std::numeric_limits<float>::quiet_NaN()), t, 3.0).empty());
    CHECK(feed(other, onGround(std::numeric_limits<float>::infinity()), t, 3.0).empty());
    // Either direction: a speed's sign does not matter.
    REQUIRE(feed(other, onGround(-6.0f), t, 1.0).size() == 1);
    other.reset();
    CHECK_FALSE(other.riding());
}

TEST_CASE("ride: bad timing values take their defaults") {
    RideTiming bad;
    bad.startSpeed = -1.0f;
    bad.stopSpeed = 50.0f; // above the start: both go back
    bad.startSeconds = std::numeric_limits<float>::quiet_NaN();
    bad.fullSpeed = 0.5f; // below its start: both go back
    RideDetector ride(bad);
    const RideTiming d;
    double now = 0.0;
    CHECK(feed(ride, onGround(d.startSpeed + 0.5f), now, d.startSeconds - 0.1).empty());
    CHECK(feed(ride, onGround(d.startSpeed + 0.5f), now, 0.2).size() == 1);
    CHECK(ride.amount() ==
          doctest::Approx((d.startSpeed + 0.5f - d.amountStartSpeed) / (d.fullSpeed - d.amountStartSpeed)));
    CHECK(std::string(rideEndName(RideEnd::Stopped)) == "the ride stopped");
}

namespace {

// Feeds `speed` metres per second of carrying along +y (and `own` of the player's own along +x, the body
// moving by both) for `seconds` at `fps`, the camera hook's two views of each tick (same origin) included;
// returns the last estimate.
float carry(CarriedSpeed& c,
            double& t,
            evr::Vec3& origin,
            evr::Vec3 carriedVelocity,
            evr::Vec3 own,
            double seconds,
            double fps = 75.0) {
    float last = -1.0f;
    const double dt = 1.0 / fps;
    for (const double end = t + seconds; t < end; t += dt) {
        origin = origin + (carriedVelocity + own) * static_cast<float>(dt);
        last = c.add(t, origin, own);
        last = c.add(t + 0.002, origin, own); // the other eye's view of the same tick
    }
    return last;
}

} // namespace

TEST_CASE("carried speed: the body's motion less the player's own") {
    using evr::Vec3;
    CarriedSpeed c;
    double t = 0.0;
    Vec3 origin{100.0f, 200.0f, 5.0f};
    // Standing still: nothing; the first samples give no estimate until the window is long enough.
    CHECK(c.add(t, origin, {}) < 0.0f);
    CHECK(std::fabs(carry(c, t, origin, {}, {}, 1.0)) < 0.15f);
    // Running at 9.5 m/s: the player's own, nothing carried.
    CHECK(std::fabs(carry(c, t, origin, {}, {9.5f, 0.0f, 0.0f}, 1.0)) < 0.15f);
    // An elevator going down 6.4 m/s with the player's own velocity 0 (rig: e2m3's Phobos elevator).
    CHECK(carry(c, t, origin, {0.0f, 0.0f, -6.4f}, {}, 1.0) == doctest::Approx(6.4f).epsilon(0.02));
    // A tram at 20 m/s with the player walking along it at 3 m/s: the tram's.
    CHECK(carry(c, t, origin, {0.0f, 20.0f, 0.0f}, {0.0f, 3.0f, 0.0f}, 1.0) ==
          doctest::Approx(20.0f).epsilon(0.02));
    // Stairs at a run: the body lifted with no upward velocity of its own counts for nothing.
    CHECK(std::fabs(carry(c, t, origin, {0.0f, 0.0f, 4.0f}, {9.5f, 0.0f, 0.0f}, 1.0)) < 0.15f);
    // But a lift going up with the player standing on it does.
    CHECK(carry(c, t, origin, {0.0f, 0.0f, 4.0f}, {}, 1.0) == doctest::Approx(4.0f).epsilon(0.02));
}

TEST_CASE("carried speed: teleports, gaps, a clock going back and bad values start over") {
    using evr::Vec3;
    CarriedSpeed c;
    double t = 0.0;
    Vec3 origin{};
    carry(c, t, origin, {}, {}, 1.0);
    // A teleport of 30 m: no estimate (not a 30 m step's speed), then normal again once the window refills.
    origin = origin + Vec3{30.0f, 0.0f, 0.0f};
    CHECK(c.add(t, origin, {}) < 0.0f);
    t += 0.01;
    CHECK(carry(c, t, origin, {}, {}, 0.1) < 0.0f);
    CHECK(std::fabs(carry(c, t, origin, {}, {}, 0.3)) < 0.15f);
    // A gap (a load, a menu): over.
    t += 1.0;
    CHECK(c.add(t, origin, {}) < 0.0f);
    // The clock going back, and values that are not finite.
    carry(c, t, origin, {}, {}, 0.5);
    CHECK(c.add(t - 1.0, origin, {}) < 0.0f);
    carry(c, t, origin, {}, {}, 0.5);
    CHECK(c.add(t, {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}, {}) < 0.0f);
    CHECK(c.add(std::numeric_limits<double>::infinity(), origin, {}) < 0.0f);
    c.reset();
    CHECK(c.add(t, origin, {}) < 0.0f);
}

namespace {

// A RideWatch fed every frame at `fps` (two views of each tick), the body moving by `body` per game second
// and its own velocity `own`, `gameRate` game seconds per wall second (1 normally; the weapon wheel's slow
// motion below 1). Counts the rides started.
struct Feed {
    RideWatch watch;
    double t = 0.0;
    evr::Vec3 origin{10.0f, 20.0f, 0.0f};
    int started = 0;
    void run(evr::Vec3 body,
             evr::Vec3 own,
             double seconds,
             bool excluded = false,
             double fps = 75.0,
             float gameRate = 1.0f) {
        const double dt = 1.0 / fps;
        for (const double end = t + seconds; t < end; t += dt) {
            origin = origin + body * (static_cast<float>(dt) * gameRate);
            for (const double view : {0.0, 0.002}) {
                RideFrame f;
                f.valid = true;
                f.excluded = excluded;
                f.onGround = true;
                f.origin = origin;
                f.ownVelocity = own;
                started += watch.update(f, t + view) == RideChange::Started ? 1 : 0;
            }
        }
    }
};

} // namespace

TEST_CASE("ride watch: the elevator and the tram are rides, walking is not") {
    using evr::Vec3;
    Feed f;
    f.run({9.5f, 0.0f, 0.0f}, {9.5f, 0.0f, 0.0f}, 5.0);
    f.run({0.0f, -9.5f, 0.0f}, {0.0f, -9.5f, 0.0f}, 2.0);
    CHECK(f.started == 0);
    // The Phobos elevator, the player standing: 6.4 m/s down.
    f.run({0.0f, 0.0f, -6.4f}, {}, 2.0);
    CHECK(f.started == 1);
    CHECK(f.watch.ride().riding());
    f.run({}, {}, 2.0);
    CHECK_FALSE(f.watch.ride().riding());
    CHECK(f.watch.ride().lastEnd() == RideEnd::Stopped);
    // A tram at 20 m/s, the player walking across it at 3 m/s: still a ride.
    f.run({3.0f, 20.0f, 0.0f}, {3.0f, 0.0f, 0.0f}, 2.0);
    CHECK(f.started == 2);
    CHECK(f.watch.carried() > 15.0f);
}

TEST_CASE("ride watch: own motion the body does not follow is never carried") {
    using evr::Vec3;
    {
        // Running into a wall (or a corner, a stuck spot): 9 m/s of own velocity, the body still.
        Feed f;
        f.run({}, {9.0f, 0.0f, 0.0f}, 5.0);
        CHECK(f.started == 0);
        CHECK(f.watch.carried() == 0.0f);
        // Sliding along a wall at 45 degrees: the body slower than its own velocity, sideways.
        f.run({4.5f, 4.5f, 0.0f}, {9.0f, 0.0f, 0.0f}, 5.0);
        CHECK(f.started == 0);
    }
    {
        // The weapon wheel's slow motion: the game moves the body a tenth as fast as its velocity says in
        // wall time, running and standing nearly still alike.
        Feed f;
        f.run({9.5f, 0.0f, 0.0f}, {9.5f, 0.0f, 0.0f}, 4.0, false, 75.0, 0.1f);
        f.run({0.8f, 0.0f, 0.0f}, {0.8f, 0.0f, 0.0f}, 4.0, false, 75.0, 0.1f);
        CHECK(f.started == 0);
    }
    {
        // Stalled frames at a run: 0.3 s without a frame (the body moved one tick meanwhile), again and
        // again, and just over 0.4 s (the window starts over).
        Feed f;
        for (int i = 0; i < 6; ++i) {
            f.run({9.5f, 0.0f, 0.0f}, {9.5f, 0.0f, 0.0f}, 0.3);
            f.t += 0.3;
        }
        f.run({9.5f, 0.0f, 0.0f}, {9.5f, 0.0f, 0.0f}, 0.3);
        f.t += 0.41;
        f.run({9.5f, 0.0f, 0.0f}, {9.5f, 0.0f, 0.0f}, 1.0);
        CHECK(f.started == 0);
    }
    {
        // Knockback: the own velocity jumping to 15 m/s for a few frames and back, the body following a frame
        // late, again and again.
        Feed f;
        for (int i = 0; i < 20; ++i) {
            f.run({}, {15.0f, 0.0f, 0.0f}, 0.04);
            f.run({15.0f, 0.0f, 0.0f}, {}, 0.04);
            f.run({}, {}, 0.1);
        }
        CHECK(f.started == 0);
    }
}

TEST_CASE("ride watch: teleports under 20 m are no ride, at low and normal frame rates") {
    using evr::Vec3;
    for (const double fps : {20.0, 30.0, 75.0}) {
        Feed f;
        f.run({}, {}, 1.0, false, fps);
        for (int i = 0; i < 5; ++i) {
            f.origin = f.origin + Vec3{15.0f, 0.0f, 0.0f};
            f.run({}, {}, 1.0, false, fps);
            f.origin = f.origin + Vec3{0.0f, 0.0f, 19.0f};
            f.run({9.5f, 0.0f, 0.0f}, {9.5f, 0.0f, 0.0f}, 1.0, false, fps);
        }
        CHECK(f.started == 0);
    }
}

TEST_CASE("ride watch: motion under an excluded state counts for nothing after it") {
    using evr::Vec3;
    Feed f;
    // A cutscene (or a death camera, a climb) glides the body 10 m/s with no own velocity: excluded.
    f.run({10.0f, 0.0f, 0.0f}, {}, 3.0, true);
    CHECK(f.started == 0);
    CHECK(f.watch.carried() == 0.0f);
    // It ends with the player standing: the glide before it is not carried into the new window.
    f.run({}, {}, 2.0);
    CHECK(f.started == 0);
    // A ride under way ends at an excluded frame, and a new one needs its own half second after it.
    f.run({0.0f, 0.0f, -6.4f}, {}, 1.0);
    CHECK(f.started == 1);
    f.run({0.0f, 0.0f, -6.4f}, {}, 0.1, true);
    CHECK_FALSE(f.watch.ride().riding());
    CHECK(f.watch.ride().lastEnd() == RideEnd::Excluded);
    f.run({0.0f, 0.0f, -6.4f}, {}, 0.5);
    CHECK(f.started == 1); // the window refills (0.2 s) and the start takes 0.5 s more
    f.run({0.0f, 0.0f, -6.4f}, {}, 0.5);
    CHECK(f.started == 2);
    f.watch.reset();
    CHECK(f.watch.carried() == 0.0f);
}

TEST_CASE("vignette: a ride is a target like the stick's") {
    const VignetteTiming timing;
    VignetteMotion m;
    m.ride = 0.5f;
    CHECK(vignetteTarget(m, timing) == doctest::Approx(0.5f));
    m.ride = 2.0f;
    CHECK(vignetteTarget(m, timing) == 1.0f);
    m.ride = std::numeric_limits<float>::quiet_NaN();
    CHECK(vignetteTarget(m, timing) == 0.0f);
    // It comes in at the policy's pace, not at once.
    VignettePolicy policy(timing);
    m.ride = 1.0f;
    const float first = policy.update(m, kFrame);
    CHECK(first > 0.0f);
    CHECK(first < 0.2f);
}
