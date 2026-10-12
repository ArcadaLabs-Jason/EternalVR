#include "vkcore/snapshot_ring.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

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

// A session of the pairs as view_snapshot_pairs.cpp runs them, with the GPU done with everything by the next
// frame's submit: each frame's submit carries view 0's command buffer, then view 1's (measured: the same
// submit), so eye 0's copy is recorded, not yet submitted, when eye 1's ring is checked; a present after each
// submit. Each ring is made for a size (`want`, per eye); `step` decides what each eye's copy does.
struct Session {
    PairSlots slots{};
    std::array<int, 2> ring{1, 1}; // the size each ring is made for
    std::array<int, 2> want{1, 1}; // the size each eye's copy needs
    std::array<std::uint64_t, 2> seq{};
    std::array<std::uint64_t, 2> done{};
    std::uint64_t reads = 0;
    std::uint64_t readDone = 0;
    std::uint64_t frame = 0;
    std::uint64_t lastShown = 0;
    std::uint32_t kept = 0;
    int newPairs = 0; // presents with a new pair since the last count()

    template <typename Step>
    void run(int frames, Step&& step) {
        for (int f = 0; f < frames; ++f) {
            done = seq; // the GPU caught up with the last frame's copies and reads
            readDone = reads;
            ++frame;
            std::array<bool, 2> copied{};
            for (int eye = 0; eye < 2; ++eye) {
                const auto e = static_cast<std::size_t>(eye);
                const RingStep r = step(eye, ring[e] != want[e], pairsIdle(slots, done, readDone));
                if (r == RingStep::Wait) {
                    continue;
                }
                if (r == RingStep::Remake) {
                    ring[e] = want[e];
                    slots = {};
                }
                const std::size_t i = pairSlotFor(slots, 2, frame, eye);
                if (i == kPairSlots) {
                    continue;
                }
                if (slots[i].frame != frame) {
                    claim(slots[i], frame, frame);
                }
                slots[i].seq[e] = ++seq[e];
                copied[e] = true;
            }
            for (PairSlot& s : slots) { // the submit carrying the copies returned
                for (std::size_t e = 0; e < 2; ++e) {
                    s.submitted[e] = s.submitted[e] || (copied[e] && s.frame == frame && s.seq[e] != 0);
                }
            }
            // The pairs show a pair only while both rings are of one size (the ring's number standing for its
            // size).
            const auto size = [](int n) {
                return Extent{static_cast<std::uint32_t>(n), static_cast<std::uint32_t>(n)};
            };
            const PairPick p = pickPair(slots, 2, lastShown, true, kept, 8,
                                        pairSizesMatch(1, size(ring[0]), 1, size(ring[1])));
            if (p.show == PairShow::New) {
                slots[p.index].readValue = ++reads;
                lastShown = slots[p.index].frame;
                kept = 0;
                ++newPairs;
            } else {
                kept += p.show == PairShow::Kept ? 1 : 0;
            }
        }
    }

    int count() { return std::exchange(newPairs, 0); }
};

// The decision before RingResize: a ring that differs is remade once the rings are idle, the other eye's
// copies go on regardless.
RingStep oldStep(int, bool differs, bool idle) {
    return !differs ? RingStep::Copy : idle ? RingStep::Remake : RingStep::Wait;
}

// The first RingResize (f61c2e5b): while a ring waits the other eye does not copy, but a ring that differs is
// remade as soon as the rings are idle, ahead of one that waited before it.
struct NoTurns {
    std::array<bool, 2> waiting{};

    RingStep operator()(int eye, bool differs, bool idle) {
        const auto e = static_cast<std::size_t>(eye);
        if (!differs) {
            waiting[e] = false;
            return waiting[1 - e] ? RingStep::Wait : RingStep::Copy;
        }
        waiting[e] = !idle;
        return idle ? RingStep::Remake : RingStep::Wait;
    }
};

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

TEST_CASE("the rings are idle only once every copy and read of both rings is done") {
    PairSlots slots{};
    CHECK(pairsIdle(slots, {0, 0}, 0));
    slots[0] = pair(5, 5, 7, 3);
    CHECK(pairsIdle(slots, {5, 7}, 3));
    CHECK_FALSE(pairsIdle(slots, {4, 7}, 3)); // eye 0's copy not done on the GPU
    CHECK_FALSE(pairsIdle(slots, {5, 6}, 3)); // eye 1's
    CHECK_FALSE(pairsIdle(slots, {5, 7}, 2)); // its read not done
    slots[0].submitted[0] = false;            // eye 0's copy recorded, its submit not returned
    CHECK_FALSE(pairsIdle(slots, {5, 7}, 3));
    slots[0] = pair(5, 5, 7, kPairReading); // a present recording its read
    CHECK_FALSE(pairsIdle(slots, {5, 7}, 3));
    slots[0] = pair(6, 0, 0);
    slots[0].prior = {9, 0}; // the copies it held before its claim
    CHECK_FALSE(pairsIdle(slots, {8, 0}, 0));
    CHECK(pairsIdle(slots, {9, 0}, 0));
    slots[0].priorRead = 4;
    CHECK_FALSE(pairsIdle(slots, {9, 0}, 3));
}

TEST_CASE("while one ring waits to be remade the other eye does not copy") {
    RingResize r;
    CHECK(r.step(0, false, false) == RingStep::Copy);
    CHECK(r.step(1, true, false) == RingStep::Wait); // eye 1's ring has another size, the rings are busy
    CHECK(r.waiting(1));
    CHECK(r.step(0, false, false) == RingStep::Wait); // eye 0 stops copying...
    CHECK(r.step(0, false, true) == RingStep::Wait);
    CHECK(r.step(1, true, true) == RingStep::Remake); // ... until eye 1's ring is remade
    CHECK_FALSE(r.waiting(1));
    CHECK(r.step(0, false, false) == RingStep::Copy);
    CHECK(r.step(1, false, false) == RingStep::Copy);
    CHECK(r.step(0, true, true) == RingStep::Remake); // a ring with nothing to wait for: at once
    CHECK(r.step(1, true, false) == RingStep::Wait);
    CHECK(r.step(1, false, false) == RingStep::Copy); // its need went away (the size came back)
    CHECK(r.step(0, false, false) == RingStep::Copy);
    CHECK(r.step(0, true, false) == RingStep::Wait);
    r.reset();
    CHECK_FALSE(r.waiting(0));
    CHECK(r.step(1, false, false) == RingStep::Copy);
}

TEST_CASE("after a swapchain remade at another size both rings follow and the pairs come back") {
    RingResize resize;
    auto step = [&resize](int eye, bool differs, bool idle) {
        return resize.step(eye, differs, idle);
    };
    Session s;
    s.run(10, step);
    CHECK(s.count() == 10);
    s.want = {2, 2}; // the swapchain and view 1's image at another size
    s.run(4, step);
    CHECK(s.ring[0] == 2);
    CHECK(s.ring[1] == 2);
    s.count();
    s.run(50, step);
    CHECK(s.count() == 50); // a new pair every present
    s.want[0] = 3;          // eye 0's size first, view 1's image a few frames later
    s.run(5, step);
    CHECK(s.ring[0] == 3);
    CHECK(s.ring[1] == 2);
    CHECK(s.count() == 0); // copies of two sizes: never shown as a pair
    s.want[1] = 3;
    s.run(4, step);
    CHECK(s.ring[0] == 3);
    CHECK(s.ring[1] == 3);
    s.count();
    s.run(20, step);
    CHECK(s.count() == 20);
}

TEST_CASE("positive control: with the old decision eye 1's ring stays at the old size, no pair again") {
    Session s;
    s.run(10, oldStep);
    CHECK(s.count() == 10);
    s.want = {2, 2};
    s.run(200, oldStep);
    CHECK(s.ring[0] == 2);
    CHECK(s.ring[1] == 1); // as measured: eye 1's copies refused 'no slot free' for the rest of the session
    CHECK(s.count() == 0);
}

TEST_CASE("a pair is shown only when both eyes' copies are of one size") {
    CHECK(pairSizesMatch(2, {2048, 2208}, 2, {2048, 2208}));
    CHECK_FALSE(pairSizesMatch(2, {2048, 2208}, 2, {2496, 2688}));
    CHECK_FALSE(pairSizesMatch(2, {2048, 2208}, 2, {2048, 2688}));
    CHECK(pairSizesMatch(0, {}, 2, {2496, 2688})); // a ring not made yet: no copies to pair
    CHECK(pairSizesMatch(2, {2048, 2208}, 0, {}));
    PairSlots slots{};
    slots[0] = pair(5, 5, 5, 30); // shown last
    slots[1] = pair(6, 6, 6);
    CHECK(pickPair(slots, 2, 5, true, 0, 8, true).show == PairShow::New);
    const PairPick p = pickPair(slots, 2, 5, true, 0, 8, false);
    CHECK(p.show == PairShow::Kept); // two sizes: the headset keeps the last pair, for a while
    CHECK(p.index == kPairSlots);
    CHECK(p.skipped == 0);
    CHECK(pickPair(slots, 2, 5, true, 8, 8, false).show == PairShow::NoPair);
}

TEST_CASE("the first ring to wait is remade first; two waiting rings never wait for each other") {
    RingResize r;
    CHECK(r.step(1, true, false) == RingStep::Wait); // eye 1 waits first
    CHECK(r.step(0, true, true) == RingStep::Wait);  // eye 0 differs too: behind eye 1, idle or not
    CHECK(r.step(1, true, true) == RingStep::Remake);
    CHECK(r.step(0, true, false) == RingStep::Wait); // eye 1's copy is in flight
    CHECK(r.step(1, false, false) == RingStep::Wait);
    CHECK(r.step(0, true, true) == RingStep::Remake);
    CHECK(r.step(1, false, false) == RingStep::Copy);
    // Both waiting: whichever is asked first once idle, the one that waited first is remade.
    CHECK(r.step(0, true, false) == RingStep::Wait);
    CHECK(r.step(1, true, false) == RingStep::Wait);
    CHECK(r.step(1, true, true) == RingStep::Wait);
    CHECK(r.step(0, true, true) == RingStep::Remake);
    CHECK(r.step(1, true, true) == RingStep::Remake);
    CHECK_FALSE(r.waiting(0));
    CHECK_FALSE(r.waiting(1));
}

TEST_CASE("a ring remade on every frame (a window dragged) does not keep the other one waiting") {
    RingResize resize;
    auto step = [&resize](int eye, bool differs, bool idle) {
        return resize.step(eye, differs, idle);
    };
    Session s;
    s.run(5, step);
    s.want = {2, 2};
    for (int f = 0; f < 30; ++f) { // eye 0's size changes on every frame, view 1's image stays at 2
        s.want[0] = 3 + f;
        s.run(1, step);
    }
    CHECK(s.ring[1] == 2); // remade in its turn
    s.want[1] = s.want[0]; // the drag ends: view 1's image follows
    s.run(6, step);
    CHECK(s.ring[0] == s.want[0]);
    CHECK(s.ring[1] == s.want[1]);
    s.count();
    s.run(10, step);
    CHECK(s.count() == 10);
    // Control: without the turns eye 0 is remade ahead of eye 1 on every frame, eye 1 stays at the old size.
    Session t;
    NoTurns noTurns;
    t.run(5, noTurns);
    t.want = {2, 2};
    for (int f = 0; f < 30; ++f) {
        t.want[0] = 3 + f;
        t.run(1, noTurns);
    }
    CHECK(t.ring[1] == 1);
}

TEST_CASE("whatever the size changes, both rings follow and the pairs come back once they stop") {
    std::uint32_t seed = 12345;
    auto next = [&seed] {
        return (seed = seed * 1664525u + 1013904223u) >> 16;
    };
    for (int trial = 0; trial < 300; ++trial) {
        RingResize resize;
        auto step = [&resize](int eye, bool differs, bool idle) {
            return resize.step(eye, differs, idle);
        };
        Session s;
        s.run(3, step);
        const int frames = 1 + static_cast<int>(next() % 40);
        for (int f = 0; f < frames; ++f) {
            const std::uint32_t r = next() % 8;
            if (r < 3) {
                s.want[r % 2] = 2 + static_cast<int>(next() % 4);
            } else if (r == 3) {
                s.want = {s.want[0] + 1, s.want[0] + 1};
            }
            s.run(1 + static_cast<int>(next() % 2), step);
        }
        s.want[1] = s.want[0]; // the sizes settle on one
        s.run(8, step);
        CHECK(s.ring[0] == s.want[0]);
        CHECK(s.ring[1] == s.want[1]);
        s.count();
        s.run(5, step);
        CHECK(s.count() == 5);
    }
}
