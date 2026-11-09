#include "features/pacing/pace_policy.hpp"

#include <doctest/doctest.h>

#include <cstdint>

using namespace evr::pacing;

namespace {

constexpr double kPeriod = 1.0 / 90.0;

// Feeds `sw` 90 Hz headset frames for `seconds` from `start`, the game handing over `imageHz` images a
// second, each of which took `gameSeconds` from its release (none noted when negative); returns how often it
// switched.
int feedTimed(HoldSwitch& sw, double start, double seconds, double imageHz, double gameSeconds) {
    int changes = 0;
    std::uint64_t handOvers = 0;
    const int frames = static_cast<int>(seconds * 90.0);
    for (int i = 1; i <= frames; ++i) {
        const auto now = static_cast<std::uint64_t>(i * imageHz / 90.0 + 1e-9);
        for (; handOvers < now; ++handOvers) {
            if (gameSeconds >= 0.0) {
                sw.noteGameTime(gameSeconds);
            }
        }
        changes += sw.frame(start + i / 90.0, handOvers, kPeriod) ? 1 : 0;
    }
    return changes;
}

} // namespace

TEST_CASE("hold gate: a game that draws its images within the period but repeats (hitches) stays held") {
    // 85 images a second at 90 Hz (about 5.6% repeats) lets the game go without the gate ...
    HoldSwitch plain;
    CHECK(feedTimed(plain, 1.0, 2.5, 85.0, 0.0065) == 1);
    CHECK_FALSE(plain.held());
    // ... but each image took 6.5 ms from its release: the repeats were not the game's own time.
    HoldSwitch gated;
    gated.setLatencyGate(true);
    CHECK(feedTimed(gated, 1.0, 5.0, 85.0, 0.0065) == 0);
    CHECK(gated.held());
}

TEST_CASE("hold gate: a game whose images take longer than the period is let go") {
    HoldSwitch gated;
    gated.setLatencyGate(true);
    CHECK(feedTimed(gated, 1.0, 2.5, 75.0, 0.014) == 1); // GPU-bound below the refresh
    CHECK_FALSE(gated.held());
}

TEST_CASE("hold gate: a third of the images over the period still holds; two thirds let go") {
    const auto run = [](int slowOfThree) {
        HoldSwitch sw;
        sw.setLatencyGate(true);
        std::uint64_t handOvers = 0;
        int changes = 0;
        for (int i = 1; i <= 90 * 3; ++i) {
            const auto now = static_cast<std::uint64_t>(i * 85.0 / 90.0 + 1e-9);
            for (; handOvers < now; ++handOvers) {
                sw.noteGameTime(static_cast<int>(handOvers % 3) < slowOfThree ? 0.014 : 0.006);
            }
            changes += sw.frame(1.0 + i / 90.0, handOvers, kPeriod) ? 1 : 0;
        }
        return changes;
    };
    CHECK(run(1) == 0);
    CHECK(run(2) == 1);
}

TEST_CASE("hold gate: windows without game times decide as without the gate") {
    HoldSwitch gated;
    gated.setLatencyGate(true);
    CHECK(feedTimed(gated, 1.0, 2.5, 85.0, -1.0) == 1);
    CHECK_FALSE(gated.held());
}
