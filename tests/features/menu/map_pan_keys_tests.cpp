#include "features/menu/map_pan_keys.hpp"

#include <doctest/doctest.h>

#include <cmath>

using evr::input::Axis2;
using evr::menu::kKeyHoldSeconds;
using evr::menu::MapKeys;
using evr::menu::MapPanKeys;

namespace {

constexpr double kFrame = 1.0 / 90.0;
// The headset rates the tests run at.
constexpr double kRates[] = {72.0, 90.0, 120.0, 144.0};

// What the game makes of the keys: normalize(right - left, up - down).
Axis2 gamePan(const MapKeys& k) {
    const float x = (k.right ? 1.0f : 0.0f) - (k.left ? 1.0f : 0.0f);
    const float y = (k.up ? 1.0f : 0.0f) - (k.down ? 1.0f : 0.0f);
    const float m = std::hypot(x, y);
    return m > 0.0f ? Axis2{x / m, y / m} : Axis2{};
}

// The game's pan averaged over `frames` frames, `dt` seconds each, of the stick held at `stick`.
Axis2 averagePan(MapPanKeys& keys, Axis2 stick, int frames, double dt = kFrame) {
    Axis2 sum;
    for (int i = 0; i < frames; ++i) {
        const Axis2 p = gamePan(keys.update(stick, i == 0 ? 0.0 : dt));
        sum.x += p.x;
        sum.y += p.y;
    }
    return {sum.x / static_cast<float>(frames), sum.y / static_cast<float>(frames)};
}

} // namespace

TEST_CASE("map pan keys: a stick at rest holds no key") {
    MapPanKeys keys;
    CHECK(keys.update({}, kFrame) == MapKeys{});
    CHECK(keys.update({std::nanf(""), 0.0f}, kFrame) == MapKeys{});
}

TEST_CASE("map pan keys: full deflection along an axis or a diagonal holds its keys every frame") {
    MapPanKeys keys;
    for (int i = 0; i < 10; ++i) {
        CHECK(keys.update({0.0f, 1.0f}, kFrame) == MapKeys{.up = true});
    }
    keys.reset();
    for (int i = 0; i < 10; ++i) {
        CHECK(keys.update({1.0f, 0.0f}, kFrame) == MapKeys{.right = true});
    }
    keys.reset();
    const float d = std::sqrt(0.5f);
    for (int i = 0; i < 10; ++i) {
        CHECK(keys.update({-d, -d}, kFrame) == MapKeys{.left = true, .down = true});
    }
}

TEST_CASE("map pan keys: part of the way, or between two directions, the keys average to the stick") {
    const Axis2 sticks[] = {{0.0f, 0.5f}, {0.3f, 0.0f}, {0.4f, 0.7f}, {-0.85f, 0.2f}, {0.25f, -0.25f}};
    // The average is over time, whatever the frame rate.
    for (const double hz : kRates) {
        for (const Axis2 stick : sticks) {
            MapPanKeys keys;
            const Axis2 avg = averagePan(keys, stick, static_cast<int>(10.0 * hz), 1.0 / hz); // 10 s
            CHECK(avg.x == doctest::Approx(stick.x).epsilon(0.02).scale(1.0));
            CHECK(avg.y == doctest::Approx(stick.y).epsilon(0.02).scale(1.0));
        }
    }
}

TEST_CASE("map pan keys: no key changes sooner than one game frame, at any headset rate") {
    const Axis2 sticks[] = {{0.0f, 0.5f}, {0.3f, 0.0f}, {0.4f, 0.7f}, {0.1f, -0.05f}};
    for (const double hz : kRates) {
        for (const Axis2 stick : sticks) {
            MapPanKeys keys;
            MapKeys last = keys.update(stick, 0.0);
            double since = 0.0;
            int changes = 0;
            for (int i = 0; i < static_cast<int>(5.0 * hz); ++i) {
                const MapKeys k = keys.update(stick, 1.0 / hz);
                since += 1.0 / hz;
                if (!(k == last)) {
                    CHECK(since >= kKeyHoldSeconds - 1e-9);
                    since = 0.0;
                    ++changes;
                    last = k;
                }
            }
            CHECK(changes > 0); // part of the way: the keys do change
        }
    }
}

TEST_CASE("map pan keys: at full deflection between two directions the direction holds") {
    const Axis2 sticks[] = {{0.5f, 0.866f}, {-0.966f, -0.259f}, {0.383f, -0.924f}};
    for (const Axis2 stick : sticks) {
        MapPanKeys keys;
        const Axis2 avg = averagePan(keys, stick, 900);
        const float radians = std::atan2(avg.y, avg.x) - std::atan2(stick.y, stick.x);
        CHECK(std::fabs(radians) * 57.2958f < 1.0f); // within a degree
        // As fast as the keys can go that way: at least the octagon's edge, 0.92 of full speed.
        CHECK(std::hypot(avg.x, avg.y) > 0.91f);
    }
}

TEST_CASE("map pan keys: half deflection straight up holds W half the time, in holds of a game frame") {
    MapPanKeys keys;
    int held = 0;
    for (int i = 0; i < 90; ++i) { // 1 s at 90 Hz
        const MapKeys k = keys.update({0.0f, 0.5f}, i == 0 ? 0.0 : kFrame);
        CHECK_FALSE(k.down);
        CHECK_FALSE(k.left);
        CHECK_FALSE(k.right);
        held += k.up ? 1 : 0;
    }
    CHECK(held >= 43);
    CHECK(held <= 47);
}

TEST_CASE("map pan keys: past full deflection the keys stay down and owe nothing afterwards") {
    MapPanKeys keys;
    for (int i = 0; i < 50; ++i) {
        CHECK(keys.update({0.0f, 1.4f}, i == 0 ? 0.0 : kFrame) == MapKeys{.up = true});
    }
    // Back to a quarter: W about a quarter of the time, no run of extra time from the time at full.
    int held = 0;
    for (int i = 0; i < 36; ++i) { // 0.4 s
        held += keys.update({0.0f, 0.25f}, kFrame).up ? 1 : 0;
    }
    CHECK(held >= 6);
    CHECK(held <= 12);
}

TEST_CASE("map pan keys: letting go forgets what was owed") {
    MapPanKeys keys;
    keys.update({0.0f, 0.4f}, 0.0); // holds nothing, and owes 0.4 of the time it is held
    keys.update({0.0f, 0.4f}, kFrame);
    CHECK(keys.update({}, kFrame) == MapKeys{});
    // A fresh start: the first frame at 0.4 holds nothing (0.4 is nearer 0 than 1).
    CHECK(keys.update({0.0f, 0.4f}, kFrame) == MapKeys{});
}

TEST_CASE("map pan keys: a stall in the frames is not paid back as a run of key time") {
    MapPanKeys keys;
    keys.update({0.0f, 0.5f}, 0.0);
    keys.update({0.0f, 0.5f}, 2.0); // a two-second hitch
    // Paid back in full, the hitch would hold W for the whole next 0.4 s; at most a little more than a
    // game frame of it is owed.
    int held = 0;
    for (int i = 0; i < 36; ++i) { // 0.4 s
        held += keys.update({0.0f, 0.5f}, kFrame).up ? 1 : 0;
    }
    CHECK(held <= 27);
}
