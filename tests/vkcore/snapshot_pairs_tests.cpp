#include "vkcore/snapshot_ring.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>

using namespace evr::vkcore::snapshot_ring;

namespace {

// Frame `frame`'s pair with eye 0's copy `seq0` and eye 1's `seq1` (0: not made), both submitted.
PairSlot pair(std::uint64_t frame, std::uint64_t seq0, std::uint64_t seq1, std::uint64_t readValue = 0) {
    PairSlot s;
    s.frame = frame;
    s.view = frame * 10;
    s.seq = {seq0, seq1};
    s.submitted = {seq0 != 0, seq1 != 0};
    s.readValue = readValue;
    return s;
}

} // namespace

TEST_CASE("a frame's second copy joins its first one's slot, whichever eye came first") {
    PairSlots slots{};
    slots[0] = pair(7, 30, 0); // eye 0 of frame 7
    slots[1] = pair(6, 29, 40);
    CHECK(pairSlotFor(slots, 2, 7, 1) == 0);
    slots[0] = pair(7, 0, 41); // eye 1 came first (view 0's command buffer submitted after view 1's)
    CHECK(pairSlotFor(slots, 2, 7, 0) == 0);
    CHECK(pairSlotFor(slots, 2, 7, 1) == kPairSlots); // the same eye twice: none
}

TEST_CASE("a new frame takes an empty slot first, then the oldest frame's, shown or not") {
    PairSlots slots{};
    CHECK(pairSlotFor(slots, 2, 1, 0) == 0);
    slots[0] = pair(1, 1, 1);
    CHECK(pairSlotFor(slots, 2, 2, 0) == 1);
    slots[1] = pair(2, 2, 2, 5);
    CHECK(pairSlotFor(slots, 2, 3, 0) == 0); // frame 1 was never shown: written over all the same
    slots[0] = pair(3, 3, 3, 6);
    CHECK(pairSlotFor(slots, 2, 4, 1) == 1);
    CHECK(pairSlotFor(slots, 3, 4, 1) == 2); // a third slot: still empty
}

TEST_CASE("never a slot a present is reading, one being submitted, or a newer frame's") {
    PairSlots slots{};
    slots[0] = pair(5, 10, 10, kPairReading);
    slots[1] = pair(6, 11, 11);
    CHECK(pairSlotFor(slots, 2, 7, 0) == 1);
    slots[1].submitted[1] = false; // its eye 1 copy's submit has not returned
    CHECK(pairSlotFor(slots, 2, 7, 0) == kPairSlots);
    slots[0] = pair(8, 12, 12);
    slots[1] = pair(9, 13, 0);
    CHECK(pairSlotFor(slots, 2, 7, 1) == kPairSlots); // a late copy: both slots hold newer frames
    CHECK(pairSlotFor(slots, 2, 0, 0) == kPairSlots); // a frame not known
}

TEST_CASE("a claimed slot's new copies wait for its last copies and its last read") {
    PairSlot s = pair(4, 20, 21, 70);
    claim(s, 6, 60);
    CHECK(s.frame == 6);
    CHECK(s.view == 60);
    CHECK(s.seq[0] == 0);
    CHECK(s.seq[1] == 0);
    CHECK_FALSE(s.submitted[0]);
    CHECK(s.readValue == 0);
    CHECK(s.prior[0] == 20);
    CHECK(s.prior[1] == 21);
    CHECK(s.priorRead == 70);
    // Eye 1 never came and nobody read it: the next claim still waits for the older copies and read.
    s.seq[0] = 25;
    s.submitted[0] = true;
    claim(s, 8, 80);
    CHECK(s.prior[0] == 25);
    CHECK(s.prior[1] == 21);
    CHECK(s.priorRead == 70);
}

TEST_CASE("a present shows the newest complete pair newer than the last one shown") {
    PairSlots slots{};
    slots[0] = pair(5, 5, 5);
    slots[1] = pair(6, 6, 0); // eye 1 not copied yet
    PairPick p = pickPair(slots, 2, 4, true, 0, 8);
    CHECK(p.index == 0);
    CHECK(p.show == PairShow::New);
    CHECK(p.skipped == 0);
    slots[1] = pair(6, 6, 6);
    slots[1].submitted[0] = false; // its submit has not returned: not shown
    CHECK(pickPair(slots, 2, 4, true, 0, 8).index == 0);
    slots[1].submitted[0] = true;
    p = pickPair(slots, 2, 4, true, 0, 8);
    CHECK(p.index == 1);
    CHECK(p.skipped == 1); // frame 5 is never shown
    slots[2] = pair(7, 7, 7);
    CHECK(pickPair(slots, 2, 4, true, 0, 8).index == 1); // only the first `count` slots
    CHECK(pickPair(slots, 3, 4, true, 0, 8).index == 2);
}

TEST_CASE("without a newer pair the headset keeps the last one, for a while") {
    PairSlots slots{};
    slots[0] = pair(5, 5, 5, 40); // shown by the last present
    slots[1] = pair(6, 6, 0);
    PairPick p = pickPair(slots, 2, 5, true, 0, 8);
    CHECK(p.index == kPairSlots);
    CHECK(p.show == PairShow::Kept);
    CHECK(pickPair(slots, 2, 5, true, 7, 8).show == PairShow::Kept);
    CHECK(pickPair(slots, 2, 5, true, 8, 8).show == PairShow::NoPair); // too long: the presented image
    PairSlots first{};
    first[0] = pair(6, 6, 0);
    CHECK(pickPair(first, 2, 0, true, 0, 8).show == PairShow::NoPair); // none shown yet: nothing to keep
    slots[1] = pair(6, 6, 6, kPairReading);
    CHECK(pickPair(slots, 2, 5, true, 0, 8).show == PairShow::Kept);
}

TEST_CASE("frames without view 1 show no pair") {
    PairSlots slots{};
    slots[0] = pair(5, 5, 5);
    CHECK(pickPair(slots, 2, 4, false, 0, 8).show == PairShow::NoPair);
    CHECK(pickPair(slots, 2, 4, false, 0, 8).index == kPairSlots);
}

TEST_CASE(
    "the frame before is being read: the next frame's eye 0 takes the slot of the half pair before it") {
    PairSlots slots{};
    slots[0] = pair(4, 8, 8, kPairReading);  // frame k-1, a present recording its read
    slots[1] = pair(5, 9, 0);                // frame k, eye 0 only (its eye 1 never came)
    CHECK(pairSlotFor(slots, 2, 6, 0) == 1); // k+1 writes over k's half pair, never the pair being read
    CHECK(pairSlotFor(slots, 2, 5, 1) == 1); // k's own eye 1, had it come: its half pair's slot
}

TEST_CASE("several newer complete pairs: the newest is shown and the others counted as skipped") {
    PairSlots slots{};
    slots[0] = pair(5, 5, 5, 30); // shown by the last present
    slots[1] = pair(6, 6, 6);
    slots[2] = pair(7, 7, 7);
    slots[3] = pair(8, 8, 8);
    const PairPick p = pickPair(slots, 4, 5, true, 0, 8);
    CHECK(p.index == 3);
    CHECK(p.show == PairShow::New);
    CHECK(p.skipped == 2);         // frames 6 and 7 are never shown
    slots[2].submitted[1] = false; // frame 7's eye 1 copy not submitted: not complete, not skipped
    CHECK(pickPair(slots, 4, 5, true, 0, 8).skipped == 1);
}

TEST_CASE("ETERNALVR_TEST_PE_EYE1_LAG=1: the pair shown last is kept for its eye 1") {
    PairSlots slots{};
    slots[0] = pair(5, 5, 5, 30); // shown last
    slots[1] = pair(6, 6, 6);     // the next one to show
    slots[2] = pair(4, 4, 4, 20); // shown before that
    CHECK(shownBefore(slots, 3, 5) == 0);
    CHECK(shownBefore(slots, 2, 6) == 1);
    CHECK(shownBefore(slots, 1, 6) == kPairSlots); // past the first `count`
    CHECK(pairSlotFor(slots, 3, 7, 0) == 2);       // frame 4's slot, never the kept frame 5's
    CHECK(pairSlotFor(slots, 3, 7, 0, 5) == 2);    // the same with the knob
    slots[2] = pair(6, 0, 0);
    slots[1] = pair(7, 7, 0);
    CHECK(pairSlotFor(slots, 3, 8, 0) == 0);             // without the knob the oldest: frame 5's
    CHECK(pairSlotFor(slots, 3, 8, 0, 5) == 2);          // with it the next oldest
    CHECK(pairSlotFor(slots, 2, 8, 0, 5) == 1);          // two slots: frame 7's half pair
    CHECK(pairSlotFor(slots, 3, 5, 1, 5) == kPairSlots); // frame 5's eye 1 again: none, as without it
    CHECK(shownBefore(slots, 3, 0) == kPairSlots);       // none shown yet
    CHECK(shownBefore(slots, 3, 6) == kPairSlots);       // frame 6 not complete
}

TEST_CASE("a slot count past the slots there are reads only those") {
    PairSlots slots{};
    for (std::size_t i = 0; i < kPairSlots; ++i) {
        slots[i] = pair(10 + i, 10 + i, 10 + i);
    }
    CHECK(pairSlotFor(slots, kPairSlots + 5, 20, 0) == 0); // the oldest of the kPairSlots
    CHECK(pairSlotFor(slots, 1000, 20, 1) < kPairSlots);
    const PairPick p = pickPair(slots, kPairSlots + 5, 9, true, 0, 8);
    CHECK(p.index == kPairSlots - 1);
    CHECK(p.skipped == kPairSlots - 1);
}
