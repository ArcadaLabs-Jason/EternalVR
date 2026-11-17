// Two-handed aim's take rule: a squeeze arriving at the gun, the right it keeps while the grip is down, the
// gates that drop it, and the weapon in the left hand. The hold's own engage, ease and release are in
// two_hand_hold_tests.cpp.
#include "features/input/two_hand.hpp"
#include "features/input/two_hand_frames.hpp"

#include <doctest/doctest.h>

using evr::Quat;
using evr::Vec3;
using evr::input::Hand;
using evr::input::SupportRow;
using evr::input::TwoHandBlock;
using evr::input::TwoHandEnd;
using evr::input::TwoHandFrame;
using evr::input::TwoHandHold;
using evr::input::TwoHandResult;

namespace {

constexpr float kDt = evr::test::kTwoHandDt;
constexpr SupportRow kShotgun = evr::test::kShotgunRow;
constexpr Vec3 kGrip = evr::test::kWeaponGrip;

TwoHandFrame frame(Vec3 offGrip, float grip, SupportRow row = kShotgun) {
    return evr::test::twoHandFrame(offGrip, grip, row);
}

Vec3 support(Quat aim = {}, SupportRow row = kShotgun) {
    return evr::test::supportAt(aim, row);
}

TwoHandResult grab(TwoHandHold& hold, Vec3 off, SupportRow row = kShotgun) {
    return evr::test::grabAt(hold, off, row);
}

} // namespace

namespace {

// The off hand `along` metres along the gun's support line from the weapon hand's grip and `off` metres off
// it.
Vec3 at(float along, float off, SupportRow row = kShotgun) {
    const Vec3 u = evr::normalize(support({}, row) - kGrip);
    const Vec3 p = evr::normalize(evr::cross(u, Vec3{1.0f, 0.0f, 0.0f}));
    return kGrip + u * along + p * off;
}

float reachOf(SupportRow row) {
    return evr::length(evr::input::supportInAim(row));
}

// A squeeze made at `pos` after the grip was up there: the result of the press frame.
TwoHandResult press(TwoHandHold& hold, Vec3 pos, SupportRow row = kShotgun) {
    hold.update(frame(pos, 0.0f, row));
    return hold.update(frame(pos, 1.0f, row));
}

} // namespace

TEST_CASE(
    "two-handed hold: a squeeze with the hands together or the hand behind the grip is never arriving") {
    const float reach = reachOf(kShotgun);
    // Under the capture's own 12 cm line less the margin (9 cm), behind the grip, or past the support
    // point by more than the arriving radius: nothing is held back, and the right is not given.
    for (const float along : {-0.05f, 0.0f, 0.05f, 0.085f, reach + 0.13f}) {
        TwoHandHold hold;
        CHECK_FALSE(hold.update(frame(at(along, 0.02f), 0.0f)).arrivable);
        const auto r = hold.update(frame(at(along, 0.02f), 1.0f));
        CHECK_FALSE(r.keeps);
        CHECK_FALSE(r.engaged);
    }
    // Just inside each end it is.
    for (const float along : {0.095f, 0.11f, reach + 0.11f}) {
        TwoHandHold hold;
        CHECK(hold.update(frame(at(along, 0.02f), 0.0f)).arrivable);
        CHECK(hold.update(frame(at(along, 0.02f), 1.0f)).keeps);
    }
    // The 12 cm arriving radius (the test's own placement is good to a tenth of a millimetre).
    TwoHandHold edgeIn;
    CHECK(edgeIn.update(frame(at(0.2f, 0.1199f), 0.0f)).arrivable);
    TwoHandHold edgeOut;
    CHECK_FALSE(edgeOut.update(frame(at(0.2f, 0.1201f), 0.0f)).arrivable);
}

TEST_CASE("two-handed hold: arrivable needs both hands tracked and a gate a squeeze may keep its right in") {
    TwoHandHold hold;
    TwoHandFrame f = frame(at(0.2f, 0.1f), 0.0f);
    CHECK(hold.update(f).arrivable);
    f.offTracked = false;
    CHECK_FALSE(hold.update(f).arrivable);
    f.offTracked = true;
    f.weaponTracked = false;
    CHECK_FALSE(hold.update(f).arrivable);
    f.weaponTracked = true;
    for (const TwoHandBlock block :
         {TwoHandBlock::Menu, TwoHandBlock::WeaponWheel, TwoHandBlock::Dead, TwoHandBlock::Demon,
          TwoHandBlock::Swimming, TwoHandBlock::NoSupport, TwoHandBlock::Off, TwoHandBlock::NotHandAim}) {
        f.block = block;
        CHECK_FALSE(hold.update(f).arrivable);
    }
    for (const TwoHandBlock block : {TwoHandBlock::None, TwoHandBlock::GunDown, TwoHandBlock::ForcedView}) {
        f.block = block;
        CHECK(hold.update(f).arrivable);
    }
}

TEST_CASE("two-handed hold: the arrival window and its edge in frames") {
    const int window = static_cast<int>(evr::input::kTwoHandArriveSeconds / kDt); // 18
    const Vec3 outside = at(0.2f, 0.10f);
    const Vec3 inside = at(0.2f, 0.05f);
    // The hand reaches the zone `frames` frames after the press frame.
    const auto reachesAt = [&](int frames) {
        TwoHandHold hold;
        press(hold, outside);
        for (int i = 1; i < frames; ++i) {
            hold.update(frame(outside, 1.0f));
        }
        return hold.update(frame(inside, 1.0f)).engaged;
    };
    CHECK(reachesAt(window - 1));
    CHECK(reachesAt(window));
    CHECK_FALSE(reachesAt(window + 2)); // a frame of float slack either side of the edge itself
}

TEST_CASE("two-handed hold: a kept right is dropped when the gate becomes one that is not the gun's") {
    for (const TwoHandBlock block : {TwoHandBlock::Menu, TwoHandBlock::WeaponWheel, TwoHandBlock::Dead,
                                     TwoHandBlock::Demon, TwoHandBlock::Swimming}) {
        TwoHandHold hold;
        REQUIRE(grab(hold, support()).engaged);
        TwoHandFrame f = frame(support(), 1.0f);
        f.block = block;
        auto r = hold.update(f);
        CHECK(r.ended == TwoHandEnd::Blocked);
        CHECK_FALSE(r.keeps); // the binding is not held back for a squeeze the gun no longer owns
        // The gate opens again with the grip still down: not taken again, not held back.
        f.block = TwoHandBlock::None;
        r = hold.update(f);
        CHECK_FALSE(r.engaged);
        CHECK_FALSE(r.keeps);
        CHECK_FALSE(r.capturable);
        // Let go and squeezed again: takes.
        CHECK(grab(hold, support()).engaged);
    }
    // A right kept through a mod switch, then a menu while the grip is still down: dropped.
    TwoHandHold hold;
    TwoHandFrame f = frame(support(), 0.0f);
    f.block = TwoHandBlock::GunDown;
    hold.update(f);
    f.offGrip = 1.0f;
    CHECK(hold.update(f).keeps);
    f.block = TwoHandBlock::Menu;
    CHECK_FALSE(hold.update(f).keeps);
    f.block = TwoHandBlock::None;
    CHECK_FALSE(hold.update(f).engaged);
    // The gate a squeeze keeps its right through (a throw, a melee, a forced view) does not drop it.
    TwoHandHold kept;
    REQUIRE(grab(kept, support()).engaged);
    f = frame(support(), 1.0f);
    f.block = TwoHandBlock::ForcedView;
    CHECK(kept.update(f).keeps);
    f.block = TwoHandBlock::None;
    CHECK(kept.update(f).started);
}

TEST_CASE("two-handed hold: nothing is held back while the held item takes no support grip") {
    TwoHandHold hold;
    REQUIRE(grab(hold, support()).engaged);
    TwoHandFrame f = frame(support(), 1.0f);
    f.block = TwoHandBlock::NoSupport; // switched to a gun that takes no support grip
    auto r = hold.update(f);
    CHECK_FALSE(r.keeps);
    CHECK_FALSE(r.capturable);
    // Back to one that does, the grip still down (a melee shows an item with no row for a third of a second):
    // the right is kept, the gun taken again.
    f.block = TwoHandBlock::None;
    r = hold.update(f);
    CHECK(r.started);
    CHECK(r.keeps);
    // The same from the arrival window.
    TwoHandHold arriving;
    press(arriving, at(0.2f, 0.10f));
    f = frame(at(0.2f, 0.10f), 1.0f);
    f.block = TwoHandBlock::NoSupport;
    CHECK_FALSE(arriving.update(f).keeps);
}

TEST_CASE(
    "two-handed hold: the gun is taken again after a lost hand or hands too close, the grip still down") {
    TwoHandHold lost;
    REQUIRE(grab(lost, support()).engaged);
    TwoHandFrame f = frame(support(), 1.0f);
    f.offTracked = false;
    auto r = lost.update(f);
    CHECK(r.ended == TwoHandEnd::Untracked);
    CHECK(r.keeps); // the Belch stays held back
    f.offTracked = true;
    CHECK(lost.update(f).started);

    TwoHandHold close;
    REQUIRE(grab(close, support()).engaged);
    r = close.update(frame(kGrip + Vec3{0.0f, 0.0f, -0.05f}, 1.0f));
    CHECK(r.ended == TwoHandEnd::TooClose);
    CHECK(r.keeps);
    CHECK(close.update(frame(support(), 1.0f)).started);
}

TEST_CASE("two-handed hold: the rule works with the weapon in the left hand") {
    const SupportRow row = evr::input::supportRowFor(kShotgun, Hand::Left);
    const Vec3 outside = at(0.2f, 0.10f, row);
    const Vec3 inside = at(0.2f, 0.05f, row);
    TwoHandHold hold;
    CHECK(hold.update(frame(outside, 0.0f, row)).arrivable);
    auto r = hold.update(frame(outside, 1.0f, row));
    CHECK(r.keeps);
    CHECK_FALSE(r.engaged);
    r = hold.update(frame(inside, 1.0f, row));
    CHECK(r.engaged);
    // Well away from the line (a Belch squeeze): neither.
    TwoHandHold away;
    CHECK_FALSE(away.update(frame(at(0.2f, 0.30f, row), 0.0f, row)).arrivable);
    // A re-take after a mod switch, as with the right.
    TwoHandHold retake;
    REQUIRE(grab(retake, support({}, row), row).engaged);
    TwoHandFrame f = frame(support({}, row), 1.0f, row);
    f.block = TwoHandBlock::GunDown;
    CHECK(retake.update(f).ended == TwoHandEnd::Blocked);
    f.block = TwoHandBlock::None;
    CHECK(retake.update(f).started);
}

TEST_CASE("two-handed hold: a miss carries the weapon at the press") {
    TwoHandHold hold;
    TwoHandFrame f = frame(at(0.2f, 0.20f), 0.0f);
    f.weapon = "shotgun";
    hold.update(f);
    f.offGrip = 1.0f;
    hold.update(f);
    f.weapon = "rocket_launcher"; // switched while the squeeze went on
    f.offGrip = 0.0f;
    const auto r = hold.update(f);
    REQUIRE(r.missed);
    CHECK(r.missed->weapon == "shotgun");
}
