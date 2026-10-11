#include "features/flares/flare_pending.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

using namespace evr::flares;

namespace {

using Words = std::array<std::uint32_t, kPendingWords>;

std::span<const std::uint32_t> range(const Words& words, std::int32_t first, std::int32_t count) {
    return {words.data() + first, static_cast<std::size_t>(count)};
}

} // namespace

TEST_CASE("the clear zeroes every pending word and reports the ones left set") {
    Words words{};
    // Positive control: bits left from an earlier frame (a twin in the upper half, an engine slot) are found.
    words[3] = 0x10;
    words[17] = 0x80000001u;
    const PendingCleared found = clearPending(words);
    CHECK(found.words == 2);
    CHECK(found.firstWord == 3);
    CHECK(found.firstValue == 0x10u);
    for (const std::uint32_t w : words) {
        CHECK(w == 0u);
    }
    // Nothing set: nothing reported.
    const PendingCleared none = clearPending(words);
    CHECK(none.words == 0);
    CHECK(none.firstWord == -1);
}

TEST_CASE("a copy may wait only on bits its own view marked since the reset") {
    auto marks = std::make_unique<ViewMarks>();
    marks->reset();
    marks->mark(1, 512 + 3); // view 1's twin of slot 3
    marks->mark(1, 512 + 4);
    marks->mark(0, 3); // view 0's own slot
    Words pending{};
    pending[16] = (1u << 3) | (1u << 4);
    CHECK(marks->covers(1, range(pending, 16, 16), 16));
    pending[0] = 1u << 3;
    CHECK(marks->covers(0, range(pending, 0, 16), 0));
    // Positive control: a stale bit nobody marked this frame (word 16, bit 5) stops view 1's wait.
    pending[16] |= 1u << 5;
    CHECK_FALSE(marks->covers(1, range(pending, 16, 16), 16));
    std::uint32_t bits = 0;
    CHECK(marks->firstUnmarked(1, range(pending, 16, 16), 16, bits) == 16);
    CHECK(bits == (1u << 5));
    // A bit the other view marked is not this view's.
    pending[16] = 1u << 3;
    CHECK(marks->covers(1, range(pending, 16, 16), 16));
    CHECK_FALSE(marks->covers(0, range(pending, 16, 16), 16));
}

TEST_CASE("the reset at view 0's latch drops the last frame's marks") {
    auto marks = std::make_unique<ViewMarks>();
    marks->reset();
    marks->mark(1, 600);
    Words pending{};
    pending[600 / 32] = 1u << (600 % 32);
    CHECK(marks->covers(1, range(pending, 16, 16), 16)); // positive control: marked this frame
    marks->reset();
    CHECK_FALSE(marks->covers(1, range(pending, 16, 16), 16)); // the same bit a frame later: no wait
    // No bit pending: nothing to wait for that was not marked.
    CHECK(marks->covers(1, range(Words{}, 16, 16), 16));
}

TEST_CASE("marks and scans outside the pool or of another view are never covered") {
    auto marks = std::make_unique<ViewMarks>();
    marks->reset();
    marks->mark(1, -1);
    marks->mark(1, kPoolQueries);
    marks->mark(2, 5);
    Words pending{};
    pending[0] = 1u << 5;
    CHECK_FALSE(marks->covers(0, range(pending, 0, 1), 0));
    CHECK_FALSE(marks->covers(2, range(pending, 0, 1), 0));
    CHECK_FALSE(marks->covers(1, range(Words{}, 0, 16), 20)); // past the pool's 32 words
    CHECK_FALSE(marks->covers(1, range(Words{}, 0, 1), -1));
    marks->mark(0, 5);
    CHECK(marks->covers(0, range(pending, 0, 1), 0));
}

TEST_CASE("a copy uses up its view's marks: a frame without view 0's latch cannot wait on them") {
    auto marks = std::make_unique<ViewMarks>();
    marks->reset();
    marks->mark(1, 512 + 3);
    marks->mark(0, 3);
    Words pending{};
    pending[16] = 1u << 3;
    pending[0] = 1u << 3;
    // Positive control: this frame's copy may wait.
    CHECK(marks->covers(1, range(pending, 16, 16), 16));
    marks->clear(1);
    // The next frame, no reset (view 0's latch did not run), the bit left set: no wait.
    CHECK_FALSE(marks->covers(1, range(pending, 16, 16), 16));
    // View 0's own stay until its copy.
    CHECK(marks->covers(0, range(pending, 0, 16), 0));
    // Marked again after the copy: the next copy may wait.
    marks->mark(1, 512 + 3);
    CHECK(marks->covers(1, range(pending, 16, 16), 16));
    // Another view than 0 or 1: nothing cleared.
    marks->clear(2);
    marks->clear(-1);
    CHECK(marks->covers(1, range(pending, 16, 16), 16));
    CHECK(marks->covers(0, range(pending, 0, 16), 0));
}

TEST_CASE("the queries a view's copies waited on are checked against its occlusion pass's issues") {
    auto waited = std::make_unique<ViewMarks>();
    auto issued = std::make_unique<ViewMarks>();
    waited->reset();
    issued->reset();
    Words scanned{};
    scanned[16] = (1u << 3) | (1u << 4);
    waited->note(1, range(scanned, 16, 16), 16);
    CHECK(waited->noted(1, 16) == ((1u << 3) | (1u << 4)));
    CHECK(waited->noted(0, 16) == 0u);
    // The pass began both (recorded after the copy): nothing reported.
    issued->mark(1, 512 + 3);
    issued->mark(1, 512 + 4);
    std::uint32_t bits = 0;
    CHECK(firstWaitedUnissued(*waited, *issued, 1, bits) < 0);
    CHECK(bits == 0u);
    // Positive control: a waited query the pass did not begin is found (word and bit).
    Words more{};
    more[17] = 1u << 9;
    waited->note(1, range(more, 17, 1), 17);
    CHECK(firstWaitedUnissued(*waited, *issued, 1, bits) == 17);
    CHECK(bits == (1u << 9));
    // Issued by the other view: still not this view's.
    issued->mark(0, 512 + 32 + 9);
    CHECK(firstWaitedUnissued(*waited, *issued, 1, bits) == 17);
    issued->mark(1, 512 + 32 + 9);
    CHECK(firstWaitedUnissued(*waited, *issued, 1, bits) < 0);
    // View 0 waited on nothing; a view outside 0, 1 has nothing.
    CHECK(firstWaitedUnissued(*waited, *issued, 0, bits) < 0);
    CHECK(firstWaitedUnissued(*waited, *issued, 2, bits) < 0);
    // Noting outside the pool or for another view: ignored.
    waited->note(1, range(more, 17, 1), 40);
    waited->note(3, range(more, 17, 1), 17);
    waited->note(1, range(more, 17, 1), -1);
    CHECK(firstWaitedUnissued(*waited, *issued, 1, bits) < 0);
    waited->reset();
    CHECK(waited->noted(1, 16) == 0u);
}

TEST_CASE("the rig's stale bit is one view 1's copy scans and its setup never marked") {
    auto marks = std::make_unique<ViewMarks>();
    marks->reset();
    Words pending{};
    // Only every kStaleTestPeriod-th frame split by view.
    CHECK_FALSE(setStaleTestBit(pending, 1));
    CHECK_FALSE(setStaleTestBit(pending, kStaleTestPeriod - 1));
    CHECK(pending[31] == 0u);
    CHECK(setStaleTestBit(pending, 0));
    CHECK(setStaleTestBit(pending, kStaleTestPeriod));
    CHECK(pending[31] == 0x80000000u);
    // View 1's twins of this frame, marked, beside it.
    marks->mark(1, 512 + 2);
    pending[16] = 1u << 2;
    std::uint32_t bits = 0;
    CHECK_FALSE(marks->covers(1, range(pending, 16, 16), 16));
    CHECK(marks->firstUnmarked(1, range(pending, 16, 16), 16, bits) == 31);
    CHECK(bits == 0x80000000u);
    // View 0's scan of the lower half does not see it.
    CHECK(marks->covers(0, range(pending, 0, 16), 0));
    // A pool too short for the bit: nothing set.
    std::array<std::uint32_t, 4> few{};
    CHECK_FALSE(setStaleTestBit(few, 0));
}

TEST_CASE("the rig's trip test notes one waited query no pass issued, at view 1's kTripTestCopy-th wait") {
    auto waited = std::make_unique<ViewMarks>();
    auto issued = std::make_unique<ViewMarks>();
    waited->reset();
    issued->reset();
    // View 1's real waited queries of the frame, issued by its pass.
    Words scanned{};
    scanned[16] = 1u << 2;
    waited->note(1, range(scanned, 16, 16), 16);
    issued->mark(1, 512 + 2);
    std::uint32_t bits = 0;
    // Before the test's copy, or another view's: nothing noted, the check finds nothing.
    CHECK_FALSE(noteTripTest(*waited, 1, kTripTestCopy - 1));
    CHECK_FALSE(noteTripTest(*waited, 1, kTripTestCopy + 1));
    CHECK_FALSE(noteTripTest(*waited, 0, kTripTestCopy));
    CHECK(firstWaitedUnissued(*waited, *issued, 1, bits) < 0);
    // Positive control: at the test's copy the check at view 0's next latch finds query 0x3FF.
    CHECK(noteTripTest(*waited, 1, kTripTestCopy));
    CHECK(firstWaitedUnissued(*waited, *issued, 1, bits) == 31);
    CHECK(bits == 0x80000000u);
    CHECK(firstWaitedUnissued(*waited, *issued, 0, bits) < 0);
}
