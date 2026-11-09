#include "features/pacing/late_image.hpp"

#include "features/pacing/slot_choice.hpp"

#include <doctest/doctest.h>

#include <limits>

using namespace evr::pacing;

namespace {

constexpr double kPeriod = 1.0 / 90.0;

// Held, no image since the previous headset frame, the last hand-over 1.4 periods ago, 0.3 ms into the frame.
LateImageOffer drawing() {
    LateImageOffer o;
    o.held = true;
    o.newImage = false;
    o.sinceHandOverSeconds = 1.4 * kPeriod;
    o.sinceFrameStartSeconds = 0.0003;
    o.periodSeconds = kPeriod;
    return o;
}

} // namespace

TEST_CASE("held, the game still drawing: the frame waits until the submit margin and the GPU's share before "
          "its period ends") {
    CHECK(lateImageWaitSeconds(drawing()) ==
          doctest::Approx(kPeriod - kSubmitMarginSeconds - kLateImageGpuSeconds - 0.0003));
    LateImageOffer o = drawing();
    o.sinceFrameStartSeconds = 0.002;
    CHECK(lateImageWaitSeconds(o) ==
          doctest::Approx(kPeriod - kSubmitMarginSeconds - kLateImageGpuSeconds - 0.002));
    o.sinceFrameStartSeconds = -0.001; // a clock read before the frame began counts as its start
    CHECK(lateImageWaitSeconds(o) == doctest::Approx(kPeriod - kSubmitMarginSeconds - kLateImageGpuSeconds));
}

TEST_CASE("no wait with a new image there, or with the game not held") {
    LateImageOffer o = drawing();
    o.newImage = true;
    CHECK(lateImageWaitSeconds(o) == 0.0);
    o = drawing();
    o.held = false;
    CHECK(lateImageWaitSeconds(o) == 0.0);
}

TEST_CASE("no wait for a game not drawing at the headset's rate (a loading screen, a stall)") {
    LateImageOffer o = drawing();
    o.sinceHandOverSeconds = kDrawingPeriods * kPeriod;
    CHECK(lateImageWaitSeconds(o) > 0.0);
    o.sinceHandOverSeconds = kDrawingPeriods * kPeriod + 0.0001;
    CHECK(lateImageWaitSeconds(o) == 0.0);
    o.sinceHandOverSeconds = 1.0;
    CHECK(lateImageWaitSeconds(o) == 0.0);
}

TEST_CASE("no wait once the frame has no time left for it, or with times that make no sense") {
    LateImageOffer o = drawing();
    o.sinceFrameStartSeconds = kPeriod - kSubmitMarginSeconds - kLateImageGpuSeconds - kMinWaitSeconds / 2.0;
    CHECK(lateImageWaitSeconds(o) == 0.0);
    o.sinceFrameStartSeconds = kPeriod;
    CHECK(lateImageWaitSeconds(o) == 0.0);
    o = drawing();
    o.periodSeconds = 0.0;
    CHECK(lateImageWaitSeconds(o) == 0.0);
    o.periodSeconds = std::numeric_limits<double>::quiet_NaN();
    CHECK(lateImageWaitSeconds(o) == 0.0);
    o = drawing();
    o.sinceHandOverSeconds = std::numeric_limits<double>::infinity();
    CHECK(lateImageWaitSeconds(o) == 0.0);
    o = drawing();
    o.sinceFrameStartSeconds = std::numeric_limits<double>::quiet_NaN();
    CHECK(lateImageWaitSeconds(o) == 0.0);
}
