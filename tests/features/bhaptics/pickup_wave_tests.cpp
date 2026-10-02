#include "features/bhaptics/body_haptics.hpp"
#include "features/bhaptics/pickups.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

using evr::bhaptics::BodyHaptics;
using evr::bhaptics::BodySignals;
using evr::bhaptics::Device;
using evr::bhaptics::Effect;
using evr::bhaptics::Frame;
using evr::bhaptics::kVestColumns;
using evr::bhaptics::kVestRows;
using evr::bhaptics::Pickup;
using evr::bhaptics::PickupKind;
using evr::bhaptics::PickupWave;
using evr::bhaptics::SyncKind;

namespace {

constexpr double kTick = 0.02; // the layer's link thread

Pickup gain(PickupKind kind, float amount, bool mega = false) {
    Pickup p;
    p.kind = kind;
    p.amount = amount;
    p.mega = mega;
    return p;
}

struct Played {
    double seconds = 0.0;
    Frame frame;
};

// One gain handed to the vest at `start`, then the updates for `forSeconds`; the wave's frames.
std::vector<Played> play(BodyHaptics& body,
                         const Pickup& pickup,
                         double start = 5.0,
                         double forSeconds = 1.0,
                         const BodySignals& base = {}) {
    std::vector<Played> out;
    for (double t = 0.0; t < forSeconds; t += kTick) {
        BodySignals s = base;
        s.seconds = start + t;
        s.gameplay = true;
        s.health = 100.0f;
        s.armor = 50.0f;
        if (t == 0.0) {
            (pickup.kind == PickupKind::Armor ? s.armorGain : s.healthGain) = pickup;
        }
        for (const Frame& f : body.update(s)) {
            if (f.effect == Effect::Health || f.effect == Effect::MegaHealth || f.effect == Effect::Armor) {
                out.push_back({s.seconds, f});
            }
        }
    }
    return out;
}

// The band's row of a frame (its strongest dots) and its strength.
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

std::vector<int> rowsOn(const std::vector<Played>& played, Device side) {
    std::vector<int> rows;
    for (const Played& p : played) {
        if (p.frame.device == side) {
            rows.push_back(bandRow(p.frame));
        }
    }
    return rows;
}

} // namespace

TEST_CASE("health goes up the vest, front and back, and nothing on the sleeves") {
    BodyHaptics body;
    const auto played = play(body, gain(PickupKind::Health, 25.0f));
    const std::vector<int> up{4, 3, 2, 1, 0};
    CHECK(rowsOn(played, Device::VestFront) == up);
    CHECK(rowsOn(played, Device::VestBack) == up);
    for (const Played& p : played) {
        CHECK(p.frame.effect == Effect::Health);
        CHECK((p.frame.device == Device::VestFront || p.frame.device == Device::VestBack));
        // The band's four columns, and after the first row the softer trail below it.
        const int band = bandRow(p.frame);
        for (const auto& dot : p.frame.dots) {
            const int row = dot.index / kVestColumns;
            CHECK((row == band || row == band + 1));
        }
    }
    REQUIRE_FALSE(played.empty());
    // Fast: the last row starts within 0.25 s and its frame ends by 0.3 s.
    const Played& last = played.back();
    CHECK(last.seconds - 5.0 <= 0.25);
    CHECK(last.seconds - 5.0 + last.frame.durationMillis / 1000.0 <= 0.3);
}

TEST_CASE("armor goes down the vest") {
    BodyHaptics body;
    const auto played = play(body, gain(PickupKind::Armor, 25.0f));
    const std::vector<int> down{0, 1, 2, 3, 4};
    CHECK(rowsOn(played, Device::VestFront) == down);
    CHECK(rowsOn(played, Device::VestBack) == down);
    for (const Played& p : played) {
        CHECK(p.frame.effect == Effect::Armor);
    }
}

TEST_CASE("a bigger gain is stronger, up to a limit") {
    const auto peak = [](float amount) {
        BodyHaptics body;
        int best = 0;
        for (const Played& p : play(body, gain(PickupKind::Health, amount))) {
            best = std::max(best, strongest(p.frame));
        }
        return best;
    };
    const int small = peak(5.0f);
    const int big = peak(25.0f);
    CHECK(small > 0);
    CHECK(small < 40);
    CHECK(big >= 75);
    CHECK(peak(50.0f) == big);
    BodyHaptics mega;
    int best = 0;
    for (const Played& p : play(mega, gain(PickupKind::Health, 100.0f, true))) {
        CHECK(p.frame.effect == Effect::MegaHealth);
        best = std::max(best, strongest(p.frame));
    }
    CHECK(best == 100);
    // The strength scales it like every effect.
    BodyHaptics half(0.5f);
    int halfBest = 0;
    for (const Played& p : play(half, gain(PickupKind::Health, 100.0f, true))) {
        halfBest = std::max(halfBest, strongest(p.frame));
    }
    CHECK(halfBest == 50);
}

TEST_CASE("a Mega Health's wave is slower") {
    BodyHaptics a;
    BodyHaptics b;
    const auto normal = play(a, gain(PickupKind::Health, 25.0f));
    const auto mega = play(b, gain(PickupKind::Health, 100.0f, true));
    REQUIRE_FALSE(normal.empty());
    REQUIRE_FALSE(mega.empty());
    CHECK(rowsOn(mega, Device::VestFront) == std::vector<int>{4, 3, 2, 1, 0});
    const double normalLast = normal.back().seconds - 5.0;
    const double megaLast = mega.back().seconds - 5.0;
    CHECK(megaLast > normalLast * 1.3);
    CHECK(mega.back().frame.durationMillis > normal.back().frame.durationMillis);
}

TEST_CASE("a hit, a glory kill or a portal keeps the wave off the vest") {
    BodyHaptics body;
    // Prime the levels, then lose health while the gain arrives: the hit's frame wins.
    BodySignals s;
    s.seconds = 4.98;
    s.gameplay = true;
    s.health = 100.0f;
    s.armor = 50.0f;
    body.update(s);
    s.seconds = 5.0;
    s.armor = 30.0f;
    s.healthGain = gain(PickupKind::Health, 25.0f);
    const auto frames = body.update(s);
    bool damage = false;
    for (const Frame& f : frames) {
        damage = damage || f.effect == Effect::Damage;
        CHECK(f.effect != Effect::Health);
    }
    CHECK(damage);
    // A portal's sweep holds the vest for the whole wave.
    BodySignals portal;
    portal.portals = 1;
    BodyHaptics other;
    CHECK(play(other, gain(PickupKind::Health, 25.0f), 5.0, 0.4, portal).empty());
    // A Sentinel Crystal's upgrade filling health is felt as the crystal's wave only.
    BodySignals crystal;
    crystal.sync = true;
    crystal.syncKind = SyncKind::Crystal;
    BodyHaptics third;
    CHECK(play(third, gain(PickupKind::Health, 100.0f), 5.0, 1.0, crystal).empty());
}

TEST_CASE("no wave outside gameplay or while dead") {
    BodyHaptics body;
    BodySignals s;
    s.seconds = 5.0;
    s.gameplay = false;
    s.healthGain = gain(PickupKind::Health, 25.0f);
    CHECK(body.update(s).empty());
    BodySignals dead;
    dead.dead = true;
    BodyHaptics other;
    CHECK(play(other, gain(PickupKind::Armor, 25.0f), 5.0, 1.0, dead).empty());
}

TEST_CASE("a new gain starts the wave again") {
    PickupWave wave(kVestRows);
    wave.start(gain(PickupKind::Health, 5.0f), 1.0);
    const auto first = wave.step(1.0);
    REQUIRE(first);
    CHECK(first->row == kVestRows - 1);
    CHECK(first->trailRow == -1);
    CHECK_FALSE(wave.step(1.01)); // still on the first row
    const auto second = wave.step(1.06);
    REQUIRE(second);
    CHECK(second->row == kVestRows - 2);
    CHECK(second->trailRow == kVestRows - 1);
    wave.start(gain(PickupKind::Health, 25.0f), 1.08);
    const auto again = wave.step(1.08);
    REQUIRE(again);
    CHECK(again->row == kVestRows - 1);
    CHECK(again->intensity > first->intensity);
    CHECK_FALSE(wave.step(3.0)); // over
    CHECK_FALSE(wave.active());
}
