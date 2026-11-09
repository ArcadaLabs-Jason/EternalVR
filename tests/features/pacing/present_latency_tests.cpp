#include "features/pacing/present_latency.hpp"

#include <doctest/doctest.h>

#include <cstdint>

using namespace evr::pacing;

namespace {

constexpr double kPeriod = 1.0 / 90.0;
constexpr double kMs = 0.001;

// A headset frame whose xrWaitFrame was called 9 ms and returned 0.3 ms before its choice at `chosen`.
void frameAt(PresentLatency& p, double chosen) {
    p.frameChosen(chosen - 9.3 * kMs, chosen - 0.3 * kMs, chosen);
}

// The XR worker's frame shows image `value`: taken, copied and submitted after the choice at `chosen`.
void show(PresentLatency& p, std::uint64_t value, double chosen) {
    p.slotTaken(value, chosen + 0.5 * kMs);
    p.copied(value, chosen + 1.0 * kMs);
    p.endCalled(chosen + 1.5 * kMs);
}

// The game, released at `released`, presents eye L at +3 ms and hands image `value` over at +`tookMs`; its
// present hook returns 0.2 ms later, `held` when it then waited for a headset frame.
void gameImage(PresentLatency& p, std::uint64_t value, double released, double tookMs) {
    p.presentReturned(released + 3.0 * kMs, false); // eye L
    p.handOver(value, released + tookMs * kMs);
}

double sum(const LatencyWindow& w) {
    return w.beforeWaitFrame.back() + w.inWaitFrame.back() + w.beginToChoice.back() + w.choiceToTaken.back() +
           w.takenToCopied.back() + w.copiedToEnd.back();
}

} // namespace

TEST_CASE("a held image: the game's time, the wake, the slack before the next choice, and where its way to "
          "xrEndFrame went") {
    PresentLatency p;
    // Image 1 starts the game's clock; it is released by headset frame 1's choice at t = 1 s.
    p.handOver(1, 0.99);
    frameAt(p, 1.0);
    show(p, 1, 1.0);
    p.presentReturned(1.0 + 0.2 * kMs, true); // released 0.2 ms after the choice
    const double released = 1.0 + 0.2 * kMs;
    gameImage(p, 2, released, 6.0);
    p.presentReturned(released + 6.2 * kMs, true); // the hold
    frameAt(p, 1.0 + kPeriod);
    show(p, 2, 1.0 + kPeriod);
    const LatencyWindow w = p.take();
    CHECK(w.handOvers == 2);
    REQUIRE(w.releaseToHandOver.size() == 1); // image 1 had no release before it
    CHECK(w.releaseToHandOver[0] == doctest::Approx(6.0));
    REQUIRE(w.releaseToFirstPresent.size() == 1);
    CHECK(w.releaseToFirstPresent[0] == doctest::Approx(3.0));
    CHECK(w.firstPresentToHandOver[0] == doctest::Approx(3.0));
    CHECK(w.heldReleases == 1);
    REQUIRE(w.wake.size() == 1);
    CHECK(w.wake[0] == doctest::Approx(0.2));
    REQUIRE(w.slack.size() == 1);
    CHECK(w.slack[0] == doctest::Approx(kPeriod / kMs - 6.2));
    CHECK(w.late == 0);
    CHECK(w.shownFirst == 2);
    CHECK(w.shownLater == 0);
    CHECK(w.neverShown == 0);
    // Image 2: handed over at released + 6 ms, frame 2 called xrWaitFrame at its choice - 9.3 ms.
    const double handOver = released + 6.0 * kMs;
    const double choice = 1.0 + kPeriod;
    REQUIRE(w.handOverToEnd.size() == 2);
    CHECK(w.handOverToEnd[1] == doctest::Approx((choice + 1.5 * kMs - handOver) / kMs));
    CHECK(w.beforeWaitFrame[1] == 0.0); // handed over while the worker was in xrWaitFrame
    CHECK(w.inWaitFrame[1] == doctest::Approx((choice - 0.3 * kMs - handOver) / kMs));
    CHECK(w.beginToChoice[1] == doctest::Approx(0.3));
    CHECK(w.choiceToTaken[1] == doctest::Approx(0.5));
    CHECK(w.takenToCopied[1] == doctest::Approx(0.5));
    CHECK(w.copiedToEnd[1] == doctest::Approx(0.5));
    CHECK(sum(w) == doctest::Approx(w.handOverToEnd[1]));
    REQUIRE(w.choiceInterval.size() == 1);
    CHECK(w.choiceInterval[0] == doctest::Approx(kPeriod / kMs));
}

TEST_CASE("a held image handed over after the next choice is late: negative slack, the frame between shows "
          "nothing new") {
    PresentLatency p;
    p.handOver(1, 0.99);
    frameAt(p, 1.0);
    show(p, 1, 1.0);
    p.presentReturned(1.0 + 0.2 * kMs, true);
    const double released = 1.0 + 0.2 * kMs;
    frameAt(p, 1.0 + kPeriod); // frame 2: no new image
    p.endCalled(1.0 + kPeriod + 1.5 * kMs);
    gameImage(p, 2, released, 13.0); // after frame 2's choice
    p.presentReturned(released + 13.2 * kMs, false);
    frameAt(p, 1.0 + 2.0 * kPeriod);
    show(p, 2, 1.0 + 2.0 * kPeriod);
    const LatencyWindow w = p.take();
    REQUIRE(w.slack.size() == 1);
    CHECK(w.slack[0] == doctest::Approx(kPeriod / kMs - 0.2 - 13.0));
    CHECK(w.late == 1);
    CHECK(w.shownFirst == 2); // frame 3 is the first chosen after the hand-over
    CHECK(w.releaseToHandOver[0] == doctest::Approx(13.0));
}

TEST_CASE("an image handed over in time but shown a frame later (the GPU fence ran out) counts as later") {
    PresentLatency p;
    p.handOver(1, 0.99);
    frameAt(p, 1.0);
    show(p, 1, 1.0);
    p.presentReturned(1.0 + 0.2 * kMs, true);
    gameImage(p, 2, 1.0 + 0.2 * kMs, 6.0);
    p.presentReturned(1.0 + 6.4 * kMs, true);
    frameAt(p, 1.0 + kPeriod); // waits for image 2's GPU work, runs out: no copy
    p.endCalled(1.0 + kPeriod + 7.1 * kMs);
    frameAt(p, 1.0 + 2.0 * kPeriod);
    show(p, 2, 1.0 + 2.0 * kPeriod);
    const LatencyWindow w = p.take();
    CHECK(w.late == 0);
    CHECK(w.shownFirst == 1);
    CHECK(w.shownLater == 1);
    CHECK(sum(w) == doctest::Approx(w.handOverToEnd.back()));
}

TEST_CASE("an older image never copied once a newer one is shown is never shown") {
    PresentLatency p;
    p.handOver(1, 1.0);
    p.presentReturned(1.0 + 0.1 * kMs, false);
    p.handOver(2, 1.0 + 5.0 * kMs);
    p.presentReturned(1.0 + 5.1 * kMs, false);
    frameAt(p, 1.0 + 6.0 * kMs);
    show(p, 2, 1.0 + 6.0 * kMs);
    const LatencyWindow w = p.take();
    CHECK(w.neverShown == 1);
    CHECK(w.shownFirst == 1);
    CHECK(w.handOverToEnd.size() == 1);
    // Not held: the game's time is measured, no wake or slack.
    CHECK(w.heldReleases == 0);
    CHECK(w.wake.empty());
    CHECK(w.slack.empty());
    REQUIRE(w.releaseToHandOver.size() == 1); // image 1 had no release before it
    CHECK(w.releaseToHandOver[0] == doctest::Approx(5.0 - 0.1));
    REQUIRE(w.handOverToReturn.size() == 2);
    CHECK(w.handOverToReturn[0] == doctest::Approx(0.1));
    CHECK(w.handOverToReturn[1] == doctest::Approx(0.1));
}

TEST_CASE(
    "an image handed over after the frame's choice and taken by it: all of its time is the frame's own") {
    PresentLatency p;
    frameAt(p, 1.0);
    p.handOver(1, 1.0 + 0.2 * kMs);
    p.presentReturned(1.0 + 0.3 * kMs, false);
    show(p, 1, 1.0);
    const LatencyWindow w = p.take();
    CHECK(w.shownFirst == 1);
    CHECK(w.beforeWaitFrame[0] == 0.0);
    CHECK(w.inWaitFrame[0] == 0.0);
    CHECK(w.beginToChoice[0] == 0.0);
    CHECK(w.choiceToTaken[0] == doctest::Approx(0.3));
    CHECK(sum(w) == doctest::Approx(w.handOverToEnd[0]));
}

TEST_CASE("more images open than followed: the oldest are dropped and counted") {
    PresentLatency p;
    for (std::uint64_t v = 1; v <= PresentLatency::kOpen + 3; ++v) {
        p.handOver(v, 1.0 + static_cast<double>(v) * kMs);
        p.presentReturned(1.0 + (static_cast<double>(v) + 0.1) * kMs, false);
    }
    const LatencyWindow w = p.take();
    CHECK(w.handOvers == PresentLatency::kOpen + 3);
    CHECK(w.unfollowed == 3);
}

TEST_CASE("records are kept only when asked, and a window starts over after take") {
    PresentLatency p;
    p.handOver(1, 0.99);
    frameAt(p, 1.0);
    show(p, 1, 1.0);
    CHECK(p.take().records.empty());
    p.keepRecords(true);
    p.presentReturned(1.0 + 0.2 * kMs, true);
    gameImage(p, 2, 1.0 + 0.2 * kMs, 6.0);
    p.presentReturned(1.0 + 6.4 * kMs, true);
    frameAt(p, 1.0 + kPeriod);
    show(p, 2, 1.0 + kPeriod);
    const LatencyWindow w = p.take();
    REQUIRE(w.records.size() == 1);
    CHECK(w.records[0].value == 2);
    CHECK(w.records[0].held);
    CHECK(w.records[0].shownFrame == 2);
    CHECK(w.records[0].framesAtRelease == 1);
    CHECK(w.handOvers == 1);
    const LatencyWindow empty = p.take();
    CHECK(empty.handOvers == 0);
    CHECK(empty.handOverToEnd.empty());
}
