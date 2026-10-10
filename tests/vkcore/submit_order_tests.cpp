#include "vkcore/submit_order.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <utility>
#include <vector>

using namespace evr::vkcore::submit_order;

namespace {

Batch batch(std::vector<std::uint64_t> waits, std::vector<std::uint64_t> signals, bool carries = false) {
    Batch b;
    b.waits = std::move(waits);
    b.signals = std::move(signals);
    b.carries = carries;
    return b;
}

} // namespace

TEST_CASE("the signals move from the first batch carrying a copied command buffer on") {
    const std::vector<Batch> batches{batch({1}, {2}), batch({}, {3}, true), batch({}, {4})};
    const Move m = moveSignals(batches);
    CHECK(m.carried);
    CHECK(m.movable);
    CHECK(m.from == 1); // batch 0's signal covers nothing of view 0's: it stays
}

TEST_CASE("no batch carries a copied command buffer: nothing moves") {
    const Move m = moveSignals({batch({}, {2}), batch({}, {3})});
    CHECK_FALSE(m.carried);
    CHECK_FALSE(m.movable);
}

TEST_CASE("a later batch waiting on a signal that would move: not movable (it would wait for the copy)") {
    CHECK_FALSE(moveSignals({batch({}, {5}, true), batch({5}, {6})}).movable);
    // On a signal of a batch before the carrying one: that one stays, so it may.
    CHECK(moveSignals({batch({}, {5}), batch({}, {6}, true), batch({5}, {7})}).movable);
    // Its own signal waited on by itself is no dependency between batches.
    CHECK(moveSignals({batch({5}, {5}, true)}).movable);
}

TEST_CASE("another pNext chain from the carrying batch on: not movable; before it: no matter") {
    std::vector<Batch> batches{batch({}, {2}), batch({}, {3}, true), batch({}, {4})};
    batches[2].plainChain = false;
    CHECK_FALSE(moveSignals(batches).movable);
    batches[2].plainChain = true;
    batches[0].plainChain = false;
    CHECK(moveSignals(batches).movable);
}
