#include "stereo_seq/object_prev.hpp"

#include <doctest/doctest.h>

#include <cstdint>

using evr::stereo_seq::Eye;
using evr::stereo_seq::ObjectPrevSlot;

namespace {

// The engine's pick of the previous frame's buffer from the counter it is given.
int prevSlot(std::uint32_t given) {
    return static_cast<int>((given + 2u) % 3u);
}

} // namespace

TEST_CASE("object prev: eye R reads its own render of the tick before, eye L the render before") {
    ObjectPrevSlot s;
    // Tick 1: eye L 11, eye R 12 (eye R's first render: nothing of its own yet).
    CHECK(s.counterFor(Eye::Left, 11) == 11);
    CHECK(s.counterFor(Eye::Right, 12) == 12);
    // Tick 2: eye L 13 reads 12 (eye R's, the render before: the tick before either way).
    CHECK(prevSlot(s.counterFor(Eye::Left, 13)) == 12 % 3);
    // Eye R 14 reads 12, its own render of the tick before, not 13 (eye L's of this frame).
    CHECK(prevSlot(s.counterFor(Eye::Right, 14)) == 12 % 3);
    // Asked again in the same render (the second buffer): the same answer.
    CHECK(prevSlot(s.counterFor(Eye::Right, 14)) == 12 % 3);
    for (std::uint32_t c = 15; c < 40; c += 2) {
        CHECK(prevSlot(s.counterFor(Eye::Left, c)) == (c - 1) % 3);
        CHECK(prevSlot(s.counterFor(Eye::Right, c + 1)) == (c - 1) % 3);
    }
}

TEST_CASE("object prev: after a gap eye R keeps the engine's pick") {
    ObjectPrevSlot s;
    s.counterFor(Eye::Right, 12);
    // A mono render between ticks (13), then eye L 14 and eye R 15: eye R's last render is three back,
    // no longer the frame before, so the engine's own pick (14) stands.
    CHECK(s.counterFor(Eye::Mono, 13) == 13);
    CHECK(s.counterFor(Eye::Left, 14) == 14);
    CHECK(s.counterFor(Eye::Right, 15) == 15);
    // Back in step on the next tick.
    CHECK(prevSlot(s.counterFor(Eye::Right, 17)) == 15 % 3);
}
