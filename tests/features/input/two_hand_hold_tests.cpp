#include "features/input/two_hand.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>

using evr::Quat;
using evr::Vec3;
using evr::input::kTwoHandRampSeconds;
using evr::input::SupportRow;
using evr::input::TwoHandBlock;
using evr::input::TwoHandEnd;
using evr::input::TwoHandFrame;
using evr::input::TwoHandHold;
using evr::input::TwoHandResult;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kDt = 1.0f / 90.0f;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

// The combat shotgun's and the Rocket Launcher's rows (data/weapons/viewmodel_offsets.toml).
constexpr SupportRow kShotgun{0.296f, 0.006f, 0.057f};
constexpr SupportRow kRocket{0.163f, 0.003f, -0.004f};
// A neutral weapon hand: 0.2 m right, 0.3 m below and 0.4 m ahead of the head, pointing ahead (-Z).
constexpr Vec3 kGrip{0.2f, -0.3f, -0.4f};

float degrees(float radians) {
    return radians * 180.0f / kPi;
}

// Angles by atan2 (acos loses everything below a few hundredths of a degree in float).
float angleBetween(Vec3 a, Vec3 b) {
    return degrees(std::atan2(evr::length(evr::cross(a, b)), evr::dot(a, b)));
}

float angleBetween(Quat a, Quat b) {
    const Quat d = a * evr::conjugate(b);
    return degrees(2.0f * std::atan2(evr::length(Vec3{d.x, d.y, d.z}), std::fabs(d.w)));
}

TwoHandFrame frame(Vec3 offGrip, float grip, SupportRow row = kShotgun) {
    TwoHandFrame f;
    f.block = TwoHandBlock::None;
    f.offGrip = grip;
    f.weaponTracked = true;
    f.offTracked = true;
    f.weaponGrip = kGrip;
    f.offGripPoint = offGrip;
    f.support = row;
    f.dt = kDt;
    return f;
}

Vec3 support(Quat aim = {}, SupportRow row = kShotgun) {
    return evr::input::supportPoint(kGrip, aim, row);
}

// The off hand brought to `off` with its grip up, then squeezed there.
TwoHandResult grab(TwoHandHold& hold, Vec3 off, SupportRow row = kShotgun) {
    hold.update(frame(off, 0.0f, row));
    return hold.update(frame(off, 1.0f, row));
}

// The same frame again until the ease is over (at most a second).
TwoHandResult settle(TwoHandHold& hold, const TwoHandFrame& f) {
    TwoHandResult r = hold.update(f);
    for (int i = 0; i < 90 && r.easing; ++i) {
        r = hold.update(f);
    }
    return r;
}

// Frames an ease takes at kDt.
const int kEaseFrames = static_cast<int>(std::ceil(kTwoHandRampSeconds / kDt));

} // namespace

TEST_CASE("two-handed hold: engages on a squeeze within the capture radius only") {
    TwoHandHold hold;
    // Near the support point, the grip not pressed: capturable (the mapper holds the grip back), not held.
    auto r = hold.update(frame(support() + Vec3{0.05f, 0.0f, 0.0f}, 0.0f));
    CHECK(r.capturable);
    CHECK_FALSE(r.engaged);
    CHECK(r.distance == doctest::Approx(0.05f).epsilon(1e-3));
    // Squeezed beyond the capture radius: neither.
    r = hold.update(frame(support() + Vec3{0.09f, 0.0f, 0.0f}, 1.0f));
    CHECK_FALSE(r.capturable);
    CHECK_FALSE(r.engaged);
    // Brought in with that squeeze still held: it was the binding's, so it takes nothing, and the mapper is
    // not told to hold the binding back.
    const Vec3 off = support() + Vec3{0.07f, 0.0f, 0.0f};
    r = hold.update(frame(off, 1.0f));
    CHECK_FALSE(r.capturable);
    CHECK_FALSE(r.engaged);
    // Let go there and squeezed again: held from that frame, the turn easing in toward the line.
    r = grab(hold, off);
    CHECK(r.engaged);
    CHECK(r.started);
    CHECK(r.easing);
    CHECK(angleBetween(support(r.target) - kGrip, off - kGrip) < 0.01f);
    CHECK(angleBetween(r.aim, Quat{}) > 0.0f);
    CHECK(angleBetween(r.aim, Quat{}) < angleBetween(r.target, Quat{}));
    r = settle(hold, frame(off, 1.0f));
    CHECK(r.aim == r.target);
    CHECK(angleBetween(support(r.aim) - kGrip, off - kGrip) < 0.01f);
}

TEST_CASE("two-handed hold: the ease ends exactly on the two-handed aim, and exactly on the weapon hand's") {
    TwoHandHold hold;
    const Vec3 off = support() + Vec3{0.07f, 0.03f, 0.0f};
    auto r = grab(hold, off);
    REQUIRE(r.engaged);
    const float full = angleBetween(r.target, Quat{});
    REQUIRE(full > 5.0f);
    float last = angleBetween(r.aim, Quat{});
    int frames = 1;
    while (r.easing) {
        r = hold.update(frame(off, 1.0f));
        const float now = angleBetween(r.aim, Quat{});
        CHECK(now >= last); // only ever toward the line
        last = now;
        ++frames;
        REQUIRE(frames <= kEaseFrames + 1);
    }
    CHECK(frames >= kEaseFrames - 1);
    CHECK(r.aim == r.target);

    // Let go: the turn eases out over the same time, and ends on the weapon hand's own aim exactly.
    r = hold.update(frame(off, 0.0f));
    CHECK_FALSE(r.engaged);
    CHECK(r.ended == TwoHandEnd::GripLetGo);
    CHECK(r.easing);
    CHECK(angleBetween(r.aim, Quat{}) < full);
    CHECK(angleBetween(r.aim, Quat{}) > 0.0f);
    last = angleBetween(r.aim, Quat{});
    frames = 1;
    while (r.easing) {
        r = hold.update(frame(off, 0.0f));
        const float now = angleBetween(r.aim, Quat{});
        CHECK(now <= last);
        last = now;
        ++frames;
        REQUIRE(frames <= kEaseFrames + 1);
    }
    CHECK(frames >= kEaseFrames - 1);
    CHECK(r.aim == Quat{});
}

TEST_CASE("two-handed hold: a grab during the release's ease does not jump") {
    TwoHandHold hold;
    const Vec3 right = support() + Vec3{0.07f, 0.0f, 0.0f};
    const Vec3 left = support() + Vec3{-0.07f, 0.0f, 0.0f};
    REQUIRE(grab(hold, right).engaged);
    auto r = settle(hold, frame(right, 1.0f));
    Quat prev = r.aim;
    float largest = 0.0f;
    // Half way through the release's ease, the off hand squeezes again on the other side of the gun.
    for (int i = 0; i < kEaseFrames / 2; ++i) {
        r = hold.update(frame(left, 0.0f));
        REQUIRE(r.easing);
        largest = std::fmax(largest, angleBetween(r.aim, prev));
        prev = r.aim;
    }
    REQUIRE(angleBetween(r.aim, Quat{}) > 3.0f); // still turned toward the old line
    r = hold.update(frame(left, 1.0f));
    REQUIRE(r.started);
    // From where the release's ease had got to: a step no larger than the eases' own.
    CHECK(angleBetween(r.aim, prev) < 2.0f);
    prev = r.aim;
    while (r.easing) {
        r = hold.update(frame(left, 1.0f));
        largest = std::fmax(largest, angleBetween(r.aim, prev));
        prev = r.aim;
    }
    CHECK(largest < 8.0f);
    CHECK(r.aim == r.target);
    CHECK(angleBetween(support(r.aim) - kGrip, left - kGrip) < 0.01f);
}

TEST_CASE("two-handed hold: slides along the gun up to the release radius, then lets go") {
    TwoHandHold hold;
    REQUIRE(grab(hold, support()).engaged);
    // Slid 15 cm forward along the line (past the capture radius, inside the release radius): still held.
    const Vec3 line = evr::normalize(support() - kGrip);
    auto r = hold.update(frame(support() + line * 0.15f, 1.0f));
    CHECK(r.engaged);
    CHECK(r.slide == doctest::Approx(0.15f).epsilon(1e-3));
    // Turned well away from the one-handed support point but on the gun: still held (the off hand steers).
    r = hold.update(frame(kGrip + Vec3{0.25f, 0.1f, -0.2f}, 1.0f));
    CHECK(r.engaged);
    // 17 cm along: past the release radius, let go, easing back to the weapon hand's aim.
    r = hold.update(frame(support() + line * 0.17f, 1.0f));
    CHECK_FALSE(r.engaged);
    CHECK(r.ended == TwoHandEnd::TooFar);
    r = settle(hold, frame(support() + line * 0.17f, 1.0f));
    CHECK(r.aim == Quat{});
    // No chatter: back inside the release radius, and inside the capture radius, the grip still held from the
    // hold does not take it again.
    r = hold.update(frame(support() + line * 0.12f, 1.0f));
    CHECK_FALSE(r.engaged);
    r = hold.update(frame(support() + line * 0.05f, 1.0f));
    CHECK_FALSE(r.engaged);
    CHECK_FALSE(r.capturable);
    // A fresh squeeze there does.
    CHECK(grab(hold, support() + line * 0.05f).engaged);
}

TEST_CASE("two-handed hold: letting go of the grip ends the hold in that frame") {
    TwoHandHold hold;
    const Vec3 off = support() + Vec3{0.06f, 0.03f, 0.0f};
    REQUIRE(grab(hold, off).engaged);
    // The grip's own hysteresis (0.65 to press, 0.40 to let go): half squeezed still holds.
    auto r = hold.update(frame(off, 0.5f));
    CHECK(r.engaged);
    CHECK(angleBetween(r.aim, Quat{}) > 0.0f);
    r = hold.update(frame(off, 0.3f));
    CHECK_FALSE(r.engaged);
    CHECK(r.ended == TwoHandEnd::GripLetGo);
    CHECK(settle(hold, frame(off, 0.3f)).aim == Quat{});
}

TEST_CASE("two-handed hold: a closed gate or a lost hand lets go, and a grip held through it takes nothing") {
    TwoHandHold hold;
    REQUIRE(grab(hold, support()).engaged);
    TwoHandFrame f = frame(support(), 1.0f);
    f.block = TwoHandBlock::ForcedView;
    auto r = hold.update(f);
    CHECK_FALSE(r.engaged);
    CHECK(r.ended == TwoHandEnd::Blocked);
    CHECK_FALSE(r.capturable);
    r = hold.update(f); // still closed: the held grip takes nothing
    CHECK_FALSE(r.engaged);
    CHECK(r.ended == TwoHandEnd::None);
    // The gate opens with the grip still held on the support point: not taken until a fresh squeeze, so a
    // squeeze made for something else never takes the gun when the gate opens under it.
    r = hold.update(frame(support(), 1.0f));
    CHECK_FALSE(r.engaged);
    CHECK_FALSE(r.capturable);
    REQUIRE(grab(hold, support()).engaged);

    // A grip squeezed while the gate is closed, then the gate opens: the same.
    TwoHandHold closed;
    f = frame(support(), 0.0f);
    f.block = TwoHandBlock::Menu;
    r = closed.update(f);
    CHECK_FALSE(r.capturable); // gated: the mapper does not hold the grip's binding back
    f.offGrip = 1.0f;
    r = closed.update(f);
    CHECK_FALSE(r.engaged);
    CHECK_FALSE(r.capturable);
    r = closed.update(frame(support(), 1.0f));
    CHECK_FALSE(r.engaged);

    f = frame(support(), 1.0f);
    f.offTracked = false;
    r = hold.update(f);
    CHECK_FALSE(r.engaged);
    CHECK(r.ended == TwoHandEnd::Untracked);
    CHECK_FALSE(r.capturable);
    REQUIRE(grab(hold, support()).engaged);
    r = hold.update(frame(kGrip + Vec3{0.0f, 0.0f, -0.05f}, 1.0f));
    CHECK_FALSE(r.engaged);
    CHECK(r.ended == TwoHandEnd::TooClose);
}

TEST_CASE("two-handed hold: a hand hovering at the shortest line does not take and drop the gun") {
    // The Rocket Launcher's support point is 16 cm ahead of the grip: its capture radius reaches to 8 cm from
    // the weapon hand.
    TwoHandHold hold;
    const Vec3 along = evr::normalize(support({}, kRocket) - kGrip);
    REQUIRE(grab(hold, kGrip + along * 0.15f, kRocket).engaged);
    int changes = 0;
    bool was = true;
    for (int i = 0; i < 40; ++i) {
        const float at = (i % 2 == 0) ? 0.095f : 0.105f;
        const auto r = hold.update(frame(kGrip + along * at, 1.0f, kRocket));
        changes += r.engaged != was ? 1 : 0;
        was = r.engaged;
    }
    CHECK(changes == 1); // let go once (too close), and not taken again
    // Squeezed afresh at 10.5 cm (inside the capture radius, under the line a hold starts at): nothing, and
    // the grip's binding is not held back there.
    auto r = hold.update(frame(kGrip + along * 0.105f, 0.0f, kRocket));
    CHECK(r.distance < evr::input::kTwoHandCaptureMetres);
    CHECK_FALSE(r.capturable);
    r = hold.update(frame(kGrip + along * 0.105f, 1.0f, kRocket));
    CHECK_FALSE(r.engaged);
    // From 12 cm it does.
    CHECK(grab(hold, kGrip + along * 0.125f, kRocket).engaged);
}

TEST_CASE("two-handed hold: a reset lets go at once and forgets the grip") {
    TwoHandHold hold;
    const Vec3 off = support() + Vec3{0.07f, 0.0f, 0.0f};
    REQUIRE(grab(hold, off).engaged);
    hold.reset();
    CHECK_FALSE(hold.engaged());
    // No ease after a reset, and the grip still held from before takes nothing.
    auto r = hold.update(frame(off, 1.0f));
    CHECK_FALSE(r.engaged);
    CHECK_FALSE(r.easing);
    CHECK(r.aim == Quat{});
    CHECK_FALSE(r.capturable);
    // Also mid-ease.
    REQUIRE(grab(hold, off).easing);
    hold.reset();
    r = hold.update(frame(off, 0.0f));
    CHECK_FALSE(r.easing);
    CHECK(r.aim == Quat{});
    CHECK(r.capturable);
}

TEST_CASE("two-handed hold: inputs that are not numbers take nothing and turn nothing") {
    TwoHandHold hold;
    const Vec3 nan{kNaN, kNaN, kNaN};
    auto r = hold.update(frame(nan, 0.0f));
    CHECK_FALSE(r.capturable);
    r = hold.update(frame(nan, 1.0f));
    CHECK_FALSE(r.engaged);
    CHECK(r.aim == Quat{});
    hold.update(frame(support(), 0.0f));
    r = hold.update(frame(support(), kNaN)); // a grip that is not a number is not squeezed
    CHECK_FALSE(r.engaged);

    REQUIRE(grab(hold, support() + Vec3{0.05f, 0.0f, 0.0f}).engaged);
    TwoHandFrame f = frame(support() + Vec3{0.05f, 0.0f, 0.0f}, 1.0f);
    f.dt = kNaN; // no time passes
    r = hold.update(f);
    CHECK(r.engaged);
    CHECK(std::isfinite(r.aim.x));
    CHECK(std::isfinite(r.aim.w));
    r = hold.update(frame(nan, 1.0f));
    CHECK_FALSE(r.engaged);
    CHECK(r.ended != TwoHandEnd::None);
    CHECK(std::isfinite(r.aim.x));
    CHECK(std::isfinite(r.aim.y));
    CHECK(std::isfinite(r.aim.z));
    CHECK(std::isfinite(r.aim.w));
    CHECK(settle(hold, frame(nan, 1.0f)).aim == Quat{});
}
