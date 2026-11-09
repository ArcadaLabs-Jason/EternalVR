#include "vkcore/frame_pacing.hpp"

#include "features/pacing/late_image.hpp"
#include "features/pacing/pace_policy.hpp"
#include "features/pacing/slot_choice.hpp"
#include "features/pacing/unseen_hold.hpp"
#include "stereo_seq/adaptive_eyes.hpp"
#include "vkcore/log.hpp"
#include "vkcore/new_frame_wait.hpp"
#include "vkcore/present_latency.hpp"
#include "vkcore/stall_watch.hpp"
#include "vkcore/view_slots.hpp"
#include "vkcore/window_timing.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <utility>

namespace evr::vkcore::frame_pacing {

namespace {

using Clock = std::chrono::steady_clock;

// An environment variable as narrow text (non-ASCII as '?'); empty when unset.
std::string narrowEnv(const wchar_t* name) {
    std::wstring value;
    std::string narrow;
    if (readEnv(name, value)) {
        for (const wchar_t c : value) {
            narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
    }
    return narrow;
}

// The seconds from a QueryPerformanceCounter reading to now.
double qpcSecondsSince(long long qpc) {
    static const double frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - qpc) / frequency;
}

double secondsNow() {
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

// Under `mutex`: the headset's loop (XR worker), the game side's decision and the counters. The game thread
// holds the mutex only around its decision and while the condition variable is not waiting, never across the
// driver's present. Allocated once and never destroyed (no static destructor at process exit, which runs
// under the loader lock; see layer_entry.cpp), so a worker left behind at shutdown can still signal it.
struct State {
    std::mutex mutex;
    std::condition_variable begun;
    pacing::HeadsetLoop loop;
    pacing::FramePacer pacer;
    pacing::Cadence cadence;
    pacing::UnfocusedCap unfocusedCap; // behind a runtime's menu, and the low hold (setBackgroundHold)
    std::condition_variable holdChanged;
    pacing::HoldSwitch hold; // headset: held, or let go to run as with new (T-194)
    // The low hold while it lasts: since when, why, and the hand-overs at its start.
    struct LowHold {
        double since = 0.0;
        pacing::HoldReason reason = pacing::HoldReason::None;
        std::uint64_t handOvers = 0;
    } low;
    // The counters at the last 10 s line.
    pacing::FramePacer::Counters lastPacer;
    pacing::Cadence::Counters lastCadence;
    std::uint64_t lastSwitches = 0;
    std::uint64_t lastHandOvers = 0;
};
State& g_state = *new State;

std::atomic<bool> g_on{false};             // the game held to the headset's frames
std::atomic<bool> g_newFrames{false};      // the headset's frames wait for a new image: new, headset let go
std::atomic<bool> g_switching{false};      // headset: HoldSwitch decides the two above
std::atomic<std::uint8_t> g_holdLevel{0};  // pacing::HoldLevel: setBackgroundHold
std::atomic<std::uint64_t> g_handOvers{0}; // images handed to the XR worker (publishSlot)
std::atomic<std::uint64_t> g_seen{0};      // the hand-overs the present hook has looked at
std::atomic<double> g_lastHandOver{0.0};   // when the last image was handed over (secondsNow)
std::atomic<bool> g_lateWait{false};       // held: a headset frame waits for a late image (late_image.hpp)
std::atomic<double> g_lastRelease{0.0};    // the present hook of the last hand-over returned (secondsNow)
std::atomic<bool> g_holdGate{false};       // HoldSwitch's latency gate (ETERNALVR_TEST_HOLD_GATE=1); set once
std::atomic<double> g_lowEnded{0.0};       // when the last low hold ended, until the line after it (0: none)
std::atomic<std::uint64_t> g_lowEndedHandOvers{0};

// The XR worker's alone: the hand-overs at the last headset frame, and the late-image waits since the last
// 10 s line.
std::uint64_t g_handOversAtFrame = 0;
struct LateWaits {
    std::uint64_t got = 0;    // the image came
    std::uint64_t ranOut = 0; // it did not: the previous image again
    double seconds = 0.0;
    double longest = 0.0;
};
LateWaits g_lateWaits;

pacing::HoldLevel holdLevel() {
    return static_cast<pacing::HoldLevel>(g_holdLevel.load(std::memory_order_acquire));
}

// Behind a runtime's menu (one image per display period) or while nobody can see the frames (the low hold):
// the render thread waits out the rest of the interval on the cap's own clock, and goes on at once when the
// hold changes (setBackgroundHold signals it). The wait is bounded by the interval, so nothing can hang on
// it; its time is left out of the stall watch's gap.
void holdBack(pacing::HoldLevel level) {
    const Clock::time_point start = Clock::now();
    {
        std::unique_lock lock(g_state.mutex);
        const double interval = level == pacing::HoldLevel::Low
                                    ? pacing::kLowIntervalSeconds
                                    : pacing::UnfocusedCap::interval(g_state.loop.periodSeconds);
        const double seconds = g_state.unfocusedCap.afterHandOverEvery(interval, secondsNow());
        if (seconds <= 0.0) {
            return;
        }
        if (g_state.holdChanged.wait_for(lock,
                                         std::chrono::microseconds(static_cast<long long>(seconds * 1e6)),
                                         [level] { return holdLevel() != level; })) {
            g_state.unfocusedCap.releasedAt(secondsNow());
        }
    }
    stall_watch::addHeld(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count()));
}

// One line about a second after a low hold ended: the images handed over since, which shows the game back at
// its own rate (the rig's unseen-hold scenario checks it).
constexpr double kAfterLowSeconds = 1.0;
void noteAfterLowHold() {
    double ended = g_lowEnded.load(std::memory_order_acquire);
    if (ended <= 0.0) {
        return;
    }
    const double now = secondsNow();
    if (now - ended < kAfterLowSeconds || !g_lowEnded.compare_exchange_strong(ended, 0.0)) {
        return;
    }
    const std::uint64_t images =
        g_handOvers.load(std::memory_order_acquire) - g_lowEndedHandOvers.load(std::memory_order_relaxed);
    EVR_LOG(
        "pace: in the %.1f s after the low hold the game handed %llu image(s) to the headset (%.1f a second)",
        now - ended, static_cast<unsigned long long>(images), static_cast<double>(images) / (now - ended));
}

constexpr std::uint64_t kSwitchLines = 20; // HoldSwitch changes logged one by one

// The game counts as in play while the camera hook's game ticks keep coming (menus and loading screens have
// none, stall_watch.hpp); without the hook the ticks tell nothing and the game counts as in play.
constexpr std::uint64_t kPlayGapMicros = 250'000;
bool inPlay() {
    if (!stall_watch::ticksJudged()) {
        return true;
    }
    const std::uint64_t last = stall_watch::lastGameTickMicros();
    return last != 0 && window_timing::nowMicros() - last < kPlayGapMicros;
}

} // namespace

bool configure() {
    static const bool on = [] {
        pacing::PaceMode mode = pacing::PaceMode::Off;
        if (const std::string text = narrowEnv(L"ETERNALVR_PACE"); !text.empty()) {
            if (const auto parsed = pacing::parsePaceMode(text)) {
                mode = *parsed;
            } else {
                EVR_LOG("pace: ETERNALVR_PACE '%s' is not off, headset or new; off", text.c_str());
            }
        }
        // Adaptive alternate eyes switches by the game's tick rate, which pacing holds at the headset's: once
        // alternating it would never measure enough headroom to render both eyes per tick again. Not with
        // Parallel Eye Rendering, which ignores alternate eyes (view_slots.hpp; installed at
        // vkCreateInstance).
        if (mode == pacing::PaceMode::Headset && !parallelEyesChangedEngine() &&
            stereo_seq::alternateMode(narrowEnv(L"ETERNALVR_ALTERNATE_EYES")) ==
                stereo_seq::AlternateMode::Auto) {
            EVR_LOG("pace: ETERNALVR_PACE=headset is not used with ETERNALVR_ALTERNATE_EYES=auto (its switch "
                    "measures the game's tick rate, which pacing holds at the headset's); off");
            mode = pacing::PaceMode::Off;
        }
        {
            std::lock_guard lock(g_state.mutex);
            g_state.pacer = pacing::FramePacer(mode);
        }
        if (mode == pacing::PaceMode::NewFrames) {
            EVR_LOG("pace: ETERNALVR_PACE=new: the game is not held; each headset frame begins only once the "
                    "game "
                    "has handed over a new image (at most %.0f display periods), so every image is shown "
                    "once and "
                    "the runtime fills the refreshes in between itself",
                    pacing::kNewFrameWaitPeriods);
        } else if (mode == pacing::PaceMode::Headset) {
            EVR_LOG(
                "pace: ETERNALVR_PACE=headset: after each image it hands to the headset (a stereo pair) the "
                "game's render thread waits until the headset begins its next frame, at most %.0f display "
                "periods; one image per headset frame. A game that falls behind (over %.0f%% of the "
                "headset's "
                "frames in a second without a new image) is let go and runs as with new until its own rate "
                "reaches the refresh rate. The pose lead is on unless ETERNALVR_POSE_LEAD=0",
                pacing::FramePacer::kTimeoutPeriods, pacing::HoldSwitch::kLetGoRepeatShare * 100.0);
        } else {
            EVR_LOG(
                "pace: off: the game renders as fast as it can and each headset frame shows the newest image "
                "(ETERNALVR_PACE=headset holds it to one per headset frame)");
        }
        g_on.store(mode == pacing::PaceMode::Headset, std::memory_order_release);
        // ETERNALVR_TEST_PACE_REPEAT=1 (a rig A/B): headset as before T-194, the game always held and the
        // newest image at every headset frame.
        const bool repeat =
            mode == pacing::PaceMode::Headset && narrowEnv(L"ETERNALVR_TEST_PACE_REPEAT") == "1";
        if (repeat) {
            EVR_LOG("pace: the game is always held and the headset's frames do not wait for a new image "
                    "(ETERNALVR_TEST_PACE_REPEAT=1, a rig test)");
        }
        g_newFrames.store(mode == pacing::PaceMode::NewFrames, std::memory_order_release);
        // ETERNALVR_TEST_LATE_WAIT=1 (a rig A/B): held, a headset frame waits for the image the game is still
        // drawing (late_image.hpp).
        const bool lateWait =
            mode == pacing::PaceMode::Headset && narrowEnv(L"ETERNALVR_TEST_LATE_WAIT") == "1";
        if (lateWait) {
            EVR_LOG("pace: held, a headset frame that begins while the game is still drawing its image waits "
                    "for it until %.1f ms before the period ends (ETERNALVR_TEST_LATE_WAIT=1, a rig test)",
                    (pacing::kSubmitMarginSeconds + pacing::kLateImageGpuSeconds) * 1000.0);
        }
        g_lateWait.store(lateWait, std::memory_order_relaxed);
        // ETERNALVR_TEST_HOLD_GATE=1 (a rig A/B): the hold switch lets a game go only when most of its images
        // took longer than a display period from their release (HoldSwitch::noteGameTime).
        const bool holdGate =
            mode == pacing::PaceMode::Headset && !repeat && narrowEnv(L"ETERNALVR_TEST_HOLD_GATE") == "1";
        g_holdGate.store(holdGate, std::memory_order_relaxed);
        if (holdGate) {
            EVR_LOG("pace: the hold lets the game go only when most of its images take longer than a display "
                    "period from their release (ETERNALVR_TEST_HOLD_GATE=1, a rig test)");
            std::lock_guard lock(g_state.mutex);
            g_state.hold.setLatencyGate(true);
        }
        g_switching.store(mode == pacing::PaceMode::Headset && !repeat, std::memory_order_release);
        return mode == pacing::PaceMode::Headset;
    }();
    return on;
}

bool newFramesOnly() {
    // Not during the low hold: a headset frame waiting for one of its few images would only slow the
    // runtime's own loop, with nobody to see the images.
    return g_newFrames.load(std::memory_order_acquire) && holdLevel() != pacing::HoldLevel::Low;
}

void onHandOver() {
    if (g_holdGate.load(std::memory_order_relaxed) || g_lateWait.load(std::memory_order_relaxed)) {
        g_lastHandOver.store(secondsNow(), std::memory_order_relaxed);
    }
    g_handOvers.fetch_add(1, std::memory_order_release);
}

std::uint64_t awaitLateImage(const std::atomic<std::uint64_t>& latest,
                             long long frameStartQpc,
                             const std::atomic<bool>& stop) {
    if (!g_lateWait.load(std::memory_order_relaxed) || !g_on.load(std::memory_order_acquire)) {
        return latest.load();
    }
    const double sinceFrameStartSeconds = qpcSecondsSince(frameStartQpc);
    pacing::LateImageOffer offer;
    offer.held = true;
    offer.newImage = g_handOvers.load(std::memory_order_acquire) != g_handOversAtFrame;
    const double start = secondsNow();
    offer.sinceHandOverSeconds = start - g_lastHandOver.load(std::memory_order_relaxed);
    offer.sinceFrameStartSeconds = sinceFrameStartSeconds;
    {
        std::lock_guard lock(g_state.mutex);
        offer.periodSeconds = g_state.loop.periodSeconds;
    }
    const double wait = pacing::lateImageWaitSeconds(offer);
    if (wait <= 0.0) {
        return latest.load();
    }
    const double deadline = start + wait;
    bool got = false;
    while (!stop.load(std::memory_order_acquire)) {
        if (g_handOvers.load(std::memory_order_acquire) != g_handOversAtFrame) {
            got = true;
            break;
        }
        const double left = deadline - secondsNow();
        if (left <= 0.0) {
            break;
        }
        new_frame_wait::waitPublished(left);
    }
    const double took = secondsNow() - start;
    ++(got ? g_lateWaits.got : g_lateWaits.ranOut);
    g_lateWaits.seconds += took;
    g_lateWaits.longest = std::max(g_lateWaits.longest, took);
    return latest.load();
}

void onHeadsetFrame(std::int64_t periodNs, long long frameStartQpc, double waitFrameMs) {
    present_latency::onChoice(frameStartQpc, waitFrameMs); // before the signal: the release follows it
    bool switched = false;
    bool held = true;
    std::uint64_t switches = 0;
    pacing::HoldSwitch::Change why = pacing::HoldSwitch::Change::None;
    double share = 0.0;
    double rate = 0.0;
    double backOff = 0.0;
    const bool playing = g_switching.load(std::memory_order_acquire) && inPlay();
    const bool low = holdLevel() == pacing::HoldLevel::Low;
    {
        std::lock_guard lock(g_state.mutex);
        const double now = secondsNow();
        const std::uint64_t handOvers = g_handOvers.load(std::memory_order_acquire);
        g_handOversAtFrame = handOvers;
        ++g_state.loop.frames;
        g_state.loop.lastFrameSeconds = now;
        g_state.loop.periodSeconds = static_cast<double>(periodNs) / 1e9;
        g_state.cadence.frame(handOvers);
        // Held low, the game's rate is the hold's: the switch decides nothing (T-228).
        if (g_switching.load(std::memory_order_acquire) && low) {
            g_state.hold.pause(now, handOvers);
        } else if (g_switching.load(std::memory_order_acquire) &&
                   g_state.hold.frame(now, handOvers, g_state.loop.periodSeconds, playing)) {
            switched = true;
            held = g_state.hold.held();
            switches = g_state.hold.switches();
            why = g_state.hold.lastChange();
            share = g_state.hold.lastRepeatShare();
            rate = g_state.hold.lastRate();
            backOff = g_state.hold.backOffSeconds();
            g_on.store(held, std::memory_order_release);
            g_newFrames.store(!held, std::memory_order_release);
        }
    }
    g_state.begun.notify_all();
    if (switched && switches <= kSwitchLines) {
        const char* last = switches == kSwitchLines ? " (the last such line; the 10 s lines count them)" : "";
        using Change = pacing::HoldSwitch::Change;
        if (why == Change::FellBehind) {
            EVR_LOG(
                "pace: the game falls behind the headset: let go, new frames only (%d windows of %.0f s in a "
                "row over %.0f%% of the headset's frames without a new image, the last %.1f%%; the game's "
                "rate %.1f images/s; it is held again once it reaches the refresh rate, %.0f s from now at "
                "the earliest)%s",
                pacing::HoldSwitch::kBadWindowsToLetGo, pacing::HoldSwitch::kWindowSeconds,
                pacing::HoldSwitch::kLetGoRepeatShare * 100.0, share * 100.0, rate, backOff, last);
        } else if (why == Change::KeptUp) {
            EVR_LOG(
                "pace: the game keeps up with the headset: held again (its own rate %.1f images/s; %.1f%% of "
                "the headset's frames without a new image; a hold that ends within %.0f s doubles the wait "
                "before the next, now %.0f s)%s",
                rate, share * 100.0, pacing::HoldSwitch::kLastingHoldSeconds, backOff, last);
        } else {
            EVR_LOG("pace: a menu or a loading screen: held again (the game is held while not in play)%s",
                    last);
        }
    }
}

void beforeEndFrame() {
    present_latency::beforeEndFrame();
}

void noteShown(std::uint64_t seq, std::int64_t lateNs) {
    std::lock_guard lock(g_state.mutex);
    g_state.cadence.shown(seq, static_cast<double>(lateNs) / 1e9);
}

bool afterPresent() {
    noteAfterLowHold();
    const pacing::HoldLevel level = holdLevel();
    if (!g_on.load(std::memory_order_acquire) && level == pacing::HoldLevel::None) {
        return false;
    }
    // Only a present that handed an image over waits (under Route S eye R's, which completes the pair; eye
    // L's present only copies its half).
    const std::uint64_t handed = g_handOvers.load(std::memory_order_acquire);
    if (g_seen.exchange(handed, std::memory_order_acq_rel) == handed) {
        return false;
    }
    // This present handed an image over: the game's own time for it ran from the previous hand-over's
    // release. Any return from here on is this hand-over's release.
    const bool gate = g_holdGate.load(std::memory_order_relaxed);
    const double gameTime =
        gate ? g_lastHandOver.load(std::memory_order_relaxed) - g_lastRelease.load(std::memory_order_relaxed)
             : 1.0;
    struct Release {
        bool on;
        ~Release() {
            if (on) {
                g_lastRelease.store(secondsNow(), std::memory_order_relaxed);
            }
        }
    } release{gate};
    if (level != pacing::HoldLevel::None) {
        holdBack(level);
    }
    if (!g_on.load(std::memory_order_acquire)) {
        return false;
    }
    std::unique_lock lock(g_state.mutex);
    if (gate && gameTime < 0.1) { // a release before a let-go stretch is not this image's
        g_state.hold.noteGameTime(gameTime);
    }
    const pacing::PaceStep step =
        g_state.pacer.afterHandOver(g_state.loop, secondsNow(), level == pacing::HoldLevel::Low);
    if (!step.wait) {
        return false;
    }
    const Clock::time_point start = Clock::now();
    const bool begun = g_state.begun.wait_for(
        lock, std::chrono::microseconds(static_cast<long long>(step.timeoutSeconds * 1e6)),
        [&step] { return g_state.loop.frames >= step.untilFrame; });
    g_state.pacer.waited(g_state.loop.frames, std::chrono::duration<double>(Clock::now() - start).count(),
                         begun);
    return begun;
}

// Rig tests. ETERNALVR_TEST_UNFOCUSED_CAP=0: a runtime's menu does not hold the game. The OpenXR simulator is
// VISIBLE whenever its window is not in front, so its rate measurements otherwise end at one image per
// period. ETERNALVR_TEST_UNSEEN_VISIBLE=1: VISIBLE after focus counts as nobody looking, so the simulator
// (which never goes SYNCHRONIZED and senses no user) drives the low hold with its window's focus; only from
// the game's first tick (in the map), so the start and the map's load run as in every other rig run.
namespace {
bool unfocusedCapOff() {
    static const bool off = narrowEnv(L"ETERNALVR_TEST_UNFOCUSED_CAP") == "0";
    return off;
}
bool unseenVisible() {
    static const bool on = narrowEnv(L"ETERNALVR_TEST_UNSEEN_VISIBLE") == "1";
    return on;
}
} // namespace

void setBackgroundHold(pacing::UnseenHold hold) {
    using pacing::HoldLevel;
    if (hold.reason == pacing::HoldReason::Menu && unseenVisible() &&
        stall_watch::lastGameTickMicros() != 0) {
        hold = {HoldLevel::Low, pacing::HoldReason::RigVisible};
    }
    if (hold.level == HoldLevel::DisplayRate && unfocusedCapOff()) {
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true, std::memory_order_relaxed)) {
            EVR_LOG("pace: a runtime menu is over the game; not held (ETERNALVR_TEST_UNFOCUSED_CAP=0, a rig "
                    "test)");
        }
        hold = {};
    }
    HoldLevel before = HoldLevel::None;
    double lowSeconds = 0.0;
    std::uint64_t lowImages = 0;
    {
        std::lock_guard lock(g_state.mutex);
        before = static_cast<HoldLevel>(
            g_holdLevel.exchange(static_cast<std::uint8_t>(hold.level), std::memory_order_acq_rel));
        if (before == hold.level) {
            g_state.low.reason = hold.reason; // the 10 s line names the reason that holds now
            return;
        }
        const double now = secondsNow();
        const std::uint64_t handOvers = g_handOvers.load(std::memory_order_acquire);
        if (before == HoldLevel::Low) {
            lowSeconds = now - g_state.low.since;
            lowImages = handOvers - g_state.low.handOvers;
            g_lowEndedHandOvers.store(handOvers, std::memory_order_relaxed);
            g_lowEnded.store(now, std::memory_order_release);
        }
        if (hold.level == HoldLevel::Low) {
            g_state.low = {now, hold.reason, handOvers};
        }
    }
    g_state.holdChanged.notify_all(); // a render thread held back goes on at once
    const char* const menu = "a runtime menu is over the game: the game is held to one image per display "
                             "period until it has focus again";
    const char* const unheld = g_on.load(std::memory_order_relaxed)
                                   ? "the game renders paced to the headset again"
                                   : "the game renders as fast as it can again";
    if (before == HoldLevel::Low) {
        EVR_LOG("pace: the low hold ends after %.1f s (%llu image(s) handed over, %.1f a second); %s",
                lowSeconds, static_cast<unsigned long long>(lowImages),
                lowSeconds > 0.0 ? static_cast<double>(lowImages) / lowSeconds : 0.0,
                hold.level == HoldLevel::DisplayRate ? menu : unheld);
    }
    if (hold.level == HoldLevel::Low) {
        EVR_LOG("pace: nobody can see the game's frames (%s): the game is held to %.0f images a second until "
                "they can again",
                pacing::holdReasonText(hold.reason), pacing::kLowImagesPerSecond);
    } else if (hold.level == HoldLevel::DisplayRate && before == HoldLevel::None) {
        EVR_LOG("pace: %s", menu);
    } else if (hold.level == HoldLevel::None && before == HoldLevel::DisplayRate) {
        EVR_LOG("pace: the menu is gone; %s", unheld);
    }
}

void logSummary() {
    pacing::FramePacer::Counters p;
    pacing::Cadence::Counters c;
    double longest = 0.0;
    std::uint64_t switches = 0;
    std::uint64_t handedOver = 0;
    double lowSeconds = -1.0; // held low: for how long so far
    pacing::HoldReason lowReason = pacing::HoldReason::None;
    {
        std::lock_guard lock(g_state.mutex);
        const pacing::FramePacer::Counters& np = g_state.pacer.counters();
        const pacing::Cadence::Counters& nc = g_state.cadence.counters();
        p.handOvers = np.handOvers - g_state.lastPacer.handOvers;
        p.waits = np.waits - g_state.lastPacer.waits;
        p.timeouts = np.timeouts - g_state.lastPacer.timeouts;
        p.waitSeconds = np.waitSeconds - g_state.lastPacer.waitSeconds;
        p.frameBegun = np.frameBegun - g_state.lastPacer.frameBegun;
        p.idle = np.idle - g_state.lastPacer.idle;
        p.unseen = np.unseen - g_state.lastPacer.unseen;
        c.frames = nc.frames - g_state.lastCadence.frames;
        c.none = nc.none - g_state.lastCadence.none;
        c.one = nc.one - g_state.lastCadence.one;
        c.several = nc.several - g_state.lastCadence.several;
        c.notShown = nc.notShown - g_state.lastCadence.notShown;
        c.handOvers = nc.handOvers - g_state.lastCadence.handOvers;
        c.shownViews = nc.shownViews - g_state.lastCadence.shownViews;
        c.lateSeconds = nc.lateSeconds - g_state.lastCadence.lateSeconds;
        g_state.lastPacer = np;
        g_state.lastCadence = nc;
        longest = g_state.pacer.takeLongestWait();
        switches = g_state.hold.switches() - g_state.lastSwitches;
        g_state.lastSwitches = g_state.hold.switches();
        const std::uint64_t handOvers = g_handOvers.load(std::memory_order_acquire);
        handedOver = handOvers - g_state.lastHandOvers;
        g_state.lastHandOvers = handOvers;
        if (holdLevel() == pacing::HoldLevel::Low) {
            lowSeconds = secondsNow() - g_state.low.since;
            lowReason = g_state.low.reason;
        }
    }
    // Also without headset frames (SYNCHRONIZED renders none).
    if (lowSeconds >= 0.0) {
        EVR_LOG("pace: held low for %.0f s so far (%s); last 10 s: %llu image(s) handed over", lowSeconds,
                pacing::holdReasonText(lowReason), static_cast<unsigned long long>(handedOver));
    }
    if (c.frames == 0) {
        return;
    }
    const double perFrame = static_cast<double>(c.handOvers) / static_cast<double>(c.frames);
    const double lateMs = c.shownViews ? c.lateSeconds / static_cast<double>(c.shownViews) * 1000.0 : 0.0;
    const bool switching = g_switching.load(std::memory_order_relaxed);
    const char* name = g_on.load(std::memory_order_relaxed) ? "headset"
                       : switching                          ? "headset, let go (new frames only)"
                                                            : "off";
    char held[64] = "";
    if (switching) {
        std::snprintf(held, sizeof held, " (the hold switched %llu time(s) in these 10 s)",
                      static_cast<unsigned long long>(switches));
    }
    EVR_LOG(
        "pace: %s%s; last 10 s: %llu headset frame(s): %llu with no new image, %llu with one, %llu with two "
        "or "
        "more (%llu image(s) never shown); %.2f image(s) per headset frame; game frames shown %.1f ms after "
        "the time their pose was predicted for, on average",
        name, held, static_cast<unsigned long long>(c.frames), static_cast<unsigned long long>(c.none),
        static_cast<unsigned long long>(c.one), static_cast<unsigned long long>(c.several),
        static_cast<unsigned long long>(c.notShown), perFrame, lateMs);
    const LateWaits late = std::exchange(g_lateWaits, LateWaits{});
    if (late.got + late.ranOut > 0) {
        EVR_LOG("pace: last 10 s: %llu headset frame(s) waited for a late image: %llu got it, %llu ran out "
                "(the previous image again); average %.2f ms, longest %.2f ms",
                static_cast<unsigned long long>(late.got + late.ranOut),
                static_cast<unsigned long long>(late.got), static_cast<unsigned long long>(late.ranOut),
                late.seconds / static_cast<double>(late.got + late.ranOut) * 1000.0, late.longest * 1000.0);
    }
    if (!g_on.load(std::memory_order_relaxed)) {
        return;
    }
    const std::uint64_t waited = p.waits + p.timeouts;
    EVR_LOG(
        "pace: last 10 s: %llu image(s) handed over; %llu wait(s) for the headset's next frame, average %.2f "
        "ms, longest %.2f ms; %llu timeout(s); %llu without a wait (a headset frame had begun), "
        "%llu with the headset not running, %llu held low",
        static_cast<unsigned long long>(p.handOvers), static_cast<unsigned long long>(waited),
        waited ? p.waitSeconds / static_cast<double>(waited) * 1000.0 : 0.0, longest * 1000.0,
        static_cast<unsigned long long>(p.timeouts), static_cast<unsigned long long>(p.frameBegun),
        static_cast<unsigned long long>(p.idle), static_cast<unsigned long long>(p.unseen));
}

} // namespace evr::vkcore::frame_pacing
