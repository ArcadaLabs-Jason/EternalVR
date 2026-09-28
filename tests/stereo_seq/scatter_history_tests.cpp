#include "stereo_seq/scatter_history.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>

using evr::stereo_seq::Eye;
using evr::stereo_seq::kScatterLastFrame;
using evr::stereo_seq::ScatterHistory;
using evr::stereo_seq::ScatterPair;
using evr::stereo_seq::ScatterState;

namespace {

// Distinct fake image pointers: eye L's pairs 1/2 and 3/4, eye R's 5/6 and 7/8.
void* image(std::uintptr_t i) {
    return reinterpret_cast<void*>(i * 0x100);
}

ScatterHistory ready() {
    ScatterHistory h;
    h.reset({ScatterPair{image(1), image(2)}, ScatterPair{image(3), image(4)}},
            {ScatterPair{image(5), image(6)}, ScatterPair{image(7), image(8)}});
    return h;
}

ScatterState stateWith(std::uint32_t lastFrame, std::byte fill) {
    ScatterState s{};
    s.fill(fill);
    std::memcpy(s.data() + kScatterLastFrame, &lastFrame, sizeof(lastFrame));
    return s;
}

std::uint32_t lastFrame(const ScatterState& s) {
    std::uint32_t v = 0;
    std::memcpy(&v, s.data() + kScatterLastFrame, sizeof(v));
    return v;
}

} // namespace

TEST_CASE("scatter history: each eye reads the pair it wrote last, whatever the render counter's parity") {
    ScatterHistory h = ready();
    REQUIRE(h.ready());
    const ScatterState s = stateWith(10, std::byte{1});
    // Eye L on odd counters, eye R on even ones (two renders per tick).
    const auto l1 = h.beforeRender(Eye::Left, 11, s);
    const auto r1 = h.beforeRender(Eye::Right, 12, s);
    const auto l2 = h.beforeRender(Eye::Left, 13, s);
    const auto r2 = h.beforeRender(Eye::Right, 14, s);
    const auto l3 = h.beforeRender(Eye::Left, 15, s);
    // The written pair is the counter's slot, the read pair the other one.
    CHECK(l1.slots[0] == ScatterPair{image(1), image(2)}); // read
    CHECK(l1.slots[1] == ScatterPair{image(3), image(4)}); // written
    CHECK(l2.slots[0] == ScatterPair{image(3), image(4)}); // reads what l1 wrote
    CHECK(l2.slots[1] == ScatterPair{image(1), image(2)});
    CHECK(l3.slots[0] == ScatterPair{image(1), image(2)});
    CHECK(r1.slots[1] == ScatterPair{image(5), image(6)}); // read (even counter: slot 1 is read)
    CHECK(r1.slots[0] == ScatterPair{image(7), image(8)}); // written
    CHECK(r2.slots[1] == ScatterPair{image(7), image(8)}); // reads what r1 wrote
    // Neither eye ever gets the other's images.
    for (const auto* plan : {&l1, &l2, &l3}) {
        for (const auto& pair : plan->slots) {
            CHECK((pair == ScatterPair{image(1), image(2)} || pair == ScatterPair{image(3), image(4)}));
        }
    }
    for (const auto* plan : {&r1, &r2}) {
        for (const auto& pair : plan->slots) {
            CHECK((pair == ScatterPair{image(5), image(6)} || pair == ScatterPair{image(7), image(8)}));
        }
    }
}

TEST_CASE("scatter history: the state follows the eye, and each eye's first two renders clear its history") {
    ScatterHistory h = ready();
    // Eye L's first render: the engine's struct already holds its state, but without a last frame (its pairs
    // hold what the engine filtered before the eyes had pairs of their own).
    const auto l1 = h.beforeRender(Eye::Left, 1, stateWith(5, std::byte{1}));
    REQUIRE(l1.load);
    CHECK(lastFrame(*l1.load) == 0);
    CHECK((*l1.load)[0] == std::byte{1});
    // Eye R's first render: eye L's state without a last frame.
    const auto r1 = h.beforeRender(Eye::Right, 2, stateWith(1, std::byte{2}));
    REQUIRE(r1.load);
    CHECK(lastFrame(*r1.load) == 0);
    CHECK((*r1.load)[0] == std::byte{2});
    // Eye L's second render: its state as eye R found it, and its other pair cleared too.
    const auto l2 = h.beforeRender(Eye::Left, 3, stateWith(2, std::byte{3}));
    REQUIRE(l2.load);
    CHECK(lastFrame(*l2.load) == 0);
    CHECK((*l2.load)[0] == std::byte{2});
    CHECK(l2.slots[0] != l1.slots[0]); // the pair it reads now is the other one: both cleared once
    // Eye R's second render: what its own render left, its other pair cleared.
    const auto r2 = h.beforeRender(Eye::Right, 4, stateWith(3, std::byte{4}));
    REQUIRE(r2.load);
    CHECK(lastFrame(*r2.load) == 0);
    CHECK((*r2.load)[0] == std::byte{3});
    CHECK(r2.slots[1] != r1.slots[1]);
    // From the third render each eye keeps its own last frame, passed off as the render just before.
    const auto l3 = h.beforeRender(Eye::Left, 5, stateWith(4, std::byte{5}));
    REQUIRE(l3.load);
    CHECK(lastFrame(*l3.load) == 4);
    CHECK((*l3.load)[0] == std::byte{4});
    const auto r3 = h.beforeRender(Eye::Right, 6, stateWith(5, std::byte{6}));
    REQUIRE(r3.load);
    CHECK(lastFrame(*r3.load) == 5);
    CHECK((*r3.load)[0] == std::byte{5});
    // Two renders of one eye in a row (a mono frame between ticks): no swap.
    CHECK_FALSE(h.beforeRender(Eye::Right, 7, stateWith(6, std::byte{7})).load);
    // A mono render is eye L's: eye L's state comes back; the next eye L render keeps it.
    CHECK(h.beforeRender(Eye::Mono, 8, stateWith(7, std::byte{8})).load);
    CHECK_FALSE(h.beforeRender(Eye::Left, 9, stateWith(8, std::byte{9})).load);
}

TEST_CASE("scatter history: after a resize each eye clears both pairs again and keeps its images") {
    ScatterHistory h = ready();
    for (std::uint32_t c = 1; c <= 6; ++c) {
        h.beforeRender(c % 2 ? Eye::Left : Eye::Right, c, stateWith(c - 1, std::byte{0}));
    }
    h.invalidate();
    const auto l = h.beforeRender(Eye::Left, 7, stateWith(6, std::byte{0}));
    const auto r = h.beforeRender(Eye::Right, 8, stateWith(7, std::byte{0}));
    const auto l2 = h.beforeRender(Eye::Left, 9, stateWith(8, std::byte{0}));
    const auto l3 = h.beforeRender(Eye::Right, 10, stateWith(9, std::byte{0}));
    const auto l4 = h.beforeRender(Eye::Left, 11, stateWith(10, std::byte{0}));
    for (const auto* plan : {&l, &r, &l2, &l3}) {
        REQUIRE(plan->load);
        CHECK(lastFrame(*plan->load) == 0);
    }
    REQUIRE(l4.load);
    CHECK(lastFrame(*l4.load) == 10);
    for (const auto& pair : l.slots) {
        CHECK((pair == ScatterPair{image(1), image(2)} || pair == ScatterPair{image(3), image(4)}));
    }
    for (const auto& pair : r.slots) {
        CHECK((pair == ScatterPair{image(5), image(6)} || pair == ScatterPair{image(7), image(8)}));
    }
}

TEST_CASE("scatter history: mono renders are eye L's; a reset forgets both eyes") {
    ScatterHistory h = ready();
    const ScatterState s = stateWith(1, std::byte{0});
    const auto m1 = h.beforeRender(Eye::Mono, 1, s);
    const auto l1 = h.beforeRender(Eye::Left, 2, s);
    // Eye L's two clearing renders, one of them mono: no eye swap, only the cleared last frame.
    REQUIRE(m1.load);
    REQUIRE(l1.load);
    CHECK(lastFrame(*m1.load) == 0);
    CHECK(lastFrame(*l1.load) == 0);
    CHECK_FALSE(h.beforeRender(Eye::Left, 3, s).load);
    CHECK(l1.slots[1] == m1.slots[1]); // eye L (even: reads slot 1) reads what the mono render (odd) wrote
    h.beforeRender(Eye::Right, 4, s);
    h.reset({ScatterPair{image(1), image(2)}, ScatterPair{image(3), image(4)}},
            {ScatterPair{image(5), image(6)}, ScatterPair{image(7), image(8)}});
    const auto r = h.beforeRender(Eye::Right, 5, stateWith(9, std::byte{0}));
    REQUIRE(r.load);
    CHECK(lastFrame(*r.load) == 0); // a first render again
}

TEST_CASE("scatter history: not ready without all eight images") {
    ScatterHistory h;
    CHECK_FALSE(h.ready());
    h.reset({ScatterPair{image(1), image(2)}, ScatterPair{image(3), image(4)}},
            {ScatterPair{image(5), nullptr}, ScatterPair{image(7), image(8)}});
    CHECK_FALSE(h.ready());
}
