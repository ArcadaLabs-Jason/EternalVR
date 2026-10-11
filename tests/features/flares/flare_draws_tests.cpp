#include "features/flares/flare_draws.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>

using namespace evr::flares;

TEST_CASE("half 1 of a flare's indices is 24 bytes per quad further on") {
    CHECK(halfIndexShift(1) == 24);
    CHECK(halfIndexShift(3) == 72);
}

TEST_CASE("view 1's queries are the twins of the engine's slots in the pool's upper half") {
    CHECK(viewQuery(0, 0) == 0);
    CHECK(viewQuery(0, 1) == 512);
    CHECK(viewQuery(511, 1) == 1023);
    CHECK(viewQuery(37, 0) == 37);
    // A slot up there already (more than 256 flares) is shared, as the engine has it.
    CHECK(viewQuery(512, 1) == 512);
    CHECK(viewQuery(1023, 1) == 1023);
    CHECK(viewQuery(-1, 1) == -1);
}

TEST_CASE("the queries are split while the engine took no more than the lower half") {
    CHECK(queriesSplit(0));
    CHECK(queriesSplit(512));
    CHECK_FALSE(queriesSplit(513));
    CHECK_FALSE(queriesSplit(-1));
}

TEST_CASE("view 0 keeps the engine's scan, view 1 scans the upper half; both wait while split") {
    const CopyScan v0 = copyScan(0, 3, 70);
    CHECK(v0.firstWord == 0);
    CHECK(v0.words == 3);
    CHECK(v0.firstSlot == 0);
    CHECK(v0.wait);
    const CopyScan v1 = copyScan(1, 3, 70);
    CHECK(v1.firstWord == 16);
    CHECK(v1.words == 16);
    CHECK(v1.firstSlot == 512);
    CHECK(v1.wait);
    // More than 256 flares: the engine's slots reach the upper half, nobody waits.
    const CopyScan many0 = copyScan(0, 20, 620);
    CHECK(many0.words == 20);
    CHECK_FALSE(many0.wait);
    CHECK_FALSE(copyScan(1, 20, 620).wait);
}

TEST_CASE("a flare record of this frame is shifted only at the offset it was registered with") {
    auto shifts = std::make_unique<DrawShifts>();
    CHECK(shifts->empty());
    CHECK_FALSE(shifts->find(0x1000, 0x60).has_value());
    shifts->reset();
    REQUIRE(shifts->put(0x1000, 0x60, 0));
    CHECK_FALSE(shifts->empty());
    REQUIRE(shifts->find(0x1000, 0x60).has_value());
    CHECK(*shifts->find(0x1000, 0x60) == 0); // registered for view 0, view 1's half not built yet
    REQUIRE(shifts->put(0x1000, 0x60, 48));  // view 1's half built: the same record gets its shift
    CHECK(*shifts->find(0x1000, 0x60) == 48);
    CHECK_FALSE(shifts->find(0x1000, 0x78).has_value()); // another offset: someone else's range
    CHECK_FALSE(shifts->find(0x1188, 0x60).has_value()); // another record
    CHECK_FALSE(shifts->put(0, 0x60, 24));
}

TEST_CASE("every reset (view 0's latch) drops the last frame's records, even of the same world frame") {
    auto shifts = std::make_unique<DrawShifts>();
    shifts->reset();
    shifts->put(0x1000, 0x60, 24);
    shifts->put(0x2000, 0x90, 24);
    CHECK(shifts->find(0x2000, 0x90).has_value()); // positive control: found before the reset
    shifts->reset();
    CHECK(shifts->empty());
    CHECK_FALSE(shifts->find(0x1000, 0x60).has_value());
    CHECK_FALSE(shifts->find(0x2000, 0x90).has_value());
    REQUIRE(shifts->put(0x2000, 0xA8, 72));
    CHECK(*shifts->find(0x2000, 0xA8) == 72);
}

TEST_CASE("records that share a home slot are all found") {
    auto shifts = std::make_unique<DrawShifts>();
    shifts->reset();
    // Records 0x188 apart, as one flare's elements are, and many flares: every one is kept and found.
    for (std::uintptr_t i = 0; i < 1000; ++i) {
        REQUIRE(shifts->put(0x10000000 + i * 0x188, static_cast<std::int32_t>(i * 24), 24));
    }
    for (std::uintptr_t i = 0; i < 1000; ++i) {
        const auto found = shifts->find(0x10000000 + i * 0x188, static_cast<std::int32_t>(i * 24));
        REQUIRE(found.has_value());
        CHECK(*found == 24);
    }
}

TEST_CASE("a full table takes no more records, and the ones in it stay") {
    auto shifts = std::make_unique<DrawShifts>();
    shifts->reset();
    std::size_t added = 0;
    for (std::uintptr_t i = 0; i < DrawShifts::kSlots; ++i) {
        added += shifts->put(0x20000000 + i * 0x40, 0, 24) ? 1 : 0;
    }
    CHECK(added == DrawShifts::kSlots * 3 / 4);
    CHECK(shifts->find(0x20000000, 0).has_value());
    CHECK(shifts->put(0x20000000, 0, 48)); // an update needs no new slot
    CHECK(*shifts->find(0x20000000, 0) == 48);
}

TEST_CASE("view 1's flares off once its halves are rebuilt, set up to be drawn, and no bind of them shows") {
    CHECK_FALSE(drawsUnseen({0, 0, 0}));
    CHECK_FALSE(drawsUnseen({kUnseenRebuilds - 1, 0, 50}));
    CHECK(drawsUnseen({kUnseenRebuilds, 0, 1})); // positive control: set up, never bound at the hooks
    CHECK_FALSE(drawsUnseen({kUnseenRebuilds * 4, 1, 50}));
    // Built every frame but set up by neither view (out of sight, rig run pf3-f-wdef): not drawn, no trip.
    CHECK_FALSE(drawsUnseen({900, 0, 0}));
}
