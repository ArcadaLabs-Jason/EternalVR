#include "features/bhaptics/pickups.hpp"

#include <doctest/doctest.h>

#include <optional>
#include <vector>

using evr::bhaptics::addPickup;
using evr::bhaptics::kPickupMaxMergeSeconds;
using evr::bhaptics::kPickupMergeSeconds;
using evr::bhaptics::kPickupSettleSeconds;
using evr::bhaptics::Pickup;
using evr::bhaptics::PickupDetector;
using evr::bhaptics::PickupKind;
using evr::bhaptics::PickupReading;
using evr::bhaptics::PickupSkip;

namespace {

constexpr double kFrame = 1.0 / 90.0;

// The detector fed once a game frame, with the gains it reported.
struct Player {
    PickupDetector detector;
    double seconds = 10.0;
    bool inPlay = true;
    float health = 60.0f;
    float armor = 20.0f;
    bool dead = false;
    std::vector<Pickup> gains;

    void frame() {
        seconds += kFrame;
        for (const Pickup& p : detector.update(PickupReading{seconds, inPlay, health, armor, dead})) {
            gains.push_back(p);
        }
    }

    void run(double forSeconds) {
        for (double t = 0.0; t < forSeconds; t += kFrame) {
            frame();
        }
    }

    // Past the first readings' settling time.
    void settle() { run(kPickupSettleSeconds + 0.1); }

    [[nodiscard]] std::vector<Pickup> felt() const {
        std::vector<Pickup> out;
        for (const Pickup& p : gains) {
            if (p.skip == PickupSkip::None) {
                out.push_back(p);
            }
        }
        return out;
    }
};

} // namespace

TEST_CASE("the first readings are no pickup") {
    Player p;
    p.health = 100.0f;
    p.frame();
    p.run(1.0);
    CHECK(p.gains.empty());
    // A level starting with health set a frame after the first reading: not felt.
    Player q;
    q.health = 0.0f;
    q.frame();
    q.health = 100.0f;
    q.armor = 50.0f;
    q.run(1.0);
    CHECK(q.felt().empty());
    REQUIRE(q.gains.size() == 2);
    CHECK(q.gains[0].skip == PickupSkip::Settling);
}

TEST_CASE("a small and a big health pickup") {
    Player p;
    p.settle();
    p.health = 65.0f;
    p.run(0.5);
    p.health = 90.0f;
    p.run(0.5);
    const auto felt = p.felt();
    REQUIRE(felt.size() == 2);
    CHECK(felt[0].kind == PickupKind::Health);
    CHECK(felt[0].amount == doctest::Approx(5.0f));
    CHECK(felt[0].from == doctest::Approx(60.0f));
    CHECK(felt[0].to == doctest::Approx(65.0f));
    CHECK(felt[0].steps == 1);
    CHECK_FALSE(felt[0].mega);
    CHECK(felt[1].amount == doctest::Approx(25.0f));
    CHECK_FALSE(felt[1].mega);
}

TEST_CASE("a gain is reported once its rises stop") {
    Player p;
    p.settle();
    p.health = 70.0f;
    p.frame();
    CHECK(p.gains.empty());
    // Within the merge window: nothing yet.
    p.run(kPickupMergeSeconds * 0.5);
    CHECK(p.gains.empty());
    p.run(kPickupMergeSeconds);
    CHECK(p.gains.size() == 1);
}

TEST_CASE("rises close together are one gain") {
    Player p;
    p.settle();
    // One pickup moving health over three frames.
    for (const float h : {63.0f, 68.0f, 72.0f}) {
        p.health = h;
        p.frame();
    }
    p.run(0.5);
    auto felt = p.felt();
    REQUIRE(felt.size() == 1);
    CHECK(felt[0].amount == doctest::Approx(12.0f));
    CHECK(felt[0].steps == 3);
    CHECK(felt[0].spanSeconds == doctest::Approx(2 * kFrame));
    // Two pickups further apart than the window are two.
    p.health = 77.0f;
    p.frame();
    p.run(kPickupMergeSeconds + 0.05);
    p.health = 82.0f;
    p.frame();
    p.run(0.5);
    felt = p.felt();
    CHECK(felt.size() == 3);
}

TEST_CASE("a gain that keeps rising ends after the longest merge") {
    Player p;
    p.settle();
    const double start = p.seconds;
    while (p.seconds - start < 1.0) {
        p.health += 0.5f; // healing over time: every frame a little
        p.frame();
    }
    p.run(0.5);
    const auto felt = p.felt();
    REQUIRE(felt.size() >= 2);
    for (const Pickup& g : felt) {
        CHECK(g.spanSeconds <= kPickupMaxMergeSeconds);
    }
}

TEST_CASE("a Mega Health is a gain of 100") {
    Player p;
    p.health = 80.0f;
    p.settle();
    p.health = 180.0f;
    p.run(0.5);
    // Above 100 with an upgraded maximum, but an ordinary pickup.
    p.health = 205.0f;
    p.run(0.5);
    const auto felt = p.felt();
    REQUIRE(felt.size() == 2);
    CHECK(felt[0].mega);
    CHECK_FALSE(felt[1].mega);
}

TEST_CASE("armor going up") {
    Player p;
    p.settle();
    p.armor = 25.0f;
    p.run(0.5);
    p.armor = 75.0f;
    p.run(0.5);
    const auto felt = p.felt();
    REQUIRE(felt.size() == 2);
    CHECK(felt[0].kind == PickupKind::Armor);
    CHECK(felt[0].amount == doctest::Approx(5.0f));
    CHECK(felt[1].amount == doctest::Approx(50.0f));
    CHECK_FALSE(felt[1].mega); // only health has one
}

TEST_CASE("health and armor at once are two gains") {
    Player p;
    p.settle();
    p.health = 70.0f;
    p.armor = 30.0f;
    p.run(0.5);
    const auto felt = p.felt();
    REQUIRE(felt.size() == 2);
    CHECK(felt[0].kind != felt[1].kind);
}

TEST_CASE("death and a respawn to full are not pickups") {
    Player p;
    p.settle();
    p.health = 0.0f;
    p.armor = 0.0f;
    p.dead = true;
    p.run(1.0);
    // Health set back while still dead, then alive.
    p.health = 100.0f;
    p.armor = 50.0f;
    p.run(0.3);
    p.dead = false;
    p.run(1.0);
    CHECK(p.felt().empty());
    REQUIRE_FALSE(p.gains.empty());
    CHECK(p.gains[0].skip == PickupSkip::Dead);
    // Alive first, health set a few frames later.
    Player q;
    q.settle();
    q.health = 0.0f;
    q.dead = true;
    q.run(0.5);
    q.dead = false;
    q.run(0.1);
    q.health = 100.0f;
    q.run(1.0);
    CHECK(q.felt().empty());
    // After that, pickups are felt again.
    q.health = 90.0f;
    q.run(0.2);
    q.health = 100.0f;
    q.run(0.5);
    CHECK(q.felt().size() == 1);
}

TEST_CASE("a rise from 0 without death is not a pickup") {
    Player p;
    p.settle();
    p.health = 0.0f;
    p.run(0.3);
    p.health = 100.0f; // an extra life
    p.run(0.5);
    REQUIRE(p.gains.size() == 1);
    CHECK(p.gains[0].skip == PickupSkip::FromZero);
}

TEST_CASE("a level load is not a pickup") {
    Player p;
    p.settle();
    p.health = 40.0f;
    p.armor = 0.0f;
    p.run(0.2);
    // A load: no readings for seconds, then the next level's values.
    p.seconds += 6.0;
    p.health = 100.0f;
    p.armor = 50.0f;
    p.run(1.0);
    CHECK(p.gains.empty());
    // A menu in between starts over the same way.
    p.inPlay = false;
    p.run(0.5);
    p.health = 30.0f;
    p.frame();
    p.inPlay = true;
    p.frame();
    p.health = 100.0f;
    p.run(1.0);
    CHECK(p.felt().empty());
}

TEST_CASE("float noise is not a pickup") {
    Player p;
    p.settle();
    for (int i = 0; i < 200; ++i) {
        p.health = i % 2 == 0 ? 60.004f : 60.0f;
        p.armor = 20.0f + static_cast<float>(i % 3) * 0.2f;
        p.frame();
    }
    p.run(0.5);
    CHECK(p.gains.empty());
    // A repeat of the same time changes nothing.
    PickupDetector d;
    CHECK(d.update({1.0, true, 50.0f, 0.0f, false}).empty());
    CHECK(d.update({1.6, true, 50.0f, 0.0f, false}).empty());
    CHECK(d.update({1.6, true, 75.0f, 0.0f, false}).empty());
    CHECK(d.update({1.7, true, 50.0f, 0.0f, false}).empty());
    CHECK(d.update({2.0, true, 50.0f, 0.0f, false}).empty());
}

TEST_CASE("gains not yet taken add up") {
    std::optional<Pickup> pending;
    Pickup a;
    a.amount = 5.0f;
    a.from = 50.0f;
    a.to = 55.0f;
    a.steps = 1;
    addPickup(pending, a);
    REQUIRE(pending);
    Pickup b = a;
    b.amount = 100.0f;
    b.from = 55.0f;
    b.to = 155.0f;
    b.mega = true;
    addPickup(pending, b);
    CHECK(pending->amount == doctest::Approx(105.0f));
    CHECK(pending->from == doctest::Approx(50.0f));
    CHECK(pending->to == doctest::Approx(155.0f));
    CHECK(pending->steps == 2);
    CHECK(pending->mega);
}
