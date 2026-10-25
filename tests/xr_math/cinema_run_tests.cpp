#include "xr_math/cinema_run.hpp"

#include <doctest/doctest.h>

using namespace evr::xr_math;

TEST_CASE("a run's first frames and a cut's drop the history, the frames between keep it") {
    CinemaRun run;
    CHECK_FALSE(run.active());
    const CinemaPairStep first = run.pair(false, 1.0);
    CHECK(first.started);
    CHECK_FALSE(first.cut);
    CHECK(first.historyReset);
    CHECK(run.active());
    double t = 1.0;
    for (int i = 1; i < kCinemaResetFrames; ++i) {
        t += 0.011;
        const CinemaPairStep s = run.pair(false, t);
        CHECK_FALSE(s.started);
        CHECK(s.historyReset);
    }
    t += 0.011;
    CHECK_FALSE(run.pair(false, t).historyReset);
    // A cut: counted (not on a first frame) and kCinemaResetFrames frames reset again.
    t += 0.011;
    const CinemaPairStep cut = run.pair(true, t);
    CHECK(cut.cut);
    CHECK_FALSE(cut.started);
    CHECK(cut.historyReset);
    for (int i = 1; i < kCinemaResetFrames; ++i) {
        t += 0.011;
        CHECK(run.pair(false, t).historyReset);
    }
    t += 0.011;
    CHECK_FALSE(run.pair(false, t).historyReset);
}

TEST_CASE("a cut byte on a run's first frame is not a cut") {
    CinemaRun run;
    const CinemaPairStep s = run.pair(true, 0.0);
    CHECK(s.started);
    CHECK_FALSE(s.cut);
}

TEST_CASE("a gap between pair frames starts a new run") {
    CinemaRun run;
    run.pair(false, 0.0);
    for (int i = 0; i < 10; ++i) {
        run.pair(false, 0.01 * (i + 1));
    }
    const CinemaPairStep near = run.pair(false, 0.1 + kCinemaRunGapSeconds * 0.9);
    CHECK_FALSE(near.started);
    const CinemaPairStep gap = run.pair(false, 0.1 + kCinemaRunGapSeconds * 0.9 + kCinemaRunGapSeconds * 1.1);
    CHECK(gap.started);
    CHECK(gap.afterGap);
    CHECK(gap.historyReset);
}

TEST_CASE("the end of a run resets the first head-tracked frames after it, once") {
    CinemaRun run;
    bool ended = false;
    // No run: nothing to end or reset.
    CHECK_FALSE(run.end());
    CHECK_FALSE(run.headTracked(false, ended));
    CHECK_FALSE(ended);
    run.pair(false, 0.0);
    // Still in the cutscene (an immersive frame between): the run goes on.
    CHECK_FALSE(run.headTracked(true, ended));
    CHECK_FALSE(ended);
    CHECK(run.active());
    // The player's view is back.
    CHECK(run.headTracked(false, ended));
    CHECK(ended);
    CHECK_FALSE(run.active());
    for (int i = 1; i < kCinemaResetFrames; ++i) {
        CHECK(run.headTracked(false, ended));
        CHECK_FALSE(ended);
    }
    CHECK_FALSE(run.headTracked(false, ended));
    // A run ended by a frame that is not a pair resets the next head-tracked frames too.
    run.pair(false, 1.0);
    CHECK(run.end());
    CHECK_FALSE(run.end());
    CHECK(run.headTracked(false, ended));
    CHECK_FALSE(ended);
}

TEST_CASE("the layer plan: the eyes for a pair frame, the GUI quad while fresh, the composite held") {
    // Not a pair (a mono frame, a menu over the cutscene): the one quad, the composite as it was.
    const CinemaLayerPlan mono = cinemaLayerPlan(false, true, 0.0);
    CHECK_FALSE(mono.eyes);
    CHECK_FALSE(mono.gui);
    CHECK_FALSE(mono.skipComposite);
    const CinemaLayerPlan fresh = cinemaLayerPlan(true, true, 0.01);
    CHECK(fresh.eyes);
    CHECK(fresh.gui);
    CHECK(fresh.skipComposite);
    // The GUI went stale: the eyes stay (no flip to mono), no GUI quad, and the GUI stays out of eye L's
    // image for the hold.
    const CinemaLayerPlan stale = cinemaLayerPlan(true, false, 0.5 * kCinemaGuiHoldSeconds);
    CHECK(stale.eyes);
    CHECK_FALSE(stale.gui);
    CHECK(stale.skipComposite);
    // The hold's last moment still counts.
    const CinemaLayerPlan edge = cinemaLayerPlan(true, false, kCinemaGuiHoldSeconds);
    CHECK(edge.eyes);
    CHECK_FALSE(edge.gui);
    CHECK(edge.skipComposite);
}

TEST_CASE("the layer plan: a pair with no GUI image within the hold goes on the mono quad") {
    // The game composites its GUI into eye L's image only once the hold is over, so eye R would have no
    // subtitles: the frame goes on the one mono quad (eye L's half), GUI and all.
    const CinemaLayerPlan past = cinemaLayerPlan(true, false, kCinemaGuiHoldSeconds + 1e-6);
    CHECK_FALSE(past.eyes);
    CHECK_FALSE(past.gui);
    CHECK_FALSE(past.skipComposite);
    const CinemaLayerPlan stopped = cinemaLayerPlan(true, false, 2.0 * kCinemaGuiHoldSeconds);
    CHECK_FALSE(stopped.eyes);
    CHECK_FALSE(stopped.skipComposite);
    // No GUI image at all (the capture failed or never started, or its swapchain was made again).
    const CinemaLayerPlan none = cinemaLayerPlan(true, false, -1.0);
    CHECK_FALSE(none.eyes);
    CHECK_FALSE(none.gui);
    CHECK_FALSE(none.skipComposite);
    // A fresh image brings the pair back.
    const CinemaLayerPlan back = cinemaLayerPlan(true, true, 0.0);
    CHECK(back.eyes);
    CHECK(back.gui);
    CHECK(back.skipComposite);
}

TEST_CASE("a trace hit nearer than the confirm distance counts on the second frame in a row") {
    CinemaNearConfirm confirm;
    // A far hit, or none, passes at once.
    CHECK(confirm.step(2.0f, false) == doctest::Approx(2.0f));
    CHECK(confirm.step(0.0f, false) == 0.0f);
    // A near hit for one frame is held back, then gone: the flicker never reaches the limit.
    CHECK(confirm.step(0.3f, false) == 0.0f);
    CHECK(confirm.step(2.0f, false) == doctest::Approx(2.0f));
    // Two near frames in a row: the second counts, and so do the ones after it.
    CHECK(confirm.step(0.3f, false) == 0.0f);
    CHECK(confirm.step(0.28f, false) == doctest::Approx(0.28f));
    CHECK(confirm.step(0.27f, false) == doctest::Approx(0.27f));
    // A near hit on a run's first frame or a cut counts at once.
    confirm.reset();
    CHECK(confirm.step(0.3f, true) == doctest::Approx(0.3f));
}
