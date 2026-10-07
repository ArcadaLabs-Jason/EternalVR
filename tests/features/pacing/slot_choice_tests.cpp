#include "features/pacing/slot_choice.hpp"

#include <doctest/doctest.h>

#include <limits>

using namespace evr::pacing;

namespace {

constexpr double kPeriod = 1.0 / 90.0;

SlotOffer offer(bool newestUnshown, bool newestRendered, bool previousReady) {
    SlotOffer o;
    o.newestUnshown = newestUnshown;
    o.newestRendered = newestRendered;
    o.previousReady = previousReady;
    return o;
}

} // namespace

TEST_CASE("the wait for the newest image ends the submit margin before the period does") {
    CHECK(newestWaitSeconds(kPeriod, 0.0) == doctest::Approx(kPeriod - kSubmitMarginSeconds));
    CHECK(newestWaitSeconds(kPeriod, 0.002) == doctest::Approx(kPeriod - kSubmitMarginSeconds - 0.002));
    CHECK(newestWaitSeconds(kPeriod, kPeriod - kSubmitMarginSeconds) == 0.0);
    CHECK(newestWaitSeconds(kPeriod, kPeriod) == 0.0);
    CHECK(newestWaitSeconds(kPeriod, 1.0) == 0.0);
}

TEST_CASE("no display period yet: 72 Hz") {
    CHECK(newestWaitSeconds(0.0, 0.0) == doctest::Approx(kDefaultPeriodSeconds - kSubmitMarginSeconds));
    CHECK(newestWaitSeconds(-1.0, 0.0) == doctest::Approx(kDefaultPeriodSeconds - kSubmitMarginSeconds));
    CHECK(newestWaitSeconds(std::numeric_limits<double>::quiet_NaN(), 0.0) ==
          doctest::Approx(kDefaultPeriodSeconds - kSubmitMarginSeconds));
}

TEST_CASE("a clock read before the frame began counts as its start; a broken one leaves no wait") {
    CHECK(newestWaitSeconds(kPeriod, -0.003) == doctest::Approx(kPeriod - kSubmitMarginSeconds));
    CHECK(newestWaitSeconds(kPeriod, std::numeric_limits<double>::quiet_NaN()) == 0.0);
    CHECK(newestWaitSeconds(kPeriod, std::numeric_limits<double>::infinity()) == 0.0);
}

TEST_CASE("a rendered newest image is taken, with or without time left") {
    CHECK(chooseSlot(offer(true, true, false), 0.005) == SlotChoice::TakeNewest);
    CHECK(chooseSlot(offer(true, true, true), 0.0) == SlotChoice::TakeNewest);
}

TEST_CASE("a newest image still rendering is waited for while the frame has time") {
    CHECK(chooseSlot(offer(true, false, true), 0.005) == SlotChoice::WaitNewest);
    CHECK(chooseSlot(offer(true, false, false), kMinWaitSeconds) == SlotChoice::WaitNewest);
}

TEST_CASE("out of time: the previous image if ready, else the last one again") {
    CHECK(chooseSlot(offer(true, false, true), 0.0) == SlotChoice::TakePrevious);
    CHECK(chooseSlot(offer(true, false, true), kMinWaitSeconds / 2.0) == SlotChoice::TakePrevious);
    CHECK(chooseSlot(offer(true, false, false), 0.0) == SlotChoice::Repeat);
}

TEST_CASE("nothing newer than the last image shown: repeat, never wait") {
    CHECK(chooseSlot(offer(false, false, false), 0.005) == SlotChoice::Repeat);
    CHECK(chooseSlot(offer(false, true, false), 0.005) == SlotChoice::Repeat);
}

TEST_CASE("too little time left to start a wait and no previous image: repeat") {
    CHECK(chooseSlot(offer(true, false, false), kMinWaitSeconds / 2.0) == SlotChoice::Repeat);
}

TEST_CASE("an infinite display period counts as none given") {
    CHECK(newestWaitSeconds(std::numeric_limits<double>::infinity(), 0.0) ==
          doctest::Approx(kDefaultPeriodSeconds - kSubmitMarginSeconds));
}
