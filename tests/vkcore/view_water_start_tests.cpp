#include "vkcore/view_water_start.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace ws = evr::vkcore::view_water_start;

namespace {

using State = std::array<std::byte, ws::kStateSize>;

State filled(std::uint8_t value) {
    State s;
    s.fill(std::byte{value});
    return s;
}

std::uint8_t at(const State& s, std::size_t offset) {
    return std::to_integer<std::uint8_t>(s[offset]);
}

ws::View0 view0(bool begun, bool ended, bool sameWorld = true) {
    ws::View0 v;
    v.dispatched = true;
    v.begun = begun;
    v.ended = ended;
    v.sameWorld = sameWorld;
    return v;
}

} // namespace

TEST_CASE("view 1 starts from the world's state before view 0's setup, from view 0's entry after it began") {
    CHECK(ws::decide(view0(false, false)).start == ws::Start::BeforeView0);
    CHECK(ws::decide(view0(true, false)).start == ws::Start::DuringView0);
    CHECK(ws::decide(view0(true, true)).start == ws::Start::AfterView0);
    CHECK(ws::decide(view0(true, true)).left == ws::Left::None);
}

TEST_CASE("view 1's render is left to the engine without view 0, for another world or view 0's next frame") {
    ws::View0 alone;
    alone.dispatched = false;
    CHECK(ws::decide(alone).start == ws::Start::Engine);
    CHECK(ws::decide(alone).left == ws::Left::NotDispatched);
    CHECK_FALSE(ws::shouldWait(alone, true));
    CHECK(ws::decide(view0(true, true, false)).left == ws::Left::OtherWorld);
    ws::View0 later = view0(false, false);
    later.later = true;
    CHECK(ws::decide(later).left == ws::Left::Later);
    CHECK_FALSE(ws::shouldWait(later, true));
    // A remake (grid mesh, spectrum) is waited for; if the wait runs out view 1 still starts on its copy.
    CHECK(ws::shouldWait(view0(false, false), true));
    CHECK(ws::shouldWait(view0(true, false), true));
    CHECK_FALSE(ws::shouldWait(view0(true, true), true));
    CHECK_FALSE(ws::shouldWait(view0(false, false), false));
}

TEST_CASE("the copy is the world's with the start fields from view 0's entry and view 1's own grid matrix") {
    const State world = filled(1);
    const State entry = filled(2);
    std::array<std::byte, ws::kGridMatrix.size> grid;
    grid.fill(std::byte{3});
    State copy = filled(0);
    REQUIRE(ws::startState(copy, world, entry, grid));
    for (const ws::Range& r : ws::kStartRanges) {
        CHECK(at(copy, r.at) == 2);
        CHECK(at(copy, r.at + r.size - 1) == 2);
    }
    CHECK(at(copy, ws::kGridMatrix.at) == 3);
    CHECK(at(copy, ws::kGridMatrix.at + ws::kGridMatrix.size - 1) == 3);
    // The rest is the world's: the ripple hits (+0x14), the grid mesh (+0xC18), the indices' neighbours.
    CHECK(at(copy, 0x14) == 1);
    CHECK(at(copy, 0xC18) == 1);
    CHECK(at(copy, 0xC1F) == 1);
    CHECK(at(copy, 0xC3C) == 1);
    CHECK(at(copy, 0xC40) == 1);
    // The displacement, caustics and ripple indices (+0xC30..+0xC3B) and the frames before them are view 0's.
    CHECK(at(copy, 0xC20) == 2);
    CHECK(at(copy, 0xC3B) == 2);
}

TEST_CASE("before view 0's setup the copy is the world's; without its own matrix view 1 takes view 0's") {
    const State world = filled(1);
    const State entry = filled(2);
    State copy = filled(0);
    REQUIRE(ws::startState(copy, world, {}, {}));
    CHECK(copy == world);
    REQUIRE(ws::startState(copy, world, entry, {}));
    CHECK(at(copy, ws::kGridMatrix.at) == 2);
    CHECK(at(copy, 0x14) == 1);
}

TEST_CASE("view 1 steps the world's water only when view 0's setup ended with no water and did nothing") {
    ws::View0End none;
    none.ended = true;
    CHECK(ws::view1StepsWorld(none, 2));
    CHECK_FALSE(ws::view1StepsWorld(none, 0)); // neither view sees water
    ws::View0End open = none;
    open.ended = false; // not known yet
    CHECK_FALSE(ws::view1StepsWorld(open, 2));
    ws::View0End water = none;
    water.surfaces = 1;
    CHECK_FALSE(ws::view1StepsWorld(water, 2));
    ws::View0End stepped = none;
    stepped.stepped = true;
    CHECK_FALSE(ws::view1StepsWorld(stepped, 2));
    ws::View0End moved = none;
    moved.moved = true;
    CHECK_FALSE(ws::view1StepsWorld(moved, 2));
}

TEST_CASE("the write back takes the setup's own fields from the copy and leaves the rest of the world's") {
    State world = filled(1);
    const State copy = filled(2);
    REQUIRE(ws::writeBack(world, copy));
    for (const ws::Range& r : ws::kSetupWrites) {
        CHECK(at(world, r.at) == 2);
        CHECK(at(world, r.at + r.size - 1) == 2);
    }
    CHECK(at(world, 0x14) == 1);  // the ripple hits
    CHECK(at(world, 0xC20) == 1); // the last-step frames: the jobs write those
    CHECK(at(world, 0xC2F) == 1);
    CHECK(at(world, 0xC3C) == 1);
    CHECK(at(world, ws::kGridMatrix.at) == 1); // view 0's grid matrix stays
    std::array<std::byte, 16> small{};
    CHECK_FALSE(ws::writeBack(small, copy));
}

TEST_CASE("view 1's own grid matrix is used only from the frame just before, for the same world") {
    CHECK(ws::ownGridUsable(true, 41, 42));
    CHECK(ws::ownGridUsable(true, 0xFFFFFFFFu, 0)); // the counter wraps
    CHECK_FALSE(ws::ownGridUsable(false, 41, 42));
    CHECK_FALSE(ws::ownGridUsable(true, 40, 42)); // a frame between (a load, a render left to the engine)
    CHECK_FALSE(ws::ownGridUsable(true, 42, 42));
    CHECK_FALSE(ws::ownGridUsable(true, 0, 42)); // never made
}

TEST_CASE("spans of another size write nothing") {
    const State world = filled(1);
    State copy = filled(0);
    std::array<std::byte, 16> small{};
    CHECK_FALSE(ws::startState(copy, std::span<const std::byte>(world).first(16), {}, {}));
    CHECK_FALSE(ws::startState(copy, world, small, {}));
    CHECK_FALSE(ws::startState(copy, world, {}, small));
    CHECK(copy == filled(0));
}
