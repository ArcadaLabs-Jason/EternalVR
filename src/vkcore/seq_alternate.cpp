#include "vkcore/seq_alternate.hpp"

#include "vkcore/log.hpp"
#include "vkcore/window_timing.hpp"

#include <atomic>
#include <mutex>

namespace evr::vkcore::seq_alternate {

namespace {

using stereo_seq::Eye;

constexpr std::uint64_t kSummaryMicros = 10'000'000;

std::mutex g_mutex;
stereo_seq::EyeAlternator g_alternator;
stereo_seq::AlternateMode g_mode = stereo_seq::AlternateMode::On;

// Auto (under g_mutex).
stereo_seq::AdaptiveEyes g_policy;
bool g_pairDecided = false;
std::uint32_t g_pairFrame = 0;
bool g_pairInTick = false;
std::uint64_t g_lastTickMicros = 0; // the last tick's frame end (0: none yet)
bool g_lastStereo = false;
bool g_lastPaired = false;
std::uint64_t g_lastRightMicros = 0;
std::uint64_t g_summaryMicros = 0;
stereo_seq::AdaptiveEyes::Stats g_summaryStats;
std::atomic<std::int64_t> g_displayPeriodNs{0};
std::atomic<std::uint64_t> g_renders{0};

Eye decide(std::uint32_t renderFrame) {
    const Eye eye = g_alternator.eyeFor(renderFrame);
    if (!g_pairDecided || g_pairFrame != renderFrame) {
        // The pair's way is chosen once, at its eye L: a switch applies from a pair's start only.
        g_pairDecided = true;
        g_pairFrame = renderFrame;
        g_pairInTick = eye == Eye::Left && g_mode == stereo_seq::AlternateMode::Auto && g_policy.pairs();
    }
    return eye;
}

void logSwitch(const stereo_seq::AdaptiveEyes::Switch& s, std::uint32_t renderFrame) {
    if (s.toAlternate) {
        EVR_LOG("seq: adaptive eyes: alternating from the pair after render frame %u: both eyes per tick "
                "made %.1f tick(s)/s "
                "for %.2f s, below the headset's %.1f Hz (eye R's render %.2f of the rest of a tick)",
                renderFrame, s.rate, s.after, s.hz, s.share);
    } else {
        EVR_LOG(
            "seq: adaptive eyes: both eyes per tick again from the pair after render frame %u: alternating "
            "made %.1f "
            "tick(s)/s, both eyes per tick estimated at %.1f for %.2f s, above the headset's %.1f Hz (eye "
            "R's render %.2f of the rest of a tick)",
            renderFrame, s.rate, s.estimate, s.after, s.hz, s.share);
    }
}

void logSummary(std::uint64_t nowMicros) {
    const stereo_seq::AdaptiveEyes::Stats& n = g_policy.stats();
    const stereo_seq::AdaptiveEyes::Stats& l = g_summaryStats;
    const double both = n.seconds[0] - l.seconds[0];
    const double alt = n.seconds[1] - l.seconds[1];
    const auto rate = [](std::uint64_t ticks, double seconds) {
        return seconds > 0.0 ? static_cast<double>(ticks) / seconds : 0.0;
    };
    EVR_LOG(
        "seq: adaptive eyes: last %.1f s: %.1f s both eyes per tick (%.1f tick(s)/s), %.1f s alternating "
        "(%.1f tick(s)/s), %.1f s mono; %llu switch(es) to alternating, %llu back; now %s; headset %.1f "
        "Hz; both eyes per tick %s %.1f tick(s)/s; eye R's render %.2f of the rest of a tick; back to both "
        "eyes after %.1f s above %.1f Hz",
        static_cast<double>(nowMicros - g_summaryMicros) / 1e6, both, rate(n.ticks[0] - l.ticks[0], both),
        alt, rate(n.ticks[1] - l.ticks[1], alt), n.monoSeconds - l.monoSeconds,
        static_cast<unsigned long long>(n.switches[1] - l.switches[1]),
        static_cast<unsigned long long>(n.switches[0] - l.switches[0]),
        g_policy.pairs() ? "both eyes per tick" : "alternating", g_policy.displayHz(),
        g_policy.pairs() ? "measured" : "estimated", g_policy.lastEstimate(), g_policy.share(),
        g_policy.offAfter(), g_policy.displayHz() * stereo_seq::AdaptiveConfig{}.offAbove);
    g_summaryStats = n;
    g_summaryMicros = nowMicros;
}

// The tick before ends at `nowMicros` (under g_mutex).
void endTick(std::uint64_t nowMicros, std::uint32_t renderFrame) {
    const std::int64_t period = g_displayPeriodNs.load(std::memory_order_relaxed);
    if (period > 0) {
        g_policy.setDisplayPeriod(static_cast<double>(period) / 1e9);
    }
    if (g_lastTickMicros != 0 && nowMicros > g_lastTickMicros) {
        stereo_seq::TickSample sample;
        sample.seconds = static_cast<double>(nowMicros - g_lastTickMicros) / 1e6;
        sample.stereo = g_lastStereo;
        sample.pairedTick = g_lastPaired;
        sample.rightSeconds = static_cast<double>(g_lastRightMicros) / 1e6;
        if (const auto s = g_policy.onTick(sample)) {
            logSwitch(*s, renderFrame);
        }
    }
    g_lastTickMicros = nowMicros;
    if (g_summaryMicros == 0) {
        g_summaryMicros = nowMicros;
    } else if (nowMicros - g_summaryMicros >= kSummaryMicros) {
        logSummary(nowMicros);
    }
}

} // namespace

void configure(stereo_seq::AlternateMode mode) {
    std::lock_guard lock(g_mutex);
    g_mode = mode;
}

bool adaptive() {
    std::lock_guard lock(g_mutex);
    return g_mode == stereo_seq::AlternateMode::Auto;
}

Eye eyeFor(std::uint32_t renderFrame) {
    std::lock_guard lock(g_mutex);
    return decide(renderFrame);
}

bool pairInTick(std::uint32_t renderFrame) {
    std::lock_guard lock(g_mutex);
    decide(renderFrame);
    return g_pairInTick;
}

void rendered(std::uint32_t renderFrame, Eye eye, bool stereo, bool pairDone) {
    std::lock_guard lock(g_mutex);
    g_alternator.rendered(renderFrame, eye, stereo, pairDone);
    if (g_mode == stereo_seq::AlternateMode::Auto) {
        endTick(window_timing::nowMicros(), renderFrame);
        g_lastStereo = stereo;
        g_lastPaired = stereo && pairDone;
        g_lastRightMicros = 0;
    }
}

void rightRendered(bool done, std::uint64_t micros) {
    std::lock_guard lock(g_mutex);
    g_lastPaired = g_lastPaired && done;
    g_lastRightMicros = micros;
}

stereo_seq::EyeAlternator::Stats stats() {
    std::lock_guard lock(g_mutex);
    return g_alternator.stats();
}

void countRender() {
    g_renders.fetch_add(1, std::memory_order_acq_rel);
}

std::uint64_t renderIndex() {
    return g_renders.load(std::memory_order_acquire);
}

void noteDisplayPeriodNs(std::int64_t nanoseconds) {
    if (nanoseconds > 0) {
        g_displayPeriodNs.store(nanoseconds, std::memory_order_relaxed);
    }
}

} // namespace evr::vkcore::seq_alternate
