// The late-image wait (late_wait.hpp): its switch, the XR worker's wait and the GPU share estimate.

#include "vkcore/late_wait.hpp"

#include "features/pacing/late_image.hpp"
#include "features/pacing/slot_choice.hpp"
#include "vkcore/log.hpp"
#include "vkcore/new_frame_wait.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace evr::vkcore::late_wait {

namespace {

using Clock = std::chrono::steady_clock;

std::atomic<bool> g_on{false};
std::atomic<double> g_gpu{pacing::kLateImageGpuSeconds}; // the configured share (rig test), set once

// The XR worker's alone.
pacing::GpuShareEstimate g_estimate;
struct Waits {
    std::uint64_t got = 0;       // the image came
    std::uint64_t ranOut = 0;    // it did not: the previous image again
    std::uint64_t stepAside = 0; // the configured share would have waited, the GPU's recent need left no time
    double seconds = 0.0;
    double longest = 0.0;
    double shareSum = 0.0; // the share left, over got + ranOut + stepAside
};
Waits g_waits;

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

double seconds(Clock::duration d) {
    return std::chrono::duration<double>(d).count();
}

} // namespace

void configure(const pacing::LateWaitAsk& ask, std::string_view gpuMs) {
    g_gpu.store(pacing::lateImageGpuSeconds(gpuMs), std::memory_order_relaxed);
    const bool on = pacing::lateWaitOn(ask);
    g_on.store(on, std::memory_order_release);
    if (on) {
        EVR_LOG(
            "pace: held, a headset frame that begins while the game is still drawing its image waits for it "
            "until %.1f ms before the period ends, or less while the image's GPU work has needed more "
            "(ETERNALVR_PACE_LATE_WAIT=1)",
            (pacing::kSubmitMarginSeconds + g_gpu.load(std::memory_order_relaxed)) * 1000.0);
    } else if (const pacing::LateWaitRefusal why = pacing::lateWaitRefusal(ask);
               why != pacing::LateWaitRefusal::None) {
        EVR_LOG("pace: ETERNALVR_PACE_LATE_WAIT=1 is not used: %s", pacing::lateWaitRefusalText(why));
    }
}

bool on() {
    return g_on.load(std::memory_order_acquire);
}

void await(const Frame& frame,
           long long frameStartQpc,
           const std::atomic<std::uint64_t>& handOvers,
           std::uint64_t atFrame,
           const std::atomic<bool>& stop) {
    pacing::LateImageOffer offer;
    offer.held = true;
    offer.newImage = frame.newImage;
    offer.sinceHandOverSeconds = frame.sinceHandOverSeconds;
    offer.sinceFrameStartSeconds = qpcSecondsSince(frameStartQpc);
    offer.periodSeconds = frame.periodSeconds;
    offer.gpuSeconds = g_gpu.load(std::memory_order_relaxed);
    const double configured = pacing::lateImageWaitSeconds(offer);
    if (configured <= 0.0) {
        return;
    }
    // Where the graphics card sets the pace, its recent fence waits leave the wait less time, or none.
    offer.gpuSeconds = std::max(offer.gpuSeconds, g_estimate.seconds());
    g_waits.shareSum += offer.gpuSeconds;
    const double wait = pacing::lateImageWaitSeconds(offer);
    if (wait <= 0.0) {
        ++g_waits.stepAside;
        return;
    }
    const Clock::time_point start = Clock::now();
    const Clock::time_point deadline =
        start + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(wait));
    bool got = false;
    while (!stop.load(std::memory_order_acquire)) {
        if (handOvers.load(std::memory_order_acquire) != atFrame) {
            got = true;
            break;
        }
        const Clock::time_point now = Clock::now();
        if (now >= deadline || !new_frame_wait::waitPublished(seconds(deadline - now))) {
            break; // the wait ran out, or no event to wait on: never a spin
        }
    }
    const double took = seconds(Clock::now() - start);
    ++(got ? g_waits.got : g_waits.ranOut);
    g_waits.seconds += took;
    g_waits.longest = std::max(g_waits.longest, took);
}

void noteFenceWait(double seconds) {
    if (on()) {
        g_estimate.note(seconds);
    }
}

void logSummary() {
    const Waits w = std::exchange(g_waits, Waits{});
    const std::uint64_t waited = w.got + w.ranOut;
    if (waited + w.stepAside == 0) {
        return;
    }
    EVR_LOG("pace: last 10 s: %llu headset frame(s) waited for a late image: %llu got it, %llu ran out (the "
            "previous image again); average %.2f ms, longest %.2f ms; %llu not waited for (the GPU's recent "
            "fence waits left no time); %.2f ms left for the GPU on average",
            static_cast<unsigned long long>(waited), static_cast<unsigned long long>(w.got),
            static_cast<unsigned long long>(w.ranOut),
            waited ? w.seconds / static_cast<double>(waited) * 1000.0 : 0.0, w.longest * 1000.0,
            static_cast<unsigned long long>(w.stepAside),
            w.shareSum / static_cast<double>(waited + w.stepAside) * 1000.0);
}

} // namespace evr::vkcore::late_wait
