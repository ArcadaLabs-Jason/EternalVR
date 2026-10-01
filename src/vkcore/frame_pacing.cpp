#include "vkcore/frame_pacing.hpp"

#include "features/pacing/pace_policy.hpp"
#include "stereo_seq/adaptive_eyes.hpp"
#include "vkcore/log.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

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
    // The counters at the last 10 s line.
    pacing::FramePacer::Counters lastPacer;
    pacing::Cadence::Counters lastCadence;
};
State& g_state = *new State;

std::atomic<bool> g_on{false};
std::atomic<std::uint64_t> g_handOvers{0}; // images handed to the XR worker (publishSlot)
std::atomic<std::uint64_t> g_seen{0};      // the hand-overs the present hook has looked at

} // namespace

bool configure() {
    static const bool on = [] {
        pacing::PaceMode mode = pacing::PaceMode::Off;
        if (const std::string text = narrowEnv(L"ETERNALVR_PACE"); !text.empty()) {
            if (const auto parsed = pacing::parsePaceMode(text)) {
                mode = *parsed;
            } else {
                EVR_LOG("pace: ETERNALVR_PACE '%s' is not off or headset; off", text.c_str());
            }
        }
        // Adaptive alternate eyes switches by the game's tick rate, which pacing holds at the headset's: once
        // alternating it would never measure enough headroom to render both eyes per tick again.
        if (mode == pacing::PaceMode::Headset &&
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
        if (mode == pacing::PaceMode::Headset) {
            EVR_LOG(
                "pace: ETERNALVR_PACE=headset: after each image it hands to the headset (a stereo pair) the "
                "game's render thread waits until the headset begins its next frame, at most %.0f display "
                "periods; one image per headset frame. The pose lead is on unless ETERNALVR_POSE_LEAD=0",
                pacing::FramePacer::kTimeoutPeriods);
        } else {
            EVR_LOG(
                "pace: off: the game renders as fast as it can and each headset frame shows the newest image "
                "(ETERNALVR_PACE=headset holds it to one per headset frame)");
        }
        g_on.store(mode == pacing::PaceMode::Headset, std::memory_order_release);
        return mode == pacing::PaceMode::Headset;
    }();
    return on;
}

void onHandOver() {
    g_handOvers.fetch_add(1, std::memory_order_release);
}

void onHeadsetFrame(std::int64_t periodNs) {
    {
        std::lock_guard lock(g_state.mutex);
        ++g_state.loop.frames;
        g_state.loop.lastFrameSeconds = secondsNow();
        g_state.loop.periodSeconds = static_cast<double>(periodNs) / 1e9;
        g_state.cadence.frame(g_handOvers.load(std::memory_order_acquire));
    }
    g_state.begun.notify_all();
}

void noteShown(std::uint64_t seq, std::int64_t lateNs) {
    std::lock_guard lock(g_state.mutex);
    g_state.cadence.shown(seq, static_cast<double>(lateNs) / 1e9);
}

void afterPresent() {
    if (!g_on.load(std::memory_order_acquire)) {
        return;
    }
    // Only a present that handed an image over waits (under Route S eye R's, which completes the pair; eye
    // L's present only copies its half).
    const std::uint64_t handed = g_handOvers.load(std::memory_order_acquire);
    if (g_seen.exchange(handed, std::memory_order_acq_rel) == handed) {
        return;
    }
    std::unique_lock lock(g_state.mutex);
    const pacing::PaceStep step = g_state.pacer.afterHandOver(g_state.loop, secondsNow());
    if (!step.wait) {
        return;
    }
    const Clock::time_point start = Clock::now();
    const bool begun = g_state.begun.wait_for(
        lock, std::chrono::microseconds(static_cast<long long>(step.timeoutSeconds * 1e6)),
        [&step] { return g_state.loop.frames >= step.untilFrame; });
    g_state.pacer.waited(g_state.loop.frames, std::chrono::duration<double>(Clock::now() - start).count(),
                         begun);
}

void logSummary() {
    pacing::FramePacer::Counters p;
    pacing::Cadence::Counters c;
    double longest = 0.0;
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
    }
    if (c.frames == 0) {
        return;
    }
    const double perFrame = static_cast<double>(c.handOvers) / static_cast<double>(c.frames);
    const double lateMs = c.shownViews ? c.lateSeconds / static_cast<double>(c.shownViews) * 1000.0 : 0.0;
    EVR_LOG(
        "pace: %s; last 10 s: %llu headset frame(s): %llu with no new image, %llu with one, %llu with two or "
        "more (%llu image(s) never shown); %.2f image(s) per headset frame; game frames shown %.1f ms after "
        "the time their pose was predicted for, on average",
        g_on.load(std::memory_order_relaxed) ? "headset" : "off", static_cast<unsigned long long>(c.frames),
        static_cast<unsigned long long>(c.none), static_cast<unsigned long long>(c.one),
        static_cast<unsigned long long>(c.several), static_cast<unsigned long long>(c.notShown), perFrame,
        lateMs);
    if (!g_on.load(std::memory_order_relaxed)) {
        return;
    }
    const std::uint64_t waited = p.waits + p.timeouts;
    EVR_LOG(
        "pace: last 10 s: %llu image(s) handed over; %llu wait(s) for the headset's next frame, average %.2f "
        "ms, longest %.2f ms; %llu timeout(s); %llu without a wait (a headset frame had begun), "
        "%llu with the headset not running",
        static_cast<unsigned long long>(p.handOvers), static_cast<unsigned long long>(waited),
        waited ? p.waitSeconds / static_cast<double>(waited) * 1000.0 : 0.0, longest * 1000.0,
        static_cast<unsigned long long>(p.timeouts), static_cast<unsigned long long>(p.frameBegun),
        static_cast<unsigned long long>(p.idle));
}

} // namespace evr::vkcore::frame_pacing
