#include "ui_layer/gui_hold.hpp"

#include <doctest/doctest.h>

#include <optional>

using evr::ui_layer::GuiGapTally;
using evr::ui_layer::guiImageShown;
using evr::ui_layer::kGuiFreshSeconds;
using evr::ui_layer::kGuiStallHoldSeconds;

namespace {

constexpr double kFrame = 1.0 / 90.0;

// A gap of `frames` headset frames after a new image shown with the GUI quad (GUI image 0 s old then), each
// frame's quad shown while `shownUpTo` seconds old or younger; the new image that ends it is returned.
std::optional<GuiGapTally::Gap> runGap(GuiGapTally& tally, int frames, double shownUpTo) {
    CHECK_FALSE(tally.onFrame(true, kFrame, 0.0, true));
    for (int i = 1; i <= frames; ++i) {
        const double age = i * kFrame;
        CHECK_FALSE(tally.onFrame(false, age, age, age <= shownUpTo));
    }
    return tally.onFrame(true, (frames + 1) * kFrame, 0.0, true);
}

} // namespace

TEST_CASE("gui hold: a capture that stopped hides the quad after 250 ms, a late game after a second") {
    // New images without a GUI image (a menu, a failed check): as before, 0.25 s.
    CHECK(guiImageShown(0.0, false));
    CHECK(guiImageShown(kGuiFreshSeconds, false));
    CHECK_FALSE(guiImageShown(kGuiFreshSeconds + 0.001, false));
    CHECK_FALSE(guiImageShown(0.7, false));
    // No new image since the GUI image's (the game late, a checkpoint save's 0.5 to 0.8 s): kept up.
    CHECK(guiImageShown(0.0, true));
    CHECK(guiImageShown(0.3, true));
    CHECK(guiImageShown(0.72, true));
    CHECK(guiImageShown(kGuiStallHoldSeconds, true));
    // A longer hang: gone as for a capture that stopped.
    CHECK_FALSE(guiImageShown(kGuiStallHoldSeconds + 0.001, true));
    CHECK_FALSE(guiImageShown(5.0, true));
}

TEST_CASE("gui gap tally: frames past the fresh limit are counted shown or left out") {
    GuiGapTally tally;
    // 0.7 s without a new image, the quad gone after kGuiFreshSeconds.
    const auto dropped = runGap(tally, 63, kGuiFreshSeconds);
    REQUIRE(dropped);
    CHECK(dropped->gapMs == doctest::Approx(64 * kFrame * 1000.0));
    CHECK(dropped->shown == 0);
    CHECK(dropped->left == 63 - 22); // frames 23 to 63 are past 0.25 s
    // The same gap with the quad kept up throughout.
    const auto kept = runGap(tally, 63, 10.0);
    REQUIRE(kept);
    CHECK(kept->shown == 63 - 22);
    CHECK(kept->left == 0);
    CHECK(kept->lastShownAgeMs == doctest::Approx(63 * kFrame * 1000.0));
}

TEST_CASE("gui gap tally: short gaps and gaps that began without the quad are not reported") {
    GuiGapTally tally;
    CHECK_FALSE(runGap(tally, 10, 10.0)); // never past kGuiFreshSeconds
    // A new image without the quad (a menu, a cutscene): the gap after it is not one the quad was in.
    CHECK_FALSE(tally.onFrame(true, kFrame, 0.0, false));
    for (int i = 1; i <= 60; ++i) {
        CHECK_FALSE(tally.onFrame(false, i * kFrame, 1.0 + i * kFrame, false));
    }
    CHECK_FALSE(tally.onFrame(true, 61 * kFrame, 0.0, false));
    // No GUI image at all.
    CHECK_FALSE(tally.onFrame(true, kFrame, -1.0, false));
    CHECK_FALSE(tally.onFrame(false, 0.5, -1.0, false));
    CHECK_FALSE(tally.onFrame(true, 0.6, -1.0, false));
}

TEST_CASE("gui gap tally: a gap is reported once") {
    GuiGapTally tally;
    REQUIRE(runGap(tally, 40, 0.0));
    CHECK_FALSE(tally.onFrame(true, kFrame, 0.0, true));
}
