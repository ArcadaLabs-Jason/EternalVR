#include "stereo_seq/pass_frames.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>

using evr::stereo_seq::PassFrames;

namespace {

using Source = PassFrames::Source;

// Command buffers of the two parities, as the engine's contexts hold them.
constexpr std::uint64_t kEven = 0x1000;
constexpr std::uint64_t kOdd = 0x2000;

// One backend frame with its own counter `c`, recorded into `cb` from its begin: a pass before the
// render-view job (the counter still c, the latest read c - 1), one after it, one after the swap (the
// counter c + 1).
struct FrameAnswers {
    PassFrames::Answer early;
    PassFrames::Answer agreed;
    PassFrames::Answer late;
};

FrameAnswers recordFrame(PassFrames& book, std::uint64_t cb, std::uint32_t c) {
    FrameAnswers a;
    book.begin(cb);
    a.early = book.find(cb, c, c - 1u);
    a.agreed = book.find(cb, c, c);
    a.late = book.find(cb, c + 1u, c);
    return a;
}

std::uint64_t bufferFor(std::uint32_t c) {
    return (c & 1u) == 0 ? kEven : kOdd;
}

// Frames [from, to) in the engine's two sets: every buffer's parity learned once two of its frames are in.
void steadyFrames(PassFrames& book, std::uint32_t from, std::uint32_t to) {
    for (std::uint32_t c = from; c < to; ++c) {
        recordFrame(book, bufferFor(c), c);
    }
}

} // namespace

TEST_CASE("pass frames: the counter now and the render-view read agree") {
    PassFrames book;
    const PassFrames::Answer a = book.find(kEven, 10, 10);
    CHECK(a.counter == 10u);
    CHECK(a.source == Source::Agreed);
    CHECK(a.used(false) == 10u); // the one frame used without the test knob
}

TEST_CASE("pass frames: nothing learned gives no frame, never the counter now") {
    PassFrames book;
    book.begin(kEven);
    // After the swap: the counter now names the next frame.
    CHECK_FALSE(book.find(kEven, 11, 10).counter.has_value());
    // Before the render-view job: the latest read is the previous frame's.
    CHECK_FALSE(book.find(kOdd, 11, 10).counter.has_value());
    // No render-view read noted at all.
    CHECK_FALSE(book.find(kEven, 11, std::nullopt).counter.has_value());
    CHECK(book.stats().unknown == 3);
}

TEST_CASE("pass frames: a pass after the swap keeps its recording's counter") {
    PassFrames book;
    book.find(kEven, 8, 8); // learned once, in a recording whose begin was not seen
    book.begin(kEven);
    CHECK(book.find(kEven, 10, 10).counter == 10u);
    const PassFrames::Answer late = book.find(kEven, 11, 10);
    CHECK(late.counter == 10u);
    CHECK(late.source == Source::SameRecording);
    CHECK_FALSE(late.used(false).has_value()); // a guess: full rate
    CHECK(late.used(true) == 10u);             // ETERNALVR_TEST_VRS_PARITY=1
}

TEST_CASE("pass frames: in steady frames only the agreed passes get a frame, and the guesses are right") {
    PassFrames book;
    steadyFrames(book, 100, 104); // two recordings of each buffer: parities learned
    for (std::uint32_t c = 104; c < 140; ++c) {
        CAPTURE(c);
        const FrameAnswers a = recordFrame(book, bufferFor(c), c);
        CHECK(a.early.counter == c);
        CHECK(a.early.source == Source::Parity);
        CHECK_FALSE(a.early.used(false).has_value()); // full rate
        CHECK(a.agreed.used(false) == c);
        CHECK(a.late.counter == c);
        CHECK(a.late.source == Source::SameRecording);
        CHECK_FALSE(a.late.used(false).has_value());
    }
    const PassFrames::Stats s = book.stats();
    CHECK(s.parityBreaks == 0);
    CHECK(s.contradicted == 0);
    // Every pass but the agreed ones at full rate; with the test knob only those with no guess.
    CHECK(s.fullRate(false) == s.unknown + s.sameRecording + s.parity);
    CHECK(s.fullRate(true) == s.unknown);
    CHECK(s.agreed + s.fullRate(false) == 40u * 3u);
}

TEST_CASE("pass frames: a recording with no agreement guesses by parity after the swap too") {
    PassFrames book;
    steadyFrames(book, 20, 24);
    // Frame 24 is all recorded after its swap: the counter reads 25, the render-view read 24.
    book.begin(bufferFor(24));
    const PassFrames::Answer a = book.find(bufferFor(24), 25, 24);
    CHECK(a.counter == 24u);
    CHECK(a.source == Source::Parity);
    CHECK_FALSE(a.used(false).has_value());
    // Frame 25, before its render-view job: the counter reads 25, the latest read 24.
    book.begin(bufferFor(25));
    const PassFrames::Answer b = book.find(bufferFor(25), 25, 24);
    CHECK(b.counter == 25u);
    CHECK(b.source == Source::Parity);
    // Frame 24's recording never agreed, so its frame was never seen: frame 26's early pass has no frame.
    book.find(bufferFor(25), 25, 25);
    book.begin(bufferFor(26));
    CHECK_FALSE(book.find(bufferFor(26), 26, 25).counter.has_value());
}

TEST_CASE("pass frames: one recording is not enough to guess by parity") {
    PassFrames book;
    recordFrame(book, kEven, 30);
    // Frame 32, before its render-view job.
    book.begin(kEven);
    CHECK_FALSE(book.find(kEven, 32, 31).counter.has_value());
    book.find(kEven, 32, 32);
    book.begin(kEven);
    CHECK(book.find(kEven, 34, 33).counter == 34u);
}

// H1: the previous frame's recording never answers for this one.
TEST_CASE("pass frames: a buffer recorded in consecutive frames gets no frame before its agreement") {
    PassFrames book;
    book.find(kEven, 9, 9);
    for (std::uint32_t c = 10; c < 20; ++c) {
        CAPTURE(c);
        const FrameAnswers a = recordFrame(book, kEven, c);
        CHECK_FALSE(a.early.counter.has_value()); // not frame c - 1's counter, the other eye's
        CHECK(a.agreed.counter == c);
        CHECK(a.late.counter == c);
    }
    // It never learns a parity, and its agreements do not count as broken ones.
    CHECK(book.stats().parity == 0);
    CHECK(book.stats().parityBreaks == 0);
}

TEST_CASE("pass frames: a new recording does not answer by the previous one's counter") {
    PassFrames book;
    book.find(kEven, 10, 10);
    book.begin(kEven);
    book.find(kEven, 10, 10); // this recording agreed at 10
    book.begin(kEven);        // a new one, before frame 11's render-view job
    const PassFrames::Answer a = book.find(kEven, 11, 10);
    CHECK_FALSE(a.counter.has_value());
    CHECK(book.stats().sameRecording == 0);
}

TEST_CASE("pass frames: a reset ends the recording") {
    PassFrames book;
    book.find(kEven, 10, 10);
    book.begin(kEven);
    book.find(kEven, 10, 10);
    CHECK(book.find(kEven, 11, 10).source == Source::SameRecording);
    book.reset(kEven); // vkResetCommandBuffer or its pool's reset
    CHECK_FALSE(book.find(kEven, 11, 10).counter.has_value());
    // Learned parities need recordings whose begin was seen: a reset with no begin after it stops them too.
    PassFrames other;
    steadyFrames(other, 40, 44);
    other.reset(kEven);
    CHECK_FALSE(other.find(kEven, 44, 43).counter.has_value());
}

TEST_CASE("pass frames: without begins, only the passes whose counters agree get a frame") {
    PassFrames book;
    for (std::uint32_t c = 50; c < 60; ++c) {
        CAPTURE(c);
        CHECK_FALSE(book.find(bufferFor(c), c, c - 1u).counter.has_value());
        CHECK(book.find(bufferFor(c), c, c).counter == c);
        CHECK_FALSE(book.find(bufferFor(c), c + 1u, c).counter.has_value());
    }
    CHECK(book.stats().sameRecording == 0);
    CHECK(book.stats().parity == 0);
}

// H2: a change of the parity link.
TEST_CASE("pass frames: an agreement against the learned parity starts every command buffer over") {
    PassFrames book;
    recordFrame(book, kEven, 40);
    recordFrame(book, kEven, 42);
    recordFrame(book, kOdd, 41);
    recordFrame(book, kOdd, 43);
    // Frame 44 in kEven, by parity, all of it before the render-view job.
    book.begin(kEven);
    CHECK(book.find(kEven, 44, 43).source == Source::Parity);
    // The link moves on by a frame: kEven records frame 45 too. Its 44 recording never agreed: no frame.
    book.begin(kEven);
    CHECK_FALSE(book.find(kEven, 45, 44).counter.has_value());
    CHECK(book.find(kEven, 45, 45).counter == 45u); // against its learned parity: the break
    CHECK(book.stats().parityBreaks == 1);
    // kOdd learned its odd frames under the old link: no frame until it learns again.
    book.begin(kOdd);
    CHECK_FALSE(book.find(kOdd, 46, 45).counter.has_value());
    book.find(kOdd, 46, 46);
    recordFrame(book, kEven, 47);
    recordFrame(book, kOdd, 48);
    CHECK(recordFrame(book, kEven, 49).early.counter == 49u); // 45, 47 under the new link
    CHECK(recordFrame(book, kOdd, 50).early.counter == 50u);  // 46, 48
    CHECK(book.stats().parityBreaks == 1);
}

TEST_CASE("pass frames: the early pass of a shifted recording gets no frame") {
    PassFrames book;
    recordFrame(book, kEven, 40);
    recordFrame(book, kEven, 42);
    recordFrame(book, kEven, 44); // the parity answered 44, and the recording agreed at 44
    // The link shifts: the same buffer records frame 45 as well. Its parity would say 44, the other eye's.
    book.begin(kEven);
    const PassFrames::Answer early = book.find(kEven, 45, 44);
    CHECK_FALSE(early.counter.has_value());
    CHECK(book.stats().parityBreaks == 1);
    CHECK(book.find(kEven, 45, 45).counter == 45u);
}

// H1, S1: a buffer recorded in the frame after its last agreed one, its first pass after the swap.
TEST_CASE("pass frames: a parity guess cannot tell the next frame's recording after the swap: full rate") {
    PassFrames book;
    recordFrame(book, kEven, 40);
    recordFrame(book, kEven, 42); // parity learned: even
    // Frame 43 recorded into kEven, all of it after the swap: the counter reads 44, the render-view read 43.
    book.begin(kEven);
    const PassFrames::Answer s1 = book.find(kEven, 44, 43);
    // The same input as frame 44's early pass in steady frames: the parity guesses 44, frame 43's other eye.
    CHECK(s1.counter == 44u);
    CHECK(s1.source == Source::Parity);
    CHECK_FALSE(s1.used(false).has_value()); // without the test knob: full rate
    CHECK(s1.used(true) == 44u);             // with it: the wrong frame, and nothing sees it
    CHECK(book.stats().contradicted == 0);
    CHECK(book.stats().parityBreaks == 0);
}

// H1, S2: a buffer idle for a frame, then recorded at a gap of 3.
TEST_CASE("pass frames: a parity guess after an idle frame is full rate, and its agreement contradicts it") {
    PassFrames book;
    recordFrame(book, kEven, 40);
    recordFrame(book, kEven, 42); // parity learned: even
    // kEven is not recorded at 44. Frame 45 before its render-view job: the parity guesses 44.
    book.begin(kEven);
    const PassFrames::Answer s2 = book.find(kEven, 45, 44);
    CHECK(s2.counter == 44u);
    CHECK(s2.source == Source::Parity);
    CHECK_FALSE(s2.used(false).has_value()); // full rate
    // Its agreement at 45 contradicts the guess: counted, and a parity break.
    CHECK(book.find(kEven, 45, 45).used(false) == 45u);
    CHECK(book.stats().contradicted == 1);
    CHECK(book.stats().parityBreaks == 1);
    // A later pass of that recording after the swap guesses 45 by the recording.
    CHECK(book.find(kEven, 46, 45).counter == 45u);
    CHECK(book.stats().contradicted == 1);
}

TEST_CASE("pass frames: a parity break makes every buffer learn its parity again") {
    PassFrames book;
    steadyFrames(book, 40, 44); // kEven 40, 42; kOdd 41, 43
    // Frame 44 goes to kOdd: its early pass would get 43 by parity, the frame it last agreed at.
    book.begin(kOdd);
    CHECK_FALSE(book.find(kOdd, 44, 43).counter.has_value());
    CHECK(book.stats().parityBreaks == 1);
    CHECK(book.find(kOdd, 44, 44).counter == 44u);
    // Frame 45 goes to kEven: learned under the old link, it would say 44.
    const FrameAnswers a = recordFrame(book, kEven, 45);
    CHECK_FALSE(a.early.counter.has_value());
    CHECK(a.agreed.counter == 45u);
    // Two recordings two frames apart under the new link, and the parities answer again.
    recordFrame(book, kOdd, 46);
    recordFrame(book, kEven, 47);
    CHECK(recordFrame(book, kOdd, 48).early.source == Source::Parity);
    CHECK(recordFrame(book, kEven, 49).early.counter == 49u);
    CHECK(book.stats().parityBreaks == 1);
}

TEST_CASE("pass frames: no parity guess when the counter moved on more than once past the render-view read") {
    PassFrames book;
    steadyFrames(book, 60, 64);
    book.begin(kEven);
    CHECK_FALSE(book.find(kEven, 65, 63).counter.has_value()); // 64 or 65: not one of the two the model has
    book.begin(kOdd);
    CHECK_FALSE(book.find(kOdd, 65, std::nullopt).counter.has_value());
}

TEST_CASE("pass frames: a buffer idle for a frame pair guesses by parity only after two recordings") {
    PassFrames book;
    steadyFrames(book, 70, 74); // kEven 70, 72
    // kEven comes back at frame 76 (74 went elsewhere or nowhere): its parity is not trusted over the gap.
    const FrameAnswers a = recordFrame(book, kEven, 76);
    CHECK_FALSE(a.early.counter.has_value());
    CHECK_FALSE(recordFrame(book, kEven, 80).early.counter.has_value());
    recordFrame(book, kEven, 82);
    CHECK(recordFrame(book, kEven, 84).early.counter == 84u);
}

// L1: a recording that goes on into the next frame.
TEST_CASE("pass frames: a recording over two frames is contradicted and does not count towards a parity") {
    PassFrames book;
    recordFrame(book, kEven, 90);
    book.begin(kEven);
    book.find(kEven, 92, 92);
    // Frame 93 before its render-view job: the same input as frame 92's late pass, so the recording guesses
    // 92, the other eye's.
    const PassFrames::Answer between = book.find(kEven, 93, 92);
    CHECK(between.counter == 92u);
    CHECK(between.source == Source::SameRecording);
    CHECK_FALSE(between.used(false).has_value()); // full rate
    CHECK(book.stats().contradicted == 0);
    book.find(kEven, 93, 93); // the same recording agreed in the next frame too
    CHECK(book.stats().contradicted == 1);
    book.find(kEven, 94, 94); // counted once per recording
    CHECK(book.stats().contradicted == 1);
    book.begin(kEven);
    CHECK_FALSE(book.find(kEven, 95, 94).counter.has_value());
}

TEST_CASE("pass frames: counters wrap") {
    PassFrames book;
    constexpr std::uint32_t kTop = 0xFFFFFFFFu;
    recordFrame(book, kOdd, kTop - 2u);
    recordFrame(book, kOdd, kTop);
    // Frame 1 (after the wrap), before its render-view job: the latest read is 0.
    book.begin(kOdd);
    const PassFrames::Answer a = book.find(kOdd, 1, 0);
    CHECK(a.counter == 1u);
    CHECK(a.source == Source::Parity);
    CHECK_FALSE(a.used(false).has_value());
    // The swap moved the counter on past the wrap: frame 0xFFFFFFFF's late pass.
    PassFrames other;
    other.find(kEven, kTop - 1u, kTop - 1u);
    other.begin(kEven);
    CHECK(other.find(kEven, kTop, kTop).counter == kTop);
    CHECK(other.find(kEven, 0, kTop).counter == kTop);
}

TEST_CASE("pass frames: a full book learns no new command buffer until one is forgotten") {
    PassFrames book;
    for (std::uint64_t cb = 1; cb <= PassFrames::kCapacity; ++cb) {
        book.find(cb, 5, 5);
    }
    CHECK(book.full());
    CHECK(book.stats().commandBuffers == PassFrames::kCapacity);
    constexpr std::uint64_t kNew = PassFrames::kCapacity + 1;
    CHECK(book.find(kNew, 5, 5).counter == 5u); // agreements still answer
    CHECK(book.stats().commandBuffers == PassFrames::kCapacity);
    book.begin(kNew);
    CHECK_FALSE(book.find(kNew, 6, 5).counter.has_value());
    book.forget(1); // vkFreeCommandBuffers or its pool destroyed
    CHECK_FALSE(book.full());
    book.find(kNew, 7, 7);
    CHECK(book.stats().commandBuffers == PassFrames::kCapacity);
}

TEST_CASE("pass frames: a forgotten buffer's handle starts with nothing learned") {
    PassFrames book;
    steadyFrames(book, 10, 14);
    book.forget(kEven);
    book.begin(kEven); // a new buffer with the old one's handle
    CHECK_FALSE(book.find(kEven, 14, 13).counter.has_value());
    CHECK(book.stats().commandBuffers == 1);
}
