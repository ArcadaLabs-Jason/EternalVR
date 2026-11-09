#include "features/input/two_hand.hpp"

#include "features/input/controller_settings.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>

using evr::Quat;
using evr::Vec3;
using evr::input::ArmSignals;
using evr::input::Hand;
using evr::input::HandsAction;
using evr::input::SupportRow;
using evr::input::TwoHandBlock;
using evr::input::TwoHandGates;
namespace hands_flag = evr::input::hands_flag;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// The combat shotgun's row (data/weapons/viewmodel_offsets.toml).
constexpr SupportRow kShotgun{0.296f, 0.006f, 0.057f};
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

Vec3 forward(Quat q) {
    return evr::rotate(q, {0.0f, 0.0f, -1.0f});
}

Vec3 up(Quat q) {
    return evr::rotate(q, {0.0f, 1.0f, 0.0f});
}

// Reflected across the head's middle (x negated): a point, and an orientation.
Vec3 mirror(Vec3 v) {
    return {-v.x, v.y, v.z};
}

Quat mirror(Quat q) {
    return {q.x, -q.y, -q.z, q.w};
}

Vec3 support(Quat aim = {}) {
    return evr::input::supportPoint(kGrip, aim, kShotgun);
}

ArmSignals idle() {
    ArmSignals s;
    s.pendingAction = 1; // HANDSACTION_IDLE
    return s;
}

} // namespace

TEST_CASE("two-handed aim: the row is in the aim frame, and reflected for the left hand") {
    // id Tech forward, left, up are the aim's -Z, -X, +Y.
    const Vec3 v = evr::input::supportInAim({0.3f, 0.02f, 0.05f});
    CHECK(v.x == doctest::Approx(-0.02f));
    CHECK(v.y == doctest::Approx(0.05f));
    CHECK(v.z == doctest::Approx(-0.3f));
    CHECK(evr::input::supportRowFor(kShotgun, Hand::Right) == kShotgun);
    CHECK(evr::input::supportRowFor(kShotgun, Hand::Left) == SupportRow{0.296f, -0.006f, 0.057f});
    // The support point turns with the aim: pointing left (90 degrees about +Y) it is to the left of the
    // grip.
    const Quat left = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, kPi / 2.0f);
    const Vec3 p = evr::input::supportPoint(kGrip, left, kShotgun);
    CHECK(p.x == doctest::Approx(kGrip.x - 0.296f).epsilon(1e-4));
    CHECK(p.y == doctest::Approx(kGrip.y + 0.057f).epsilon(1e-4));
}

TEST_CASE("two-handed aim: the off hand on the support point changes nothing") {
    for (const Quat aim : {Quat{}, Quat::fromAxisAngle({0.3f, 1.0f, -0.2f}, 0.7f),
                           Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, -0.5f)}) {
        const auto held = evr::input::twoHandAim(kGrip, aim, support(aim), kShotgun);
        REQUIRE(held);
        CHECK(angleBetween(*held, aim) < 0.01f);
    }
}

TEST_CASE("two-handed aim: the support point lies on the line between the hands") {
    // The off hand 10 cm right of and 5 cm above the support point, and slid 6 cm along: the gun turns until
    // its support point is on the line from the weapon hand's grip to the off hand's.
    for (const Vec3 off : {support() + Vec3{0.10f, 0.05f, 0.0f}, support() + Vec3{-0.07f, -0.04f, -0.06f},
                           support() + Vec3{0.0f, 0.12f, 0.08f}}) {
        const auto held = evr::input::twoHandAim(kGrip, Quat{}, off, kShotgun);
        REQUIRE(held);
        const Vec3 onGun = support(*held) - kGrip;
        CHECK(angleBetween(onGun, off - kGrip) < 0.01f);
        // The barrel keeps its own angle to that line (the gun is rigid).
        CHECK(angleBetween(forward(*held), off - kGrip) ==
              doctest::Approx(angleBetween(forward(Quat{}), support() - kGrip)).epsilon(1e-3));
    }
}

TEST_CASE("two-handed aim: the weapon hand keeps the roll, the off hand steers") {
    // A row along the barrel: the off hand moved sideways turns the gun about the vertical only (no roll).
    constexpr SupportRow along{0.3f, 0.0f, 0.0f};
    const Vec3 off = kGrip + Vec3{0.1f, 0.0f, -0.3f};
    const auto held = evr::input::twoHandAim(kGrip, Quat{}, off, along);
    REQUIRE(held);
    CHECK(angleBetween(forward(*held), off - kGrip) < 0.01f);
    CHECK(angleBetween(up(*held), {0.0f, 1.0f, 0.0f}) < 0.01f);
    // The weapon hand rolled 40 degrees about the barrel: the two-handed gun is rolled the same about the
    // line.
    const Quat rolled = Quat::fromAxisAngle({0.0f, 0.0f, -1.0f}, 40.0f * kPi / 180.0f);
    const auto heldRolled = evr::input::twoHandAim(kGrip, rolled, off, along);
    REQUIRE(heldRolled);
    CHECK(angleBetween(forward(*heldRolled), off - kGrip) < 0.01f);
    CHECK(angleBetween(up(*heldRolled), up(*held)) == doctest::Approx(40.0f).epsilon(1e-3));
}

TEST_CASE("two-handed aim: with the weapon in the left hand the aim is the right hand's mirrored") {
    const Vec3 off = support() + Vec3{0.06f, 0.03f, -0.04f};
    const Quat aim = Quat::fromAxisAngle({0.2f, 1.0f, 0.1f}, 0.3f);
    const auto right = evr::input::twoHandAim(kGrip, aim, off, kShotgun);
    const auto left = evr::input::twoHandAim(mirror(kGrip), mirror(aim), mirror(off),
                                             evr::input::supportRowFor(kShotgun, Hand::Left));
    REQUIRE(right);
    REQUIRE(left);
    CHECK(angleBetween(forward(*left), mirror(forward(*right))) < 0.01f);
    CHECK(angleBetween(up(*left), mirror(up(*right))) < 0.01f);
    // Mirrored without reflecting the row, the support point would be on the wrong side of the barrel.
    const auto unreflected = evr::input::twoHandAim(mirror(kGrip), mirror(aim), mirror(off), kShotgun);
    REQUIRE(unreflected);
    CHECK(angleBetween(forward(*unreflected), mirror(forward(*right))) > 1.0f);
}

TEST_CASE("two-handed aim: hands closer than the shortest line give no aim") {
    CHECK_FALSE(evr::input::twoHandAim(kGrip, Quat{}, kGrip + Vec3{0.0f, 0.0f, -0.09f}, kShotgun));
    CHECK(evr::input::twoHandAim(kGrip, Quat{}, kGrip + Vec3{0.0f, 0.0f, -0.11f}, kShotgun));
    CHECK_FALSE(evr::input::twoHandAim(kGrip, Quat{}, support(), SupportRow{}));
}

TEST_CASE("two-handed aim: the gate closes for every state that takes the gun or the view") {
    TwoHandGates g;
    g.enabled = g.handAim = g.gunUp = g.support = true;
    CHECK(evr::input::twoHandBlock(g) == TwoHandBlock::None);
    const auto with = [&g](auto change) {
        TwoHandGates c = g;
        change(c);
        return evr::input::twoHandBlock(c);
    };
    CHECK(with([](TwoHandGates& c) { c.enabled = false; }) == TwoHandBlock::Off);
    CHECK(with([](TwoHandGates& c) { c.handAim = false; }) == TwoHandBlock::NotHandAim);
    CHECK(with([](TwoHandGates& c) { c.dead = true; }) == TwoHandBlock::Dead);
    CHECK(with([](TwoHandGates& c) { c.demon = true; }) == TwoHandBlock::Demon);
    CHECK(with([](TwoHandGates& c) { c.forcedView = true; }) == TwoHandBlock::ForcedView);
    CHECK(with([](TwoHandGates& c) { c.weaponWheel = true; }) == TwoHandBlock::WeaponWheel);
    CHECK(with([](TwoHandGates& c) { c.menu = true; }) == TwoHandBlock::Menu);
    CHECK(with([](TwoHandGates& c) { c.swimming = true; }) == TwoHandBlock::Swimming);
    CHECK(with([](TwoHandGates& c) { c.gunUp = false; }) == TwoHandBlock::GunDown);
    CHECK(with([](TwoHandGates& c) { c.support = false; }) == TwoHandBlock::NoSupport);
}

TEST_CASE("two-handed aim: the gun is down while switched, lowered or swung, not while reloaded") {
    CHECK(evr::input::gunUp(idle()));
    ArmSignals s = idle();
    s.handsFlags = hands_flag::kReloading; // the Super Shotgun after every shot
    CHECK(evr::input::gunUp(s));
    s.forcedView = true; // judged by the gate, not here
    s.offHandTracked = false;
    CHECK(evr::input::gunUp(s));
    for (const std::uint64_t flag :
         {hands_flag::kChangingWeapon, hands_flag::kMeleeAnim, hands_flag::kThrowing,
          hands_flag::kModChangeAnim, hands_flag::kCustomAnim}) {
        s = idle();
        s.handsFlags = flag;
        CHECK_FALSE(evr::input::gunUp(s));
    }
    for (const HandsAction a : {HandsAction::BringDown, HandsAction::BringUp, HandsAction::Melee,
                                HandsAction::ThrowItem, HandsAction::HammerSlam}) {
        s = idle();
        s.pendingAction = static_cast<std::int32_t>(a);
        CHECK_FALSE(evr::input::gunUp(s));
    }
    s = idle();
    s.destHandsState = evr::input::kHandsStateTransitioning;
    CHECK_FALSE(evr::input::gunUp(s));
    s = idle();
    s.hiddenReasons = 1;
    CHECK_FALSE(evr::input::gunUp(s));
    s = idle();
    s.syncActive = true; // a glory kill or a pickup's animation
    CHECK_FALSE(evr::input::gunUp(s));
}

TEST_CASE("two-handed aim's setting needs the new arms") {
    const auto parse = [](std::map<std::string, std::string, std::less<>> values) {
        return evr::input::parseControllerSettings(
            [&values](std::string_view name) -> std::optional<std::string> {
                const auto it = values.find(name);
                return it == values.end() ? std::nullopt : std::optional<std::string>(it->second);
            });
    };
    CHECK_FALSE(parse({}).settings.twoHandAim);
    auto on =
        parse({{"ETERNALVR_TWO_HAND", "1"}, {"ETERNALVR_OFFHAND", "free"}, {"ETERNALVR_ARM_SOLVE", "bind"}});
    CHECK(on.settings.twoHandAim);
    CHECK(on.issues.empty());
    on = parse({{"ETERNALVR_TWO_HAND", "1"}, {"ETERNALVR_ARMS", "hands"}}); // hands only takes the new arms
    CHECK(on.settings.twoHandAim);
    const auto old = parse({{"ETERNALVR_TWO_HAND", "1"}, {"ETERNALVR_OFFHAND", "game"}});
    CHECK_FALSE(old.settings.twoHandAim);
    REQUIRE(old.issues.size() == 1);
    CHECK(old.issues[0].name == "ETERNALVR_TWO_HAND");
    // The new arms are the layer's defaults (DECISIONS T-160): the setting alone is enough; the animated
    // solve is not.
    CHECK(parse({{"ETERNALVR_TWO_HAND", "1"}}).settings.twoHandAim);
    CHECK_FALSE(
        parse({{"ETERNALVR_TWO_HAND", "1"}, {"ETERNALVR_ARM_SOLVE", "animated"}}).settings.twoHandAim);
    CHECK(parse({{"ETERNALVR_TEST_ARM_FAULT", "two_hand"}}).settings.testArmFault ==
          evr::input::ArmFault::TwoHand);
    CHECK(parse({{"ETERNALVR_TEST_ARM_FAULT", "two_hand_line"}}).settings.testArmFault ==
          evr::input::ArmFault::TwoHandLine);
    CHECK(std::string(evr::input::armFaultName(evr::input::ArmFault::TwoHandLine)) == "two_hand_line");
}
