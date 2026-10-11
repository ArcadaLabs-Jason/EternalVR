#include "features/bhaptics/ammo.hpp"
#include "features/bhaptics/body_haptics.hpp"
#include "features/bhaptics/pickups.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using evr::bhaptics::AmmoFelt;
using evr::bhaptics::AmmoWave;
using evr::bhaptics::BodyHaptics;
using evr::bhaptics::BodySignals;
using evr::bhaptics::Device;
using evr::bhaptics::Effect;
using evr::bhaptics::Frame;
using evr::bhaptics::isAmmoPickup;
using evr::bhaptics::kAmmoMergeSeconds;
using evr::bhaptics::kVestColumns;
using evr::bhaptics::kVestRows;
using evr::bhaptics::Pickup;
using evr::bhaptics::PickupKind;
using evr::bhaptics::SyncKind;

namespace {

constexpr double kTick = 0.02; // the layer's link thread

struct Played {
    double seconds = 0.0;
    Frame frame;
};

// Updates from `start` for `forSeconds`, with an ammo pickup in the update at or just after each time in
// `pickups`; every frame made.
std::vector<Played> play(BodyHaptics& body,
                         const std::vector<double>& pickups,
                         double start = 5.0,
                         double forSeconds = 1.0,
                         const BodySignals& base = {}) {
    std::vector<Played> out;
    std::size_t next = 0;
    for (double t = 0.0; t < forSeconds; t += kTick) {
        BodySignals s = base;
        s.seconds = start + t;
        s.gameplay = true;
        s.health = 100.0f;
        s.armor = 50.0f;
        while (next < pickups.size() && pickups[next] <= s.seconds + 1e-9) {
            ++s.ammo;
            ++next;
        }
        for (const Frame& f : body.update(s)) {
            out.push_back({s.seconds, f});
        }
    }
    return out;
}

std::vector<Played> ammoOnly(const std::vector<Played>& played) {
    std::vector<Played> out;
    for (const Played& p : played) {
        if (p.frame.effect == Effect::Ammo) {
            out.push_back(p);
        }
    }
    return out;
}

// The band's row of a frame (its strongest dots).
int bandRow(const Frame& frame) {
    int best = -1;
    int row = -1;
    for (const auto& dot : frame.dots) {
        if (dot.intensity > best) {
            best = dot.intensity;
            row = dot.index / kVestColumns;
        }
    }
    return row;
}

int strongest(const Frame& frame) {
    int best = 0;
    for (const auto& dot : frame.dots) {
        best = std::max<int>(best, dot.intensity);
    }
    return best;
}

std::vector<int> rows(const std::vector<Played>& played) {
    std::vector<int> out;
    for (const Played& p : played) {
        out.push_back(bandRow(p.frame));
    }
    return out;
}

} // namespace

TEST_CASE("only props that give ammo are ammo") {
    constexpr const char* kItem = ".?AVidDeclProp_ItemPickupComponent@@";
    CHECK(isAmmoPickup(kItem, ".?AVidDeclAmmo@@"));
    CHECK(isAmmoPickup(".?AVidDeclProp_UniversalAmmoComponent@@", ""));
    // Weapons, the Sentinel Armor and extra lives are item pickups of other decls.
    CHECK_FALSE(isAmmoPickup(kItem, ".?AVidDeclWeapon@@"));
    CHECK_FALSE(isAmmoPickup(kItem, ".?AVidDeclSentinelArmor@@"));
    CHECK_FALSE(isAmmoPickup(kItem, ".?AVidDeclExtraLife@@"));
    CHECK_FALSE(isAmmoPickup(kItem, ""));
    // Health and armor come from components of their own.
    CHECK_FALSE(isAmmoPickup(".?AVidDeclProp_HealthComponent@@", ""));
    CHECK_FALSE(isAmmoPickup(".?AVidDeclProp_HealthComponent@@", ".?AVidDeclAmmo@@"));
    CHECK_FALSE(isAmmoPickup("", ""));
}

TEST_CASE("an ammo pickup sends a light wave down the back only") {
    BodyHaptics body;
    const auto played = play(body, {5.0});
    const auto ammo = ammoOnly(played);
    CHECK(ammo.size() == played.size());
    CHECK(rows(ammo) == std::vector<int>{0, 1, 2, 3, 4});
    for (const Played& p : ammo) {
        CHECK(p.frame.device == Device::VestBack);
        // The band's four columns, and after the first row the softer trail above it.
        const int band = bandRow(p.frame);
        for (const auto& dot : p.frame.dots) {
            const int row = dot.index / kVestColumns;
            CHECK((row == band || row == band - 1));
        }
        CHECK(strongest(p.frame) >= 40);
        CHECK(strongest(p.frame) <= 50);
    }
    REQUIRE_FALSE(ammo.empty());
    // Quick: the last row starts within 0.2 s and its frame ends by 0.25 s.
    CHECK(ammo.back().seconds - 5.0 <= 0.2);
    CHECK(ammo.back().seconds - 5.0 + ammo.back().frame.durationMillis / 1000.0 <= 0.25);
    CHECK(body.ammoPickups() == 1);
    CHECK(body.lastAmmo() == AmmoFelt::Wave);
}

TEST_CASE("the ammo wave is lighter than the armor's") {
    BodyHaptics armor;
    BodySignals s;
    s.seconds = 5.0;
    s.gameplay = true;
    s.health = 100.0f;
    s.armor = 50.0f;
    Pickup gain;
    gain.kind = PickupKind::Armor;
    gain.amount = 25.0f;
    s.armorGain = gain;
    int armorBest = 0;
    for (const Frame& f : armor.update(s)) {
        armorBest = std::max(armorBest, strongest(f));
    }
    BodyHaptics body;
    int ammoBest = 0;
    for (const Played& p : ammoOnly(play(body, {5.0}))) {
        ammoBest = std::max(ammoBest, strongest(p.frame));
    }
    CHECK(ammoBest > 0);
    CHECK(ammoBest < armorBest);
}

TEST_CASE("ammo picked up close together is one wave") {
    BodyHaptics body;
    // A chainsaw's shower: four drops within the merge window, one wave.
    const auto one = ammoOnly(play(body, {5.0, 5.06, 5.12, 5.25}));
    CHECK(rows(one) == std::vector<int>{0, 1, 2, 3, 4});
    CHECK(body.ammoPickups() == 4);
    CHECK(body.lastAmmo() == AmmoFelt::Merged);
    // One past the window starts the next wave.
    BodyHaptics other;
    const auto two = ammoOnly(play(other, {5.0, 5.0 + kAmmoMergeSeconds + 0.04}));
    CHECK(rows(two) == std::vector<int>{0, 1, 2, 3, 4, 0, 1, 2, 3, 4});
    CHECK(other.lastAmmo() == AmmoFelt::Wave);
}

TEST_CASE("the ammo wave gives way to stronger effects and the other pickups' waves") {
    // A portal's sweep holds the vest.
    BodySignals portal;
    portal.portals = 1;
    BodyHaptics a;
    const auto withPortal = play(a, {5.0}, 5.0, 0.5, portal);
    CHECK(ammoOnly(withPortal).empty());
    CHECK_FALSE(withPortal.empty());
    // A health wave at the same time covers the back too.
    BodyHaptics b;
    BodySignals s;
    s.seconds = 5.0;
    s.gameplay = true;
    s.health = 100.0f;
    s.armor = 50.0f;
    s.ammo = 1;
    Pickup gain;
    gain.kind = PickupKind::Health;
    gain.amount = 25.0f;
    s.healthGain = gain;
    bool health = false;
    for (double t = 0.0; t < 0.5; t += kTick) {
        s.seconds = 5.0 + t;
        for (const Frame& f : b.update(s)) {
            CHECK(f.effect != Effect::Ammo);
            health = health || f.effect == Effect::Health;
        }
        s.ammo = 0;
        s.healthGain.reset();
    }
    CHECK(health);
}

TEST_CASE("no ammo wave while dead, during a Sentinel Crystal's upgrade or outside gameplay") {
    BodySignals dead;
    dead.dead = true;
    BodyHaptics a;
    CHECK(ammoOnly(play(a, {5.0}, 5.0, 0.5, dead)).empty());
    CHECK(a.ammoPickups() == 1);
    CHECK(a.lastAmmo() == AmmoFelt::NotFelt);
    BodySignals crystal;
    crystal.sync = true;
    crystal.syncKind = SyncKind::Crystal;
    BodyHaptics b;
    CHECK(ammoOnly(play(b, {5.0}, 5.0, 0.5, crystal)).empty());
    BodyHaptics c;
    BodySignals menu;
    menu.seconds = 5.0;
    menu.ammo = 1;
    CHECK(c.update(menu).empty());
    CHECK(c.ammoPickups() == 0);
}

TEST_CASE("AmmoWave merges by the wave's start and starts over after stop") {
    AmmoWave wave(kVestRows);
    CHECK(wave.pickUp(1.0));
    const auto first = wave.step(1.0);
    REQUIRE(first);
    CHECK(first->row == 0);
    CHECK(first->trailRow == -1);
    CHECK_FALSE(wave.pickUp(1.1));
    CHECK_FALSE(wave.step(1.01)); // still on the first row
    const auto second = wave.step(1.05);
    REQUIRE(second);
    CHECK(second->row == 1);
    CHECK(second->trailRow == 0);
    // The window counts from the start, not from the merged pickup.
    CHECK(wave.pickUp(1.0 + kAmmoMergeSeconds));
    wave.stop();
    CHECK_FALSE(wave.active());
    CHECK(wave.pickUp(1.35));
    CHECK_FALSE(wave.pickUp(std::numeric_limits<double>::quiet_NaN()));
    CHECK_FALSE(wave.step(3.0)); // over
    CHECK_FALSE(wave.active());
}
