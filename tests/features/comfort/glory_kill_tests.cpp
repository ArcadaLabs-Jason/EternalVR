#include "features/comfort/glory_kill.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

using namespace evr::comfort;

namespace {

constexpr double kFrame = 1.0 / 60.0;

} // namespace

TEST_CASE("glory view: names parse back, any case and spacing") {
    for (const GloryView view : {GloryView::Follow, GloryView::Steady, GloryView::Fade, GloryView::Screen}) {
        const auto parsed = parseGloryView(gloryViewName(view));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == view);
    }
    CHECK(parseGloryView("  Steady ") == GloryView::Steady);
    CHECK(parseGloryView("FADE") == GloryView::Fade);
    CHECK_FALSE(parseGloryView("").has_value());
    CHECK_FALSE(parseGloryView("blink").has_value());
    CHECK_FALSE(parseGloryView("fades").has_value());
}

TEST_CASE("glory view: a kill's sync entity counts, a pickup's animation does not") {
    // Names traced in headset sessions and on the rig (docs/BHAPTICS.md).
    CHECK(isKillSync("syncmelee/imp"));
    CHECK(isKillSync("syncmelee/hell_knight"));
    CHECK(isKillSync(""));
    CHECK_FALSE(isKillSync("interact/argent_cell/use_sync"));
    CHECK_FALSE(isKillSync("interact/preator_suit_token/preator_suit_token_sync"));
    CHECK_FALSE(isKillSync("interact/rune/rune_sync"));
}

TEST_CASE("glory episode: starts with the sync flag and ends with it when the view is the player's") {
    GloryEpisode e;
    double t = 0.0;
    CHECK_FALSE(e.update(false, false, t).active);
    const auto first = e.update(true, false, t += kFrame);
    CHECK(first.active);
    CHECK(first.started);
    CHECK_FALSE(first.ended);
    for (int i = 0; i < 60; ++i) {
        const auto s = e.update(true, true, t += kFrame);
        CHECK(s.active);
        CHECK_FALSE(s.started);
    }
    const auto last = e.update(false, false, t += kFrame);
    CHECK_FALSE(last.active);
    CHECK(last.ended);
    CHECK(e.episodes() == 1);
    CHECK_FALSE(e.update(false, false, t += kFrame).ended);
}

TEST_CASE("glory episode: lasts while the game still forces the view after the kill, up to the settle time") {
    GloryEpisode e({0.5});
    double t = 0.0;
    e.update(true, true, t);
    // The camera eases back for a few frames: still the kill.
    for (int i = 0; i < 10; ++i) {
        CHECK(e.update(false, true, t += kFrame).active);
    }
    // The gate lets go.
    const auto s = e.update(false, false, t += kFrame);
    CHECK_FALSE(s.active);
    CHECK(s.ended);

    // A forced view that never lets go (the Meathook straight after) ends the episode at the settle time.
    e.update(true, true, t += 1.0);
    const double syncEnd = t + kFrame;
    bool ended = false;
    double endedAt = 0.0;
    for (int i = 0; i < 120 && !ended; ++i) {
        const auto step = e.update(false, true, t += kFrame);
        ended = step.ended;
        endedAt = t;
    }
    CHECK(ended);
    CHECK(endedAt - syncEnd == doctest::Approx(0.5).epsilon(0.05));
    CHECK(e.episodes() == 2);
}

TEST_CASE("glory episode: a forced view without a sync kill is not one") {
    GloryEpisode e;
    double t = 0.0;
    for (int i = 0; i < 30; ++i) {
        CHECK_FALSE(e.update(false, true, t += kFrame).active); // the Meathook pull, a melee lunge
    }
    CHECK(e.episodes() == 0);
}

TEST_CASE("glory episode: the sync flag back during the settle continues the same kill") {
    GloryEpisode e;
    double t = 0.0;
    e.update(true, true, t);
    e.update(false, true, t += kFrame);
    const auto again = e.update(true, true, t += kFrame);
    CHECK(again.active);
    CHECK_FALSE(again.started);
    CHECK(e.episodes() == 1);
}

TEST_CASE(
    "glory episode: a sync flag left set ends the kill at the maximum, and no kill starts until it clears") {
    GloryEpisode e({0.5, 10.0});
    double t = 0.0;
    CHECK(e.update(true, true, t).started);
    bool timedOut = false;
    double endedAt = 0.0;
    for (int i = 0; i < 20 * 60 && !timedOut; ++i) {
        const auto s = e.update(true, true, t += kFrame);
        if (s.ended) {
            CHECK_FALSE(s.active);
            timedOut = s.timedOut;
            endedAt = t;
        }
    }
    CHECK(timedOut);
    CHECK(endedAt == doctest::Approx(10.0).epsilon(0.01));
    // The flag still set: no new kill, nothing shown.
    for (int i = 0; i < 60; ++i) {
        const auto s = e.update(true, true, t += kFrame);
        CHECK_FALSE(s.active);
        CHECK_FALSE(s.started);
    }
    CHECK(e.episodes() == 1);
    // Once it clears, the next kill is a new one, with its own maximum.
    CHECK_FALSE(e.update(false, false, t += kFrame).ended);
    const auto next = e.update(true, true, t += kFrame);
    CHECK(next.started);
    CHECK_FALSE(next.timedOut);
    CHECK(e.episodes() == 2);
}

TEST_CASE("glory episode: a pause in the middle of a kill does not count toward the maximum") {
    GloryEpisode e({0.5, 10.0});
    double t = 0.0;
    e.update(true, true, t);
    for (int i = 0; i < 2 * 60; ++i) {
        CHECK(e.update(true, true, t += kFrame).active); // 2 s of the kill
    }
    for (int i = 0; i < 30 * 60; ++i) {
        const auto s = e.update(true, true, t += kFrame, true); // 30 s in the pause menu
        CHECK(s.active);
        CHECK_FALSE(s.ended);
    }
    for (int i = 0; i < 3 * 60; ++i) {
        CHECK(e.update(true, true, t += kFrame).active); // the rest of the kill, 3 s
    }
    const auto end = e.update(false, false, t += kFrame);
    CHECK(end.ended);
    CHECK_FALSE(end.timedOut);
}

TEST_CASE("glory episode: a long gap between game views counts as one frame's worth at most") {
    GloryEpisode e({0.5, 10.0, 0.25});
    double t = 0.0;
    e.update(true, true, t);
    CHECK(e.update(true, true, t += 1.0).active);
    CHECK(e.update(true, true, t += 60.0).active); // no game view for a minute (a pause without views)
    CHECK(e.update(true, true, t += kFrame).active);
    // Still ends at the maximum once the kill's own time adds up: 0.5 s and a frame counted so far.
    bool timedOut = false;
    int frames = 0;
    while (!timedOut && frames < 20 * 60) {
        timedOut = e.update(true, true, t += kFrame).timedOut;
        ++frames;
    }
    CHECK(timedOut);
    CHECK(frames * kFrame == doctest::Approx(10.0 - 0.5 - kFrame).epsilon(0.02));
}

TEST_CASE("glory episode: a kill shorter than the maximum ends as usual") {
    GloryEpisode e;
    double t = 0.0;
    e.update(true, true, t);
    for (int i = 0; i < 9 * 60; ++i) {
        CHECK(e.update(true, true, t += kFrame).active); // a long kill, 9 s
    }
    const auto s = e.update(false, false, t += kFrame);
    CHECK(s.ended);
    CHECK_FALSE(s.timedOut);
}

TEST_CASE("glory episode: a non-finite clock changes nothing, reset forgets the kill") {
    GloryEpisode e;
    e.update(true, true, 1.0);
    const auto s = e.update(false, false, std::numeric_limits<double>::quiet_NaN());
    CHECK(s.active);
    CHECK_FALSE(s.ended);
    e.reset();
    CHECK_FALSE(e.active());
    CHECK(e.update(true, false, 2.0).started);
}
