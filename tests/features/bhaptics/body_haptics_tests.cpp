#include "features/bhaptics/body_haptics.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

using evr::bhaptics::BodyHaptics;
using evr::bhaptics::BodySignals;
using evr::bhaptics::Device;
using evr::bhaptics::Effect;
using evr::bhaptics::Frame;
using evr::bhaptics::hitColumns;
using evr::bhaptics::kShotGapSeconds;
using evr::bhaptics::kVestColumns;
using evr::bhaptics::vestColumn;
using evr::bhaptics::WeaponClass;
using evr::bhaptics::weaponClassOf;
using evr::input::Hand;

namespace {

BodySignals playing(double seconds, float health = 100.0f, float armor = 50.0f) {
    BodySignals s;
    s.seconds = seconds;
    s.gameplay = true;
    s.health = health;
    s.armor = armor;
    return s;
}

std::vector<const Frame*> of(const std::vector<Frame>& frames, Effect effect) {
    std::vector<const Frame*> out;
    for (const Frame& f : frames) {
        if (f.effect == effect) {
            out.push_back(&f);
        }
    }
    return out;
}

const Frame* on(const std::vector<Frame>& frames, Effect effect, Device device) {
    for (const Frame& f : frames) {
        if (f.effect == effect && f.device == device) {
            return &f;
        }
    }
    return nullptr;
}

int strongest(const Frame& frame) {
    int best = 0;
    for (const auto& dot : frame.dots) {
        best = std::max<int>(best, dot.intensity);
    }
    return best;
}

// The wearer-relative columns a frame's dots are in.
std::vector<int> wearerColumns(const Frame& frame) {
    std::vector<int> out;
    for (const auto& dot : frame.dots) {
        const int column = dot.index % kVestColumns;
        for (int wearer = 0; wearer < kVestColumns; ++wearer) {
            if (vestColumn(frame.device, wearer) == column &&
                std::find(out.begin(), out.end(), wearer) == out.end()) {
                out.push_back(wearer);
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

TEST_CASE("weapons are classed by their decl name, unknown ones as medium") {
    CHECK(weaponClassOf("weapon/player/shotgun") == WeaponClass::Medium);
    CHECK(weaponClassOf("weapon/player/double_barrel") == WeaponClass::Heavy);
    CHECK(weaponClassOf("weapon/player/rocket_launcher") == WeaponClass::Heavy);
    CHECK(weaponClassOf("weapon/player/heavy_cannon") == WeaponClass::Light);
    CHECK(weaponClassOf("weapon/player/plasma_rifle") == WeaponClass::Light);
    CHECK(weaponClassOf("weapon/player/bfg") == WeaponClass::Huge);
    CHECK(weaponClassOf("") == WeaponClass::Medium);
    CHECK(weaponClassOf("weapon/player/something_new") == WeaponClass::Medium);
}

TEST_CASE("vest columns: the front is seen from in front, the back from behind") {
    CHECK(vestColumn(Device::VestFront, 0) == 0);
    CHECK(vestColumn(Device::VestFront, 3) == 3);
    CHECK(vestColumn(Device::VestBack, 0) == 0);
    CHECK(vestColumn(Device::VestBack, 3) == 3);
}

TEST_CASE("a hit's direction picks the two nearest columns on the side facing it") {
    const auto ahead = hitColumns(0.0f);
    REQUIRE(ahead.size() == 2);
    for (const auto& c : ahead) {
        CHECK(c.side == Device::VestFront);
        CHECK((c.wearerColumn == 1 || c.wearerColumn == 2));
        CHECK(c.weight == doctest::Approx(1.0f));
    }
    const auto behind = hitColumns(180.0f);
    for (const auto& c : behind) {
        CHECK(c.side == Device::VestBack);
        CHECK((c.wearerColumn == 1 || c.wearerColumn == 2));
    }
    const auto left = hitColumns(67.5f);
    REQUIRE(left.size() == 2);
    CHECK(left[0].side == Device::VestFront);
    CHECK(left[0].wearerColumn == 0);
    CHECK(left[0].weight == doctest::Approx(1.0f));
    CHECK(left[1].weight < 0.5f);
    const auto right = hitColumns(-90.0f);
    for (const auto& c : right) {
        CHECK(c.wearerColumn == 3);
    }
    CHECK(hitColumns(std::numeric_limits<float>::quiet_NaN()).empty());
    // Angles past a full turn are the same direction.
    CHECK(hitColumns(360.0f)[0].side == Device::VestFront);
}

TEST_CASE("a shot pulses the weapon arm and the chest on its side, at most one per gap") {
    BodyHaptics body;
    auto s = playing(1.0);
    s.shots = 1;
    const auto first = body.update(s);
    const Frame* sleeve = on(first, Effect::Shot, Device::ForearmR);
    REQUIRE(sleeve != nullptr);
    CHECK(sleeve->dots.size() == 6);
    const Frame* chest = on(first, Effect::Shot, Device::VestFront);
    REQUIRE(chest != nullptr);
    CHECK(wearerColumns(*chest) == std::vector<int>{2, 3});
    CHECK(strongest(*chest) < strongest(*sleeve));
    s.seconds = 1.0 + kShotGapSeconds / 2;
    CHECK(of(body.update(s), Effect::Shot).empty());
    s.seconds = 1.0 + kShotGapSeconds;
    CHECK_FALSE(of(body.update(s), Effect::Shot).empty());
    s.shots = 0;
    s.seconds = 2.0;
    CHECK(of(body.update(s), Effect::Shot).empty());
}

TEST_CASE("a left-handed shot uses the left sleeve; heavier weapons kick harder") {
    BodyHaptics body;
    auto s = playing(1.0);
    s.shots = 1;
    s.weaponHand = Hand::Left;
    s.weapon = WeaponClass::Light;
    const auto light = body.update(s);
    REQUIRE(on(light, Effect::Shot, Device::ForearmL) != nullptr);
    CHECK(on(light, Effect::Shot, Device::ForearmR) == nullptr);
    CHECK(wearerColumns(*on(light, Effect::Shot, Device::VestFront)) == std::vector<int>{0, 1});
    s.seconds = 2.0;
    s.weapon = WeaponClass::Heavy;
    const auto heavy = body.update(s);
    CHECK(strongest(*on(heavy, Effect::Shot, Device::ForearmL)) >
          strongest(*on(light, Effect::Shot, Device::ForearmL)));
    CHECK(on(heavy, Effect::Shot, Device::ForearmL)->durationMillis >
          on(light, Effect::Shot, Device::ForearmL)->durationMillis);
}

TEST_CASE("damage is health plus armor lost; with a new hit's direction only that side plays") {
    BodyHaptics body;
    CHECK(body.update(playing(1.0)).empty()); // primes the levels
    auto s = playing(1.1, 100.0f, 40.0f);
    s.hitSerial = 7;
    s.hitYawDegrees = 180.0f;
    const auto hit = body.update(s);
    CHECK(on(hit, Effect::Damage, Device::VestFront) == nullptr);
    const Frame* back = on(hit, Effect::Damage, Device::VestBack);
    REQUIRE(back != nullptr);
    CHECK(wearerColumns(*back) == std::vector<int>{1, 2});
    // More damage is stronger.
    auto big = playing(1.2, 40.0f, 0.0f);
    big.hitSerial = 8;
    big.hitYawDegrees = 180.0f;
    CHECK(strongest(*on(body.update(big), Effect::Damage, Device::VestBack)) > strongest(*back));
}

TEST_CASE("damage without a new hit's direction plays the middle of both sides") {
    BodyHaptics body;
    body.update(playing(1.0));
    auto s = playing(1.1, 90.0f, 50.0f);
    s.hitYawDegrees = 0.0f; // the serial did not change: an old hit's direction
    const auto hit = body.update(s);
    REQUIRE(on(hit, Effect::Damage, Device::VestFront) != nullptr);
    REQUIRE(on(hit, Effect::Damage, Device::VestBack) != nullptr);
    CHECK(wearerColumns(*on(hit, Effect::Damage, Device::VestBack)) == std::vector<int>{1, 2});
}

TEST_CASE("a hit seen just before its damage still gives the direction, once") {
    BodyHaptics body;
    body.update(playing(1.0));
    auto hit = playing(1.02);
    hit.hitSerial = 3;
    hit.hitYawDegrees = 90.0f; // from the left
    CHECK(of(body.update(hit), Effect::Damage).empty());
    auto lost = hit;
    lost.seconds = 1.04;
    lost.health = 80.0f;
    const auto frames = body.update(lost);
    const Frame* front = on(frames, Effect::Damage, Device::VestFront);
    const Frame* back = on(frames, Effect::Damage, Device::VestBack);
    REQUIRE(front != nullptr);
    REQUIRE(back != nullptr); // 90 degrees is between the front's and the back's leftmost columns
    CHECK(wearerColumns(*front) == std::vector<int>{0});
    CHECK(wearerColumns(*back) == std::vector<int>{0});
    // More damage without a new hit is undirected.
    lost.seconds = 1.06;
    lost.health = 60.0f;
    CHECK(wearerColumns(*on(body.update(lost), Effect::Damage, Device::VestFront)) == std::vector<int>{1, 2});
    // A hit too long before its damage is not used.
    auto late = lost;
    late.hitSerial = 4;
    late.seconds = 2.0;
    body.update(late);
    late.seconds = 2.0 + evr::bhaptics::kHitWindowSeconds + 0.05;
    late.health = 50.0f;
    CHECK(wearerColumns(*on(body.update(late), Effect::Damage, Device::VestFront)) == std::vector<int>{1, 2});
}

TEST_CASE("healing, pickups and tiny changes are not damage; the first update only primes") {
    BodyHaptics body;
    CHECK(body.update(playing(1.0, 20.0f, 0.0f)).size() <= 1); // at most a heartbeat
    CHECK(of(body.update(playing(1.1, 80.0f, 50.0f)), Effect::Damage).empty());
    CHECK(of(body.update(playing(1.2, 79.8f, 50.0f)), Effect::Damage).empty());
}

TEST_CASE("low health beats lub-dub, quicker as health falls, and stops above the threshold") {
    BodyHaptics body;
    std::vector<double> beats;
    for (int i = 0; i <= 300; ++i) {
        const double t = i * 0.01;
        if (!of(body.update(playing(t, 25.0f, 0.0f)), Effect::Heartbeat).empty()) {
            beats.push_back(t);
        }
    }
    REQUIRE(beats.size() >= 4);
    CHECK(beats[1] - beats[0] == doctest::Approx(0.18).epsilon(0.1));
    const double slowPair = beats[2] - beats[0];
    BodyHaptics weaker;
    std::vector<double> fast;
    for (int i = 0; i <= 300; ++i) {
        const double t = i * 0.01;
        if (!of(weaker.update(playing(t, 5.0f, 0.0f)), Effect::Heartbeat).empty()) {
            fast.push_back(t);
        }
    }
    REQUIRE(fast.size() >= 3);
    CHECK(fast[2] - fast[0] < slowPair);
    CHECK(of(body.update(playing(3.5, 60.0f, 0.0f)), Effect::Heartbeat).empty());
    CHECK(of(body.update(playing(5.0, 60.0f, 0.0f)), Effect::Heartbeat).empty());
}

TEST_CASE("a glory kill's start pulses the front and both sleeves once") {
    BodyHaptics body;
    body.update(playing(1.0));
    auto s = playing(1.1);
    s.sync = true;
    const auto start = body.update(s);
    CHECK(on(start, Effect::GloryKill, Device::VestFront) != nullptr);
    CHECK(on(start, Effect::GloryKill, Device::ForearmL) != nullptr);
    CHECK(on(start, Effect::GloryKill, Device::ForearmR) != nullptr);
    s.seconds = 1.2;
    CHECK(of(body.update(s), Effect::GloryKill).empty());
}

TEST_CASE("dying fills both sides once; no heartbeat while dead") {
    BodyHaptics body;
    body.update(playing(1.0));
    auto s = playing(1.1, 0.0f, 0.0f);
    s.dead = true;
    const auto death = body.update(s);
    REQUIRE(on(death, Effect::Death, Device::VestFront) != nullptr);
    CHECK(on(death, Effect::Death, Device::VestFront)->dots.size() == 20);
    CHECK(on(death, Effect::Death, Device::VestBack) != nullptr);
    s.seconds = 2.0;
    const auto after = body.update(s);
    CHECK(of(after, Effect::Death).empty());
    CHECK(of(after, Effect::Heartbeat).empty());
}

TEST_CASE("outside gameplay nothing plays, and coming back does not replay what changed meanwhile") {
    BodyHaptics body;
    body.update(playing(1.0));
    auto menu = playing(1.1, 10.0f, 0.0f);
    menu.gameplay = false;
    menu.shots = 3;
    menu.sync = true;
    CHECK(body.update(menu).empty());
    auto back = playing(1.2, 10.0f, 0.0f);
    back.sync = true;
    const auto frames = body.update(back);
    CHECK(of(frames, Effect::Damage).empty());
    CHECK(of(frames, Effect::GloryKill).empty());
    auto broken = playing(1.3);
    broken.health = std::numeric_limits<float>::quiet_NaN();
    CHECK(body.update(broken).empty());
}

TEST_CASE("the strength scales every intensity; 0 plays nothing and a bad strength counts as 1") {
    auto s = playing(1.0);
    s.shots = 1;
    BodyHaptics full(1.0f);
    BodyHaptics half(0.5f);
    const int f = strongest(*on(full.update(s), Effect::Shot, Device::ForearmR));
    const int h = strongest(*on(half.update(s), Effect::Shot, Device::ForearmR));
    CHECK(h == doctest::Approx(f / 2.0).epsilon(0.05));
    BodyHaptics off(0.0f);
    CHECK(off.update(s).empty());
    CHECK(BodyHaptics(std::numeric_limits<float>::infinity()).strength() == 1.0f);
    CHECK(BodyHaptics(-1.0f).strength() == 1.0f);
}

TEST_CASE("every frame stays within its device's motors and 0..100, and the counts add up") {
    BodyHaptics body;
    body.update(playing(0.0));
    std::uint64_t frames = 0;
    for (int i = 1; i < 200; ++i) {
        auto s = playing(i * 0.02, static_cast<float>(100 - i / 2), 0.0f);
        s.shots = i % 3 == 0 ? 1u : 0u;
        s.hitSerial = static_cast<std::uint32_t>(i);
        s.hitYawDegrees = static_cast<float>(i * 37 % 360 - 180);
        s.sync = i % 50 < 5;
        for (const Frame& f : body.update(s)) {
            ++frames;
            for (const auto& dot : f.dots) {
                CHECK(dot.index < evr::bhaptics::motorCount(f.device));
                CHECK(dot.intensity <= 100);
                CHECK(dot.intensity > 0);
            }
        }
    }
    std::uint64_t counted = 0;
    for (const auto c : body.counts()) {
        counted += c;
    }
    CHECK(counted == frames);
}
