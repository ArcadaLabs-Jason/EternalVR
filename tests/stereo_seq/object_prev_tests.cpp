#include "stereo_seq/object_prev.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>

using evr::stereo_seq::Eye;
using evr::stereo_seq::ObjectRing;

namespace {

// The engine's slots from the counters it is given.
int currentSlot(const ObjectRing::Picks& p) {
    return static_cast<int>(p.current % 3u);
}
int previousSlot(const ObjectRing::Picks& p) {
    return static_cast<int>((p.previous + 2u) % 3u);
}

// Replays renders and checks the ring's rules against what each slot holds: the eye and tick last uploaded,
// and the latest render that read it.
struct Ring {
    ObjectRing ring;
    std::array<Eye, 3> eyeIn{};
    std::array<std::uint64_t, 3> tickIn{};
    std::array<std::uint32_t, 3> lastRead{};
    std::array<bool, 3> read{};

    ObjectRing::Picks render(Eye eye, std::uint64_t tick, std::uint32_t counter) {
        const ObjectRing::Picks p = ring.picksFor(eye, tick, counter);
        // Asked again by the other picks and the upload: the same answer.
        const ObjectRing::Picks again = ring.picksFor(eye, tick, counter);
        CHECK(again.current == p.current);
        CHECK(again.previous == p.previous);
        const int slot = currentSlot(p);
        // Written no sooner than two renders after its last read, as in mono.
        if (read[slot]) {
            CHECK(counter - lastRead[slot] >= 2u);
        }
        eyeIn[slot] = eye;
        tickIn[slot] = tick;
        lastRead[slot] = counter;
        read[slot] = true;
        lastRead[previousSlot(p)] = counter;
        read[previousSlot(p)] = true;
        return p;
    }
};

} // namespace

TEST_CASE("object ring: eye L keeps one slot, eye R alternates, both read the tick before's eye R") {
    Ring r;
    r.render(Eye::Left, 100, 11);
    r.render(Eye::Right, 100, 12);
    int leftSlot = -1;
    for (std::uint64_t tick = 101; tick < 130; ++tick) {
        const std::uint32_t c = 11u + static_cast<std::uint32_t>(tick - 100) * 2u;
        const ObjectRing::Picks left = r.render(Eye::Left, tick, c);
        if (leftSlot < 0) {
            leftSlot = currentSlot(left);
        }
        CHECK(currentSlot(left) == leftSlot);
        CHECK(r.eyeIn[previousSlot(left)] == Eye::Right);
        CHECK(r.tickIn[previousSlot(left)] == tick - 1);
        const ObjectRing::Picks right = r.render(Eye::Right, tick, c + 1u);
        CHECK(currentSlot(right) != leftSlot);
        CHECK(previousSlot(right) == previousSlot(left));
        CHECK(r.tickIn[previousSlot(right)] == tick - 1);
        CHECK(r.eyeIn[previousSlot(right)] == Eye::Right);
    }
}

TEST_CASE("object ring: no render writes a slot another render of its tick reads") {
    Ring r;
    std::uint32_t c = 20;
    for (std::uint64_t tick = 1; tick < 40; ++tick, c += 2u) {
        const ObjectRing::Picks left = r.render(Eye::Left, tick, c);
        const ObjectRing::Picks right = r.render(Eye::Right, tick, c + 1u);
        CHECK(currentSlot(right) != currentSlot(left));
        CHECK(currentSlot(right) != previousSlot(left));
        if (tick > 1) { // the first tick's eye R has no tick before: it reads eye L's frame
            CHECK(currentSlot(left) != previousSlot(right));
        }
    }
}

TEST_CASE("object ring: mono renders go round the ring, each reading the render before") {
    Ring r;
    int before = -1;
    for (std::uint32_t c = 50; c < 70; ++c) {
        const ObjectRing::Picks p = r.render(Eye::Mono, 0, c);
        if (before >= 0) {
            CHECK(previousSlot(p) == before);
        }
        CHECK(currentSlot(p) != before);
        before = currentSlot(p);
    }
}

TEST_CASE("object ring: the first stereo tick after mono") {
    Ring r;
    const ObjectRing::Picks mono = r.render(Eye::Mono, 0, 40);
    const ObjectRing::Picks left = r.render(Eye::Left, 7, 41);
    CHECK(previousSlot(left) == currentSlot(mono)); // the render just before
    const ObjectRing::Picks right = r.render(Eye::Right, 7, 42);
    CHECK(currentSlot(right) != currentSlot(left));
    // Nothing of the tick before: the render just before, eye L's of this tick (no object motion for a tick).
    CHECK(previousSlot(right) == currentSlot(left));
    // The next tick follows the pattern: both read eye R's.
    const ObjectRing::Picks next = r.render(Eye::Left, 8, 43);
    CHECK(previousSlot(next) == currentSlot(right));
    CHECK(previousSlot(r.render(Eye::Right, 8, 44)) == currentSlot(right));
}

TEST_CASE("object ring: a tick without eye R, and eye R without its eye L") {
    Ring r;
    r.render(Eye::Left, 1, 10);
    r.render(Eye::Right, 1, 11);
    const ObjectRing::Picks lonely = r.render(Eye::Left, 2, 12); // eye R skipped this tick
    CHECK(r.eyeIn[previousSlot(lonely)] == Eye::Right);
    const ObjectRing::Picks left = r.render(Eye::Left, 3, 13);
    CHECK(previousSlot(left) == currentSlot(lonely)); // the tick before had only eye L
    const ObjectRing::Picks right = r.render(Eye::Right, 3, 14);
    CHECK(r.tickIn[previousSlot(right)] == 2);
    // An eye R whose eye L never came: still a slot of its own and the tick before's frame.
    const ObjectRing::Picks lone = r.render(Eye::Right, 4, 15);
    CHECK(r.tickIn[previousSlot(lone)] == 3);
    CHECK(r.eyeIn[previousSlot(lone)] == Eye::Right);
}

TEST_CASE("object ring: renders between ticks never make eye R write a slot eye L uses") {
    // A mono render that still carries the last tick (seq_hooks tags mono renders with the latest tick), then
    // stereo again: eye R's tick before's eye R is in the one slot eye L did not touch.
    Ring r;
    std::uint32_t c = 30;
    for (std::uint64_t tick = 1; tick <= 3; ++tick) {
        r.render(Eye::Left, tick, c++);
        r.render(Eye::Right, tick, c++);
    }
    r.render(Eye::Mono, 3, c++);
    const ObjectRing::Picks left = r.render(Eye::Left, 4, c++);
    const ObjectRing::Picks right = r.render(Eye::Right, 4, c++);
    CHECK(currentSlot(right) != currentSlot(left));
    CHECK(currentSlot(right) != previousSlot(left));
    CHECK(previousSlot(right) == previousSlot(left)); // eye L's previous frame: the mono render
    // Two untagged renders between ticks, and one between eye L and eye R.
    r.render(Eye::Mono, 0, c++);
    r.render(Eye::Mono, 0, c++);
    r.render(Eye::Left, 5, c++);
    r.render(Eye::Right, 5, c++);
    const ObjectRing::Picks l6 = r.render(Eye::Left, 6, c++);
    r.render(Eye::Mono, 0, c++);
    const ObjectRing::Picks r6 = r.render(Eye::Right, 6, c++);
    CHECK(currentSlot(r6) != currentSlot(l6));
    // Back to the steady pattern: both eyes read the tick before's eye R.
    r.render(Eye::Left, 7, c++);
    r.render(Eye::Right, 7, c++);
    const ObjectRing::Picks l8 = r.render(Eye::Left, 8, c++);
    const ObjectRing::Picks r8 = r.render(Eye::Right, 8, c++);
    CHECK(r.eyeIn[previousSlot(l8)] == Eye::Right);
    CHECK(r.tickIn[previousSlot(l8)] == 7);
    CHECK(previousSlot(r8) == previousSlot(l8));
}

TEST_CASE("object ring: every mix of up to three renders between ticks keeps the two-render margin") {
    // Each step: untagged, mono / eye L / eye R on the same tick, or on the next one. Ring::render checks
    // that no slot is written sooner than two renders after its last read.
    struct Step {
        Eye eye;
        bool untagged;
        std::uint64_t advance;
    };
    const Step steps[] = {{Eye::Mono, true, 0},  {Eye::Mono, false, 0}, {Eye::Mono, false, 1},
                          {Eye::Left, false, 0}, {Eye::Left, false, 1}, {Eye::Right, false, 0},
                          {Eye::Right, false, 1}};
    constexpr int kSteps = static_cast<int>(sizeof(steps) / sizeof(steps[0]));
    for (int n = 1; n <= 3; ++n) {
        int combos = 1;
        for (int i = 0; i < n; ++i) {
            combos *= kSteps;
        }
        for (int k = 0; k < combos; ++k) {
            Ring r;
            std::uint32_t c = 100;
            std::uint64_t tick = 1;
            for (; tick <= 3; ++tick) {
                r.render(Eye::Left, tick, c++);
                r.render(Eye::Right, tick, c++);
            }
            --tick;
            for (int i = 0, rest = k; i < n; ++i, rest /= kSteps) {
                const Step& s = steps[rest % kSteps];
                tick += s.advance;
                r.render(s.eye, s.untagged ? 0u : tick, c++);
            }
            for (int i = 0; i < 3; ++i) {
                ++tick;
                const ObjectRing::Picks left = r.render(Eye::Left, tick, c++);
                const ObjectRing::Picks right = r.render(Eye::Right, tick, c++);
                CHECK(currentSlot(right) != currentSlot(left));
                CHECK(currentSlot(right) != previousSlot(left));
            }
        }
    }
}

TEST_CASE("object ring: renders first asked out of counter order are reported") {
    ObjectRing ring;
    bool inOrder = false;
    ring.picksFor(Eye::Left, 1, 10, &inOrder);
    CHECK(inOrder);
    ring.picksFor(Eye::Right, 1, 11, &inOrder);
    CHECK(inOrder);
    inOrder = false;
    ring.picksFor(Eye::Left, 1, 10, &inOrder); // asked again: a remembered answer, not a first ask
    CHECK_FALSE(inOrder);
    ring.picksFor(Eye::Left, 2, 13, &inOrder); // 12 was skipped
    CHECK_FALSE(inOrder);
    ring.picksFor(Eye::Right, 2, 14, &inOrder);
    CHECK(inOrder);
}
