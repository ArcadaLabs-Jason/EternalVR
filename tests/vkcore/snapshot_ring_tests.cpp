#include "vkcore/snapshot_ring.hpp"

#include <doctest/doctest.h>

using namespace evr::vkcore::snapshot_ring;

namespace {

Slot written(std::uint64_t seq, std::uint64_t tag, bool submitted = true, std::uint64_t readValue = 0) {
    Slot s;
    s.seq = seq;
    s.tag = tag;
    s.submitted = submitted;
    s.readValue = readValue;
    return s;
}

Slot drew(std::uint64_t seq, std::uint64_t tag, std::uint64_t image) {
    Slot s = written(seq, tag);
    s.image = image;
    return s;
}

} // namespace

TEST_CASE("a snapshot goes into an empty slot first, then the oldest free one") {
    Slots slots{};
    CHECK(pickWrite(slots, 0, 0) == 0);
    slots[0] = written(1, 100);
    slots[1] = written(2, 200);
    slots[2] = written(3, 300);
    slots[3] = written(4, 400);
    CHECK(pickWrite(slots, 3, 0) == 0); // all done, none read: the oldest
}

TEST_CASE("a slot whose copy or whose last read is not done is not written") {
    Slots slots{};
    slots[0] = written(1, 100, true, 50); // read by presenter copy 50
    slots[1] = written(2, 200);
    slots[2] = written(3, 300);
    slots[3] = written(4, 400);
    CHECK(pickWrite(slots, 3, 49) == 1);       // copy 50 still reading slot 0
    CHECK(pickWrite(slots, 1, 50) == 0);       // read done
    CHECK(pickWrite(slots, 0, 100) == kSlots); // no snapshot signalled yet: every copy may still run
}

TEST_CASE("an unsubmitted snapshot (its command buffer never reached a queue) can be written again") {
    Slots slots{};
    slots[0] = written(1, 100, false);
    slots[1] = written(2, 200);
    slots[2] = written(3, 300);
    slots[3] = written(4, 400);
    CHECK(pickWrite(slots, 0, 0) == 0);
}

TEST_CASE("no free slot") {
    Slots slots{};
    slots[0] = written(1, 100);
    slots[1] = written(2, 200);
    slots[2] = written(3, 300);
    slots[3] = written(4, 400);
    CHECK(pickWrite(slots, 0, 0) == kSlots);
}

TEST_CASE("a present reads the oldest unread copy carrying the semaphore it waits on") {
    Slots slots{};
    slots[0] = written(4, 100); // read by the last present
    slots[1] = written(5, 200); // this present's semaphore
    slots[2] = written(6, 100); // the next frame, already submitted
    const Read r = pickRead(slots, 200, 4);
    CHECK(r.index == 1);
    CHECK(r.miss == Miss::None);
}

TEST_CASE("the game reuses a semaphore: the oldest unread copy carrying it") {
    Slots slots{};
    slots[0] = written(4, 200); // this present's frame
    slots[1] = written(5, 200); // a later frame, same semaphore
    slots[2] = written(3, 100);
    CHECK(pickRead(slots, 200, 3).index == 0);
}

TEST_CASE("a copy carrying another semaphore is never taken") {
    Slots slots{};
    slots[0] = written(4, 100);
    slots[1] = written(5, 100);
    const Read r = pickRead(slots, 200, 3);
    CHECK(r.index == kSlots);
    CHECK(r.miss == Miss::NoCopy);
}

TEST_CASE("only the last copy read carries the semaphore: the present repeats it") {
    Slots slots{};
    slots[0] = written(4, 100);
    slots[1] = written(5, 200);
    const Read r = pickRead(slots, 200, 5);
    CHECK(r.index == 1);
    CHECK(r.miss == Miss::Repeat);
}

TEST_CASE("an older copy than the last one read is never shown") {
    Slots slots{};
    slots[0] = written(4, 100);
    slots[1] = written(5, 200);
    const Read r = pickRead(slots, 100, 5);
    CHECK(r.index == kSlots);
    CHECK(r.miss == Miss::NoCopy);
}

TEST_CASE("a copy not submitted yet is not read") {
    Slots slots{};
    slots[0] = written(5, 100, false);
    CHECK(pickRead(slots, 100, 4).miss == Miss::NoCopy);
}

TEST_CASE("the present shows the frame before the matched one: its copy") {
    Slots slots{};
    slots[0] = written(4, 100); // read by the last present
    slots[1] = written(5, 300); // the frame the present shows
    slots[2] = written(6, 200); // the frame whose submit the present waits on
    const Read r = previousOf(slots, pickRead(slots, 200, 4), 4);
    CHECK(r.index == 1);
    CHECK(r.miss == Miss::None);
}

TEST_CASE("the frame before the matched one was read by the last present: a repeat of it") {
    Slots slots{};
    slots[0] = written(5, 300); // read by the last present
    slots[1] = written(6, 200); // matched
    const Read r = previousOf(slots, pickRead(slots, 200, 5), 5);
    CHECK(r.index == 0);
    CHECK(r.miss == Miss::Repeat);
}

TEST_CASE("without the frame before the matched one, the matched copy") {
    Slots slots{};
    slots[0] = written(6, 200); // matched; 5 was overwritten
    slots[1] = written(7, 100);
    Read r = previousOf(slots, pickRead(slots, 200, 3), 3);
    CHECK(r.index == 0);
    CHECK(r.miss == Miss::None);

    slots[2] = written(5, 300, false); // not submitted: not read
    r = previousOf(slots, pickRead(slots, 200, 3), 3);
    CHECK(r.index == 0);
}

TEST_CASE("the frame before the matched one is never older than the last copy read") {
    Slots slots{};
    slots[0] = written(4, 300);
    slots[1] = written(5, 100); // read by the last present
    slots[2] = written(6, 200);
    CHECK(previousOf(slots, Read{2, Miss::None}, 5).index == 1); // 5: the last one read, repeated
    const Read r = previousOf(slots, Read{0, Miss::None}, 5);    // 3 would be older than 5: the matched one
    CHECK(r.index == 0);
}

TEST_CASE("a repeat or a miss passes through") {
    Slots slots{};
    slots[0] = written(4, 100);
    slots[1] = written(5, 200);
    const Read repeat = previousOf(slots, Read{1, Miss::Repeat}, 5);
    CHECK(repeat.index == 1);
    CHECK(repeat.miss == Miss::Repeat);
    CHECK(previousOf(slots, Read{}, 5).miss == Miss::NoCopy);
}

TEST_CASE("who drew the presented image") {
    Slots slots{};
    slots[0] = drew(5, 300, 0xA); // the frame before
    slots[1] = drew(6, 200, 0xB); // matched
    const Read matched{1, Miss::None};
    CHECK(whoDrew(slots, matched, 0xA) == Drew::Before);
    CHECK(whoDrew(slots, matched, 0xB) == Drew::Matched);
    CHECK(whoDrew(slots, matched, 0xC) == Drew::NotKnown);
    CHECK(whoDrew(slots, matched, 0) == Drew::NotKnown);
    CHECK(whoDrew(slots, Read{}, 0xA) == Drew::NotKnown);
    slots[0].image = 0xB; // the swapchain handed the same image to both frames
    CHECK(whoDrew(slots, matched, 0xB) == Drew::Both);
}

TEST_CASE("a present of the image the frame before drew shows that frame's snapshot") {
    Slots slots{};
    slots[0] = drew(4, 100, 0xB); // read by the last present
    slots[1] = drew(5, 300, 0xA);
    slots[2] = drew(6, 200, 0xB); // matched
    const Read r = pickShown(slots, pickRead(slots, 200, 4), 0xA, 4);
    CHECK(r.index == 1);
    CHECK(r.miss == Miss::None);
}

TEST_CASE("after a hitch the game presents the matched frame's own image: its snapshot") {
    Slots slots{};
    slots[0] = drew(4, 100, 0xB);
    slots[1] = drew(5, 300, 0xA);
    slots[2] = drew(6, 200, 0xB); // matched; the present shows its image
    const Read r = pickShown(slots, pickRead(slots, 200, 4), 0xB, 4);
    CHECK(r.index == 2);
    CHECK(r.miss == Miss::None);
    CHECK(pickShown(slots, pickRead(slots, 200, 4), 0xB, 4, Shown::Before).index == 1); // A/B: the one before
    CHECK(pickShown(slots, pickRead(slots, 200, 4), 0xB, 4, Shown::BothMatched).index == 2);
}

TEST_CASE("without evidence the matched frame drew it, the snapshot before") {
    Slots slots{};
    slots[0] = drew(5, 300, 0xB);
    slots[1] = drew(6, 200, 0xB); // both drew image B
    CHECK(pickShown(slots, pickRead(slots, 200, 4), 0xB, 4).index == 0);
    CHECK(pickShown(slots, pickRead(slots, 200, 4), 0xB, 4, Shown::BothMatched).index == 1); // A/B: matched
    CHECK(pickShown(slots, pickRead(slots, 200, 4), 0xC, 4, Shown::BothMatched).index == 0);
    CHECK(pickShown(slots, pickRead(slots, 200, 4), 0xC, 4).index == 0); // neither
    CHECK(pickShown(slots, pickRead(slots, 200, 4), 0, 4).index == 0);   // image not known
}

TEST_CASE("the matched frame's image after its snapshot was read: a repeat of it") {
    Slots slots{};
    slots[0] = drew(5, 300, 0xA);
    slots[1] = drew(6, 200, 0xB); // matched and read by the last present
    const Read r = pickShown(slots, pickRead(slots, 200, 6), 0xB, 6);
    CHECK(r.index == 1);
    CHECK(r.miss == Miss::Repeat);
}

TEST_CASE("the matched frame drew it but the frame before's image is not known: not known") {
    Slots slots{};
    slots[0] = drew(5, 300, 0); // its swapchain image was not seen
    slots[1] = drew(6, 200, 0xB);
    CHECK(whoDrew(slots, Read{1, Miss::None}, 0xB) == Drew::NotKnown);
    CHECK(pickShown(slots, Read{1, Miss::None}, 0xB, 4).index == 0);
    slots[0] = Slot{}; // overwritten
    CHECK(whoDrew(slots, Read{1, Miss::None}, 0xB) == Drew::NotKnown);
}

TEST_CASE("a present with no copy of its own repeats the last copy") {
    Slots slots{};
    slots[0] = drew(5, 300, 0xA);
    slots[1] = drew(6, 200, 0xB);              // read by the last present (its matched copy)
    const Read none = pickRead(slots, 300, 6); // waits on copy 5's semaphore: older than the last read
    CHECK(none.miss == Miss::NoCopy);
    const Read r = orLast(slots, none, 6);
    CHECK(r.index == 1);
    CHECK(r.miss == Miss::Repeat);
    CHECK(orLast(slots, pickRead(slots, 999, 6), 6).index == 1); // a semaphore no copy carries
}

TEST_CASE("no last copy (or it was overwritten): still no copy") {
    Slots slots{};
    slots[0] = drew(5, 300, 0xA);
    CHECK(orLast(slots, Read{}, 0).miss == Miss::NoCopy);
    CHECK(orLast(slots, Read{}, 4).miss == Miss::NoCopy);
    const Read own = orLast(slots, Read{0, Miss::None}, 4); // a present with its own copy is unchanged
    CHECK(own.index == 0);
    CHECK(own.miss == Miss::None);
}

TEST_CASE("after a recreate, presents of images no view 0 pass drew keep the last pair") {
    NewImageHold hold;
    CHECK_FALSE(hold.holds(0xB0)); // not armed: nothing held
    hold.arm(4);
    CHECK(hold.holds(0xB0)); // the recreate's frame presents a new image nothing drew
    hold.drew(0xB0);         // the next frame's view 0 draws it
    CHECK(hold.holds(0xB1)); // and presents the other new image, still not drawn
    hold.drew(0xB1);
    CHECK_FALSE(hold.holds(0xB0)); // a drawn image: the hold ends
    CHECK_FALSE(hold.active());
    CHECK_FALSE(hold.holds(0xB2));
}

TEST_CASE("the new-image hold ends after its count, and a new arm forgets the old drawn images") {
    NewImageHold hold;
    hold.arm(2);
    hold.drew(0);         // not known: not counted as drawn
    CHECK(hold.holds(0)); // an unknown presented image is held while the count lasts
    CHECK(hold.holds(0xB0));
    CHECK_FALSE(hold.holds(0xB0)); // the count is over: never stalls the presents
    hold.arm(3);
    hold.drew(0xB0);
    hold.arm(3); // a second recreate right after
    CHECK(hold.holds(0xB0));
}
