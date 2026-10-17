#include "stereo_seq/ssr_noise.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>

using evr::stereo_seq::Eye;
using evr::stereo_seq::RenderTag;
using evr::stereo_seq::ssrCounterFor;
using evr::stereo_seq::ssrNoiseIndex;
using evr::stereo_seq::ssrTickCounter;

namespace {

RenderTag eyeTag(Eye eye, std::uint64_t tick, bool pairInTick = false) {
    RenderTag t;
    t.eye = eye;
    t.tick = tick;
    t.viewApplied = true;
    t.pairInTick = pairInTick;
    return t;
}

// The engine's backend counter, one up per render, as the render-view job reads it.
struct Engine {
    std::uint32_t counter = 7000;
    std::uint32_t render() { return counter++; }
};

// The noise indices one eye received, bit i for index i.
struct Seen {
    std::uint16_t bits = 0;
    void add(std::uint32_t counter) {
        bits = static_cast<std::uint16_t>(bits | (1u << ssrNoiseIndex(counter)));
    }
};

} // namespace

TEST_CASE("SSR noise: the engine's index is the counter's low four bits reversed") {
    const std::uint8_t expected[16] = {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};
    for (std::uint32_t c = 0; c < 16; ++c) {
        CHECK(ssrNoiseIndex(c) == expected[c]);
        CHECK(ssrNoiseIndex(c + 16 * 1234) == expected[c]);
    }
}

TEST_CASE("SSR noise: under Route S both eyes of a tick get the tick's counter and each eye gets all 16") {
    Engine engine;
    Seen left, right;
    int differing = 0;
    for (std::uint64_t tick = 500; tick < 532; ++tick) {
        const std::uint32_t l = ssrCounterFor(eyeTag(Eye::Left, tick), false, true, engine.render());
        const std::uint32_t r = ssrCounterFor(eyeTag(Eye::Right, tick), false, true, engine.render());
        CHECK(l == (tick & 15u));
        CHECK(r == (tick & 15u));
        differing += ssrNoiseIndex(l) != ssrNoiseIndex(r) ? 1 : 0;
        left.add(l);
        right.add(r);
    }
    CHECK(differing == 0);
    CHECK(left.bits == 0xFFFF);
    CHECK(right.bits == 0xFFFF);
}

TEST_CASE("SSR noise: renders without an eye's own view keep the engine's counter") {
    const std::uint32_t engine = 0x12345;
    CHECK(ssrCounterFor(std::nullopt, false, true, engine) ==
          engine); // no tag (out of step, before the base)
    RenderTag mono = eyeTag(Eye::Mono, 900);
    CHECK(ssrCounterFor(mono, false, true, engine) == engine);
    RenderTag noView = eyeTag(Eye::Left, 900);
    noView.viewApplied = false; // the per-eye hook did not write the view: the game's own view
    CHECK(ssrCounterFor(noView, false, true, engine) == engine);
    CHECK(ssrCounterFor(eyeTag(Eye::Right, 0), false, true, engine) == engine); // tick unknown
    // Without per-eye history (per-eye TAA off or failed closed) the jitter phase is the engine's too.
    CHECK(ssrCounterFor(eyeTag(Eye::Left, 900), false, false, engine) == engine);
    CHECK(ssrCounterFor(eyeTag(Eye::Right, 900), false, false, engine) == engine);
}

TEST_CASE("SSR noise: with alternate eyes the eyes of a pair share a counter and each eye gets all 16") {
    Engine engine;
    Seen left, right;
    for (std::uint64_t tick = 600; tick < 664; tick += 2) {
        // Eye L renders tick 2k, eye R tick 2k + 1 (one eye per tick: no pairInTick).
        const std::uint32_t l = ssrCounterFor(eyeTag(Eye::Left, tick), true, true, engine.render());
        const std::uint32_t r = ssrCounterFor(eyeTag(Eye::Right, tick + 1), true, true, engine.render());
        CHECK(l == ((tick / 2) & 15u));
        CHECK(r == l);
        left.add(l);
        right.add(r);
    }
    CHECK(left.bits == 0xFFFF);
    CHECK(right.bits == 0xFFFF);
}

TEST_CASE("SSR noise: with alternate eyes started on an odd tick each eye still walks all 16 in order") {
    // A mono render restarts the alternation with eye L, here on an odd tick: eye L at 2k + 1 gets k, its eye
    // R at 2k + 2 gets k + 1. Each eye's consecutive renders still get consecutive counters.
    Engine engine;
    Seen left, right;
    std::uint32_t lastLeft = 0;
    std::uint32_t lastRight = 0;
    for (std::uint64_t tick = 701; tick < 765; tick += 2) {
        const std::uint32_t l = ssrCounterFor(eyeTag(Eye::Left, tick), true, true, engine.render());
        const std::uint32_t r = ssrCounterFor(eyeTag(Eye::Right, tick + 1), true, true, engine.render());
        CHECK(l == ((tick / 2) & 15u));
        CHECK(r == ((l + 1) & 15u));
        if (tick > 701) {
            CHECK(l == ((lastLeft + 1) & 15u));
            CHECK(r == ((lastRight + 1) & 15u));
        }
        lastLeft = l;
        lastRight = r;
        left.add(l);
        right.add(r);
    }
    CHECK(left.bits == 0xFFFF);
    CHECK(right.bits == 0xFFFF);
}

TEST_CASE(
    "SSR noise: with adaptive eyes a tick that renders both eyes uses the tick, a one-eye tick half of it") {
    Engine engine;
    // Both eyes of tick 801 (pairInTick: eye R nested in its eye L's tick).
    CHECK(ssrCounterFor(eyeTag(Eye::Left, 801, true), true, true, engine.render()) == (801u & 15u));
    CHECK(ssrCounterFor(eyeTag(Eye::Right, 801, true), true, true, engine.render()) == (801u & 15u));
    // Then one eye per tick while the processor falls behind.
    CHECK(ssrCounterFor(eyeTag(Eye::Left, 802), true, true, engine.render()) == ((802u / 2) & 15u));
    CHECK(ssrCounterFor(eyeTag(Eye::Right, 803), true, true, engine.render()) == ((803u / 2) & 15u));
    // Under Route S (no alternate eyes) a tag without pairInTick still uses the tick.
    CHECK(ssrCounterFor(eyeTag(Eye::Right, 803), false, true, engine.render()) == (803u & 15u));
}

TEST_CASE("SSR noise: the tick counter walks like flat play's renders") {
    CHECK(ssrTickCounter(16, false) == 0);
    CHECK(ssrTickCounter(17, false) == 1);
    CHECK(ssrTickCounter(31, false) == 15);
    CHECK(ssrTickCounter(33, true) == 0);
    CHECK(ssrTickCounter(34, true) == 1);
}
