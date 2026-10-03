#include "stereo_seq/prev_matrices.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

using evr::stereo_seq::ByteRange;
using evr::stereo_seq::Eye;
using evr::stereo_seq::previousMatrixRanges;
using evr::stereo_seq::PrevMatrixBook;

namespace {

// A tiny render view: "current" matrices at [0, 4), "previous" at [8, 12). The engine's store copies
// current into previous at the start of each frame; the latch then writes the frame's own current.
struct FakeView {
    std::vector<std::byte> bytes = std::vector<std::byte>(16);

    void latch(std::uint8_t value) {
        for (std::size_t i = 0; i < 4; ++i) {
            bytes[i] = std::byte{value};
        }
    }
    void store() {
        for (std::size_t i = 0; i < 4; ++i) {
            bytes[8 + i] = bytes[i];
        }
    }
    std::uint8_t previous() const { return std::to_integer<std::uint8_t>(bytes[8]); }
};

std::vector<ByteRange> fakeRanges() {
    return {{8, 4}};
}

// The render frame counter (renderSystem + 0x10): one per render frame, whatever it draws.
struct Frames {
    std::uint32_t next = 0xFFFFFFF0u; // wraps during the tests
};

// One render frame of view `v`: the store (and the book after it), then the latch of this frame's eye.
// Returns the previous value the frame renders with. `sameFrame` stores another view in the render frame
// of the last call.
std::uint8_t frame(FakeView& v,
                   PrevMatrixBook& book,
                   Frames& frames,
                   Eye chainEye,
                   std::uint8_t latched,
                   bool sameFrame = false) {
    const std::uint32_t renderFrame = sameFrame ? frames.next - 1 : frames.next++;
    v.store();
    book.afterStore(v.bytes.data(), chainEye, renderFrame);
    const std::uint8_t previous = v.previous();
    v.latch(latched);
    return previous;
}

// Latched values: eye L of tick t is 10 * t + 1, eye R is 10 * t + 2, a mono frame 10 * t.
std::uint8_t left(int t) {
    return static_cast<std::uint8_t>(10 * t + 1);
}
std::uint8_t right(int t) {
    return static_cast<std::uint8_t>(10 * t + 2);
}

} // namespace

TEST_CASE("previous matrices: the engine's ranges are ascending, disjoint and outside the counter") {
    const auto ranges = previousMatrixRanges();
    REQUIRE(!ranges.empty());
    for (std::size_t i = 1; i < ranges.size(); ++i) {
        CHECK(ranges[i - 1].offset + ranges[i - 1].size <= ranges[i].offset);
    }
    for (const ByteRange& r : ranges) {
        CHECK((r.offset + r.size <= 0x29944 || r.offset > 0x29944));
        CHECK(r.offset >= 0x29480);
        CHECK(r.offset + r.size <= 0x29950); // sizeof(idRenderView)
    }
    // The source fields of the store stay untouched (the latch rewrites them anyway).
    for (const std::size_t source : {std::size_t{0x29440}, std::size_t{0x295B0}, std::size_t{0x29630},
                                     std::size_t{0x296B0}, std::size_t{0x29770}, std::size_t{0x297F0}}) {
        for (const ByteRange& r : ranges) {
            CHECK((source + 0x40 <= r.offset || source >= r.offset + r.size));
        }
    }
}

TEST_CASE("previous matrices: in steady stereo each eye renders with its own previous matrices") {
    FakeView v;
    PrevMatrixBook book(fakeRanges());
    Frames frames;
    // Mono frames first, then stereo ticks.
    frame(v, book, frames, Eye::Left, 5);
    frame(v, book, frames, Eye::Left, 6);
    CHECK(frame(v, book, frames, Eye::Left, left(1)) == 6); // first stereo tick's eye L: the last mono frame
    CHECK(frame(v, book, frames, Eye::Right, right(1)) == left(1)); // nothing kept for eye R yet
    for (int t = 2; t < 10; ++t) {
        CHECK(frame(v, book, frames, Eye::Left, left(t)) == left(t - 1));
        CHECK(frame(v, book, frames, Eye::Right, right(t)) == right(t - 1));
    }
    CHECK(book.stats().rewrites == 16);
}

TEST_CASE("previous matrices: a mono frame between stereo ticks resets what is kept") {
    FakeView v;
    PrevMatrixBook book(fakeRanges());
    Frames frames;
    frame(v, book, frames, Eye::Left, left(1));
    frame(v, book, frames, Eye::Right, right(1));
    frame(v, book, frames, Eye::Left, left(2));
    frame(v, book, frames, Eye::Right, right(2));
    // Tick 3 is mono (loading screen); its frame starts after eye R and gets eye L's matrices.
    CHECK(frame(v, book, frames, Eye::Left, 30) == left(2));
    // Tick 4 is stereo again: eye L follows the mono frame, eye R has nothing current kept.
    CHECK(frame(v, book, frames, Eye::Left, left(4)) == 30);
    CHECK(frame(v, book, frames, Eye::Right, right(4)) == left(4));
    CHECK(frame(v, book, frames, Eye::Left, left(5)) == left(4));
    CHECK(frame(v, book, frames, Eye::Right, right(5)) == right(4));
}

TEST_CASE("previous matrices: render views are kept apart") {
    FakeView a;
    FakeView b;
    PrevMatrixBook book(fakeRanges());
    Frames frames;
    for (int t = 1; t < 5; ++t) {
        frame(a, book, frames, Eye::Left, left(t));
        frame(b, book, frames, Eye::Left, static_cast<std::uint8_t>(left(t) + 100), true);
        frame(a, book, frames, Eye::Right, right(t));
        frame(b, book, frames, Eye::Right, static_cast<std::uint8_t>(right(t) + 100), true);
    }
    CHECK(frame(a, book, frames, Eye::Left, left(5)) == left(4));
    CHECK(frame(b, book, frames, Eye::Left, static_cast<std::uint8_t>(left(5) + 100), true) == left(4) + 100);
    CHECK(frame(a, book, frames, Eye::Right, right(5)) == right(4));
    CHECK(frame(b, book, frames, Eye::Right, static_cast<std::uint8_t>(right(5) + 100), true) ==
          right(4) + 100);
}

TEST_CASE("previous matrices: the least recently used view is forgotten beyond the limit") {
    FakeView a;
    FakeView b;
    PrevMatrixBook book(fakeRanges(), 1);
    Frames frames;
    frame(a, book, frames, Eye::Left, left(1));
    frame(a, book, frames, Eye::Right, right(1));
    frame(b, book, frames, Eye::Left, 77, true); // evicts a
    // a starts over: its eye L keeps the engine's store (eye R's latch), no stale bytes are written.
    CHECK(frame(a, book, frames, Eye::Left, left(2)) == right(1));
}

TEST_CASE("previous matrices: kept bytes are not used once another render frame came between") {
    FakeView v;
    PrevMatrixBook book(fakeRanges());
    Frames frames;
    for (int t = 1; t < 4; ++t) {
        frame(v, book, frames, Eye::Left, left(t));
        frame(v, book, frames, Eye::Right, right(t));
    }
    // A render frame of another view only (a loading screen) between two stereo ticks of this one.
    ++frames.next;
    CHECK(frame(v, book, frames, Eye::Left, left(4)) == right(3));  // the engine's store, nothing restored
    CHECK(frame(v, book, frames, Eye::Right, right(4)) == left(4)); // nothing current kept for eye R
    CHECK(frame(v, book, frames, Eye::Left, left(5)) == left(4));
    CHECK(frame(v, book, frames, Eye::Right, right(5)) == right(4));
}

TEST_CASE("previous matrices: a new view at an old view's address after a map change gets nothing old") {
    FakeView v;
    PrevMatrixBook book(fakeRanges());
    Frames frames;
    frame(v, book, frames, Eye::Left, left(1));
    frame(v, book, frames, Eye::Right, right(1));
    frame(v, book, frames, Eye::Left, left(2));
    frame(v, book, frames, Eye::Right, right(2)); // keeps eye L's tick 2 matrices for tick 3
    frames.next += 500;                           // loading screens of the next map
    v.latch(99);                                  // the new view's own first state
    CHECK(frame(v, book, frames, Eye::Left, left(3)) == 99);
    CHECK(frame(v, book, frames, Eye::Right, right(3)) == left(3));
}

TEST_CASE("previous matrices: an eye R frame that does not follow its eye L keeps the engine's store") {
    FakeView v;
    PrevMatrixBook book(fakeRanges());
    Frames frames;
    frame(v, book, frames, Eye::Left, left(1));
    frame(v, book, frames, Eye::Right, right(1));
    frame(v, book, frames, Eye::Left, left(2));
    ++frames.next; // something rendered between eye L and eye R
    CHECK(frame(v, book, frames, Eye::Right, right(2)) == left(2));
    // Nothing from that eye R frame counts as eye L's for the next tick.
    CHECK(frame(v, book, frames, Eye::Left, left(3)) == right(2));
}

TEST_CASE("previous matrices: an eye R frame that stays mono gets the engine's store back") {
    FakeView v;
    PrevMatrixBook book(fakeRanges());
    Frames frames;
    for (int t = 1; t < 3; ++t) {
        frame(v, book, frames, Eye::Left, left(t));
        frame(v, book, frames, Eye::Right, right(t));
    }
    CHECK(frame(v, book, frames, Eye::Left, left(3)) == left(2));
    CHECK_FALSE(book.undoRewrite(v.bytes.data())); // an eye L rewrite stays: a mono frame reads eye L's
    // Eye R's frame of tick 3: the book writes eye R's previous matrices, then the per-eye hook writes no
    // view (the tags take a new base) and the frame renders the game's view.
    v.store();
    book.afterStore(v.bytes.data(), Eye::Right, frames.next++);
    CHECK(v.previous() == right(2));
    CHECK(book.undoRewrite(v.bytes.data()));
    CHECK(v.previous() == left(3)); // what the engine stored
    CHECK_FALSE(book.undoRewrite(v.bytes.data()));
    v.latch(30);
    // What follows treats it as a mono frame.
    CHECK(frame(v, book, frames, Eye::Left, left(4)) == 30);
    CHECK(frame(v, book, frames, Eye::Right, right(4)) == left(4));
    CHECK(frame(v, book, frames, Eye::Left, left(5)) == left(4));
    CHECK(frame(v, book, frames, Eye::Right, right(5)) == right(4));
    CHECK(book.stats().undone == 1);
}

TEST_CASE("previous matrices: nothing to undo for an eye R store that was left as the engine made it") {
    FakeView v;
    PrevMatrixBook book(fakeRanges());
    Frames frames;
    frame(v, book, frames, Eye::Left, left(1));
    CHECK(frame(v, book, frames, Eye::Right, right(1)) == left(1)); // nothing kept for eye R yet
    CHECK_FALSE(book.undoRewrite(v.bytes.data()));
    FakeView other;
    CHECK_FALSE(book.undoRewrite(other.bytes.data())); // a view the book never saw
    CHECK(book.stats().undone == 0);
}
