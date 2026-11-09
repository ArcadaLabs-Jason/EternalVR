#include "features/pacing/pace_policy.hpp"
#include "features/pacing/unseen_hold.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <limits>

using namespace evr::pacing;

namespace {

constexpr double kPeriod = 1.0 / 90.0;

bool is(const UnseenHold& hold, HoldLevel level, HoldReason reason) {
    return hold.level == level && hold.reason == reason;
}

bool none(const UnseenHold& hold) {
    return is(hold, HoldLevel::None, HoldReason::None);
}

// A watch whose session has had focus, as after the player first saw VR, on a system that senses the user.
ViewerWatch focused() {
    ViewerWatch w;
    w.sensesPresence(true);
    w.ready();
    w.state(SessionView::Synchronized);
    w.state(SessionView::Visible);
    w.state(SessionView::Focused);
    return w;
}

// 90 Hz headset frames for `seconds` with the game handing over `rate` images a second; `low`: the frames
// are the low hold's (HoldSwitch::pause), else the switch judges them (HoldSwitch::frame).
struct Frames {
    double t = 1.0;
    std::uint64_t handOvers = 0;
    double debt = 0.0;

    void run(HoldSwitch& sw, double seconds, double rate, bool low) {
        const int frames = static_cast<int>(seconds * 90.0 + 0.5);
        for (int i = 0; i < frames; ++i) {
            t += kPeriod;
            debt += rate * kPeriod;
            while (debt >= 1.0) {
                ++handOvers;
                debt -= 1.0;
            }
            if (low) {
                sw.pause(t, handOvers);
            } else {
                sw.frame(t, handOvers, kPeriod);
            }
        }
    }
};

} // namespace

TEST_CASE(
    "unseen hold: SYNCHRONIZED after focus holds low, VISIBLE at the display rate, FOCUSED not at all") {
    ViewerWatch w;
    w.ready();
    // Before the session's first focus nothing is held: the game's start and first load run free.
    w.state(SessionView::Synchronized);
    CHECK(none(w.hold()));
    w.state(SessionView::Visible);
    CHECK(none(w.hold()));
    w.state(SessionView::Focused);
    CHECK(none(w.hold()));
    // A runtime's menu: the game still shows behind it.
    w.state(SessionView::Visible);
    CHECK(is(w.hold(), HoldLevel::DisplayRate, HoldReason::Menu));
    // The headset asleep or off: the runtime shows none of the frames (the old rule held this at the display
    // rate too).
    w.state(SessionView::Synchronized);
    CHECK(is(w.hold(), HoldLevel::Low, HoldReason::Hidden));
    // Woken behind the runtime's menu, then back in the game.
    w.state(SessionView::Visible);
    CHECK(is(w.hold(), HoldLevel::DisplayRate, HoldReason::Menu));
    w.state(SessionView::Focused);
    CHECK(none(w.hold()));
}

TEST_CASE("unseen hold: nothing is held while the session does not run; READY forgets the focus") {
    ViewerWatch w = focused();
    w.state(SessionView::Synchronized);
    CHECK(w.hold().level == HoldLevel::Low);
    // STOPPING, IDLE, lost: the game's own window is all there is to see.
    w.state(SessionView::NotRunning);
    CHECK(none(w.hold()));
    // A new running session: SYNCHRONIZED before its own first focus is its start, not a break.
    w.ready();
    w.state(SessionView::Synchronized);
    CHECK(none(w.hold()));
    w.state(SessionView::Focused);
    w.state(SessionView::Synchronized);
    CHECK(w.hold().level == HoldLevel::Low);
}

TEST_CASE("unseen hold: the user not present holds low in every running state, once seen present") {
    ViewerWatch w = focused();
    w.presence(true); // queued when the session begins
    CHECK(none(w.hold()));
    // The headset taken off while the runtime keeps the session focused (the old rule did not hold it).
    w.presence(false);
    CHECK(is(w.hold(), HoldLevel::Low, HoldReason::Away));
    // Nobody looks at the menu either: absent wins over the menu's display rate.
    w.state(SessionView::Visible);
    CHECK(is(w.hold(), HoldLevel::Low, HoldReason::Away));
    // Put back on, with the runtime's menu still open, then back in the game.
    w.presence(true);
    CHECK(is(w.hold(), HoldLevel::DisplayRate, HoldReason::Menu));
    w.state(SessionView::Focused);
    CHECK(none(w.hold()));
    // Not running: the presence says nothing then.
    w.presence(false);
    w.state(SessionView::NotRunning);
    CHECK(none(w.hold()));
}

TEST_CASE("unseen hold: a presence never reported true in the session is not trusted") {
    ViewerWatch w = focused();
    // A sensor that reads "not present" from the start would hold a player in the headset.
    w.presence(false);
    CHECK(none(w.hold()));
    w.state(SessionView::Synchronized); // the session state still counts
    CHECK(is(w.hold(), HoldLevel::Low, HoldReason::Hidden));
    w.state(SessionView::Focused);
    w.presence(true);
    w.presence(false);
    CHECK(is(w.hold(), HoldLevel::Low, HoldReason::Away));
    // READY forgets it: the next session must report the user present again first.
    w.ready();
    w.state(SessionView::Focused);
    w.presence(false);
    CHECK(none(w.hold()));
}

TEST_CASE("unseen hold: presence counts only where the system says it senses the user") {
    ViewerWatch w = focused();
    // A runtime that offers the extension on a headset without the sensor: its events are ignored.
    w.sensesPresence(false);
    w.presence(true);
    w.presence(false);
    CHECK(none(w.hold()));
    w.state(SessionView::Synchronized); // the session state still counts
    CHECK(is(w.hold(), HoldLevel::Low, HoldReason::Hidden));
    // A sensing system: READY keeps that it senses, and forgets the last session's presence (a stale "away"
    // carried into a new session would hold a player who put the headset on in between).
    w.sensesPresence(true);
    w.ready();
    CHECK(w.sensesPresence());
    w.state(SessionView::Focused);
    w.presence(true);
    w.presence(false);
    CHECK(is(w.hold(), HoldLevel::Low, HoldReason::Away));
    w.ready();
    w.state(SessionView::Focused);
    CHECK(none(w.hold()));
}

TEST_CASE("unseen hold: held low, a headset-paced game does not also wait for a headset frame") {
    // Headset pacing, the headset's loop running and no frame begun since the last image: the pacer would
    // wait for the next one. Held low it does not: a SYNCHRONIZED session may begin none.
    FramePacer pacer(PaceMode::Headset);
    HeadsetLoop loop;
    loop.frames = 10;
    loop.lastFrameSeconds = 1.0;
    loop.periodSeconds = kPeriod;
    CHECK_FALSE(pacer.afterHandOver(loop, 1.001).wait); // the first image after frame 10 goes on
    REQUIRE(pacer.afterHandOver(loop, 1.002).wait);     // the next waits for frame 11
    CHECK_FALSE(pacer.afterHandOver(loop, 1.003, true).wait);
    CHECK(pacer.counters().unseen == 1);
    CHECK(pacer.counters().handOvers == 3);
}

TEST_CASE("unseen hold: every reason has its own text") {
    const HoldReason reasons[] = {HoldReason::None, HoldReason::Menu, HoldReason::Hidden, HoldReason::Away,
                                  HoldReason::RigVisible};
    for (const HoldReason a : reasons) {
        for (const HoldReason b : reasons) {
            CHECK((a == b) == (std::strcmp(holdReasonText(a), holdReasonText(b)) == 0));
        }
    }
}

TEST_CASE("unseen hold: the low cap gives the game 8 images a second on its own clock") {
    // Under the launcher's play threshold (SessionRates.PlayThreshold, 10 new pairs a second): a 10 s window
    // inside the hold is never taken as the game's own rate.
    CHECK(kLowImagesPerSecond < 10.0);
    UnfocusedCap cap;
    // A game that could hand over 90 images a second (11 ms of work each), held for 10 s.
    double t = 100.0;
    int images = 0;
    while (t < 110.0) {
        t += kPeriod; // the game's own work for the image
        ++images;
        t += cap.afterHandOverEvery(kLowIntervalSeconds, t);
    }
    CHECK(images >= 79);
    CHECK(images <= 81);
    // A game slower than the hold never waits.
    CHECK(cap.afterHandOverEvery(kLowIntervalSeconds, t + 0.2) == 0.0);
    // No interval (not finite, not positive): no wait.
    CHECK(cap.afterHandOverEvery(0.0, t + 0.21) == 0.0);
    CHECK(cap.afterHandOverEvery(std::numeric_limits<double>::quiet_NaN(), t + 0.22) == 0.0);
}

TEST_CASE("unseen hold: a wait ended by a change of hold counts the next interval from that moment") {
    UnfocusedCap cap;
    CHECK(cap.afterHandOverEvery(kLowIntervalSeconds, 10.0) == 0.0);
    // Held low: the next image would go on at 10.125.
    CHECK(cap.afterHandOverEvery(kLowIntervalSeconds, 10.01) == doctest::Approx(kLowIntervalSeconds - 0.01));
    // The headset shows a runtime's menu at 10.05: the wait ends there, and the display rate's interval
    // counts from it, not from the low hold's planned release.
    cap.releasedAt(10.05);
    CHECK(cap.afterHandOverEvery(UnfocusedCap::interval(kPeriod), 10.055) ==
          doctest::Approx(kPeriod - 0.005));
}

TEST_CASE("unseen hold: held low, the hold switch decides nothing and keeps its state") {
    // A game that keeps up with the headset is held; 8 images a second judged as its own rate would let it
    // go (the old rule: the switch saw the low hold's frames like any others).
    {
        HoldSwitch sw;
        Frames f;
        f.run(sw, 5.0, 90.0, false);
        REQUIRE(sw.held());
        f.run(sw, 5.0, kLowImagesPerSecond, false);
        CHECK_FALSE(sw.held());
    }
    HoldSwitch sw;
    Frames f;
    f.run(sw, 5.0, 90.0, false);
    REQUIRE(sw.held());
    f.run(sw, 60.0, kLowImagesPerSecond, true);
    CHECK(sw.held());
    CHECK(sw.switches() == 0);
    // Back from the break at its own rate: still held, nothing switched.
    f.run(sw, 10.0, 90.0, false);
    CHECK(sw.held());
    CHECK(sw.switches() == 0);
    // A game let go before the break stays let go through it (and is held again only by its own rate).
    f.run(sw, 3.0, 70.0, false);
    REQUIRE_FALSE(sw.held());
    const std::uint64_t switches = sw.switches();
    f.run(sw, 60.0, kLowImagesPerSecond, true);
    CHECK_FALSE(sw.held());
    CHECK(sw.switches() == switches);
}

TEST_CASE("unseen hold: the first window after the low hold judges the game alone") {
    HoldSwitch sw;
    Frames f;
    // One bad window (60 images a second: a third of the headset's frames repeat; the first window starts at
    // the first frame), half of the next, then the break: the run of bad windows is broken, so one more bad
    // window after it does not let the game go...
    f.run(sw, 1.5, 60.0, false);
    REQUIRE(sw.held());
    f.run(sw, 20.0, kLowImagesPerSecond, true);
    f.run(sw, 1.5, 60.0, false);
    CHECK(sw.held());
    // ... and a second one in a row does.
    f.run(sw, 1.0, 60.0, false);
    CHECK_FALSE(sw.held());
    CHECK(sw.lastChange() == HoldSwitch::Change::FellBehind);
}
