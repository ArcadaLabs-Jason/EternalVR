#include "features/menu/map_pan_keys.hpp"

#include <doctest/doctest.h>

#include <cmath>

using evr::input::Axis2;
using evr::menu::MapKeys;
using evr::menu::MapPanKeys;

namespace {

// What the game makes of the keys: normalize(right - left, up - down).
Axis2 gamePan(const MapKeys& k) {
    const float x = (k.right ? 1.0f : 0.0f) - (k.left ? 1.0f : 0.0f);
    const float y = (k.up ? 1.0f : 0.0f) - (k.down ? 1.0f : 0.0f);
    const float m = std::hypot(x, y);
    return m > 0.0f ? Axis2{x / m, y / m} : Axis2{};
}

// The game's pan averaged over `frames` frames of the stick held at `stick`.
Axis2 averagePan(MapPanKeys& keys, Axis2 stick, int frames) {
    Axis2 sum;
    for (int i = 0; i < frames; ++i) {
        const Axis2 p = gamePan(keys.update(stick));
        sum.x += p.x;
        sum.y += p.y;
    }
    return {sum.x / static_cast<float>(frames), sum.y / static_cast<float>(frames)};
}

} // namespace

TEST_CASE("map pan keys: a stick at rest holds no key") {
    MapPanKeys keys;
    CHECK(keys.update({}) == MapKeys{});
    CHECK(keys.update({std::nanf(""), 0.0f}) == MapKeys{});
}

TEST_CASE("map pan keys: full deflection along an axis or a diagonal holds its keys every frame") {
    MapPanKeys keys;
    for (int i = 0; i < 10; ++i) {
        CHECK(keys.update({0.0f, 1.0f}) == MapKeys{.up = true});
    }
    keys.reset();
    for (int i = 0; i < 10; ++i) {
        CHECK(keys.update({1.0f, 0.0f}) == MapKeys{.right = true});
    }
    keys.reset();
    const float d = std::sqrt(0.5f);
    for (int i = 0; i < 10; ++i) {
        CHECK(keys.update({-d, -d}) == MapKeys{.left = true, .down = true});
    }
}

TEST_CASE("map pan keys: part of the way, or between two directions, the keys average to the stick") {
    const Axis2 sticks[] = {{0.0f, 0.5f}, {0.3f, 0.0f}, {0.4f, 0.7f}, {-0.85f, 0.2f}, {0.25f, -0.25f}};
    for (const Axis2 stick : sticks) {
        MapPanKeys keys;
        const Axis2 avg = averagePan(keys, stick, 900); // 10 s at 90 Hz
        CHECK(avg.x == doctest::Approx(stick.x).epsilon(0.01).scale(1.0));
        CHECK(avg.y == doctest::Approx(stick.y).epsilon(0.01).scale(1.0));
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

TEST_CASE("map pan keys: half deflection straight up holds W every other frame") {
    MapPanKeys keys;
    int held = 0;
    for (int i = 0; i < 20; ++i) {
        const MapKeys k = keys.update({0.0f, 0.5f});
        CHECK_FALSE(k.down);
        CHECK_FALSE(k.left);
        CHECK_FALSE(k.right);
        held += k.up ? 1 : 0;
    }
    CHECK(held == 10);
}

TEST_CASE("map pan keys: past full deflection the keys stay down and owe nothing afterwards") {
    MapPanKeys keys;
    for (int i = 0; i < 50; ++i) {
        CHECK(keys.update({0.0f, 1.4f}) == MapKeys{.up = true});
    }
    // Back to a quarter: W about one frame in four, no run of extra frames from the time at full.
    int held = 0;
    for (int i = 0; i < 8; ++i) {
        held += keys.update({0.0f, 0.25f}).up ? 1 : 0;
    }
    CHECK(held == 2);
}

TEST_CASE("map pan keys: letting go forgets what was owed") {
    MapPanKeys keys;
    keys.update({0.0f, 0.4f}); // owes 0.4 after holding nothing
    CHECK(keys.update({}) == MapKeys{});
    // A fresh start: the first frame at 0.4 holds nothing (0.4 is nearer 0 than 1).
    CHECK(keys.update({0.0f, 0.4f}) == MapKeys{});
}
