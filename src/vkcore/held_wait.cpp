#include "vkcore/held_wait.hpp"

#include "features/pacing/held_spin.hpp"
#include "vkcore/log.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdlib>

namespace evr::vkcore::held_wait {

namespace {

using Clock = std::chrono::steady_clock;
using pacing::HeldSpin;

std::atomic<HeldSpin> g_mode{HeldSpin::OnceBehind};          // set once (configure)
std::atomic<double> g_spinSeconds{pacing::kHeldSpinSeconds}; // set once (configure)
std::atomic<bool> g_on{false};                  // the spin runs: Always, or OnceBehind after the first fall
std::atomic<std::uint64_t> g_frames{0};         // HeadsetLoop::frames (frameBegun)
std::atomic<std::uint64_t> g_sleeps{0};         // timer sleeps before a spin since the last 10 s line
std::atomic<std::uint64_t> g_wakeLateMicros{0}; // ... their wake past the time asked for
std::atomic<std::uint64_t> g_spunMicros{0};     // spinning since the last 10 s line

// frame_pacing's clock: HeadsetLoop::lastFrameSeconds is steady_clock seconds.
double secondsNow() {
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

std::chrono::microseconds micros(double seconds) {
    return std::chrono::microseconds(static_cast<long long>(seconds * 1e6));
}

// The thread's timer for the sleep before the spin: high-resolution (a condition variable's timed wait wakes
// in whole timer ticks, often past the frame itself, so the spin would not run: rig, 1.6 ms late on average,
// the timer 0.3 ms), else a plain one, else none (the caller sleeps on the condition variable). One per
// thread that holds (the game presents from several), for the process's life; the first one logs what it got.
HANDLE threadTimer() {
    thread_local const HANDLE timer = [] {
        HANDLE t =
            CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        const char* kind = "a high-resolution timer";
        if (!t) {
            t = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
            kind = t ? "a plain timer (no high-resolution one)" : "the condition variable (no timer)";
        }
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true)) {
            EVR_LOG("pace: the held wait sleeps before its spin on %s", kind);
        }
        return t;
    }();
    return timer;
}

// Sleeps `seconds` on `timer`; false when the timer could not be set.
bool sleepOn(HANDLE timer, double seconds) {
    LARGE_INTEGER due{};
    due.QuadPart = -static_cast<LONGLONG>(seconds * 1e7); // relative, 100 ns units
    if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
        return false;
    }
    const double wakeAt = secondsNow() + seconds;
    WaitForSingleObject(timer, static_cast<DWORD>(seconds * 1000.0) + 50);
    g_sleeps.fetch_add(1, std::memory_order_relaxed);
    const double late = secondsNow() - wakeAt;
    g_wakeLateMicros.fetch_add(late > 0.0 ? static_cast<std::uint64_t>(late * 1e6) : 0,
                               std::memory_order_relaxed);
    return true;
}

// Spins until `untilFrame` begins, at most `seconds`, giving the core up at each turn (SwitchToThread:
// another ready thread on this core runs, the game's workers or the XR worker that ends the spin). Returns
// whether the frame began.
bool spin(std::uint64_t untilFrame, double seconds) {
    const Clock::time_point start = Clock::now();
    const Clock::time_point until = start + micros(seconds);
    bool begun = false;
    while (!(begun = g_frames.load(std::memory_order_acquire) >= untilFrame) && Clock::now() < until) {
        YieldProcessor();
        SwitchToThread();
    }
    g_spunMicros.fetch_add(
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count()),
        std::memory_order_relaxed);
    return begun;
}

} // namespace

void configure(bool headset, const std::string& spin, const std::string& spinMs) {
    if (!headset) {
        return;
    }
    if (spin == "0") {
        g_mode.store(HeldSpin::Sleep, std::memory_order_relaxed);
        EVR_LOG("pace: held, the render thread only sleeps until the headset's next frame "
                "(ETERNALVR_TEST_HOLD_SPIN=0, a rig test)");
        return;
    }
    if (spin == "1") {
        g_mode.store(HeldSpin::Always, std::memory_order_relaxed);
        g_spinSeconds.store(0.0, std::memory_order_relaxed);
        g_on.store(true, std::memory_order_relaxed);
        EVR_LOG("pace: held, the render thread spins through its whole wait for the headset's frame in play "
                "(ETERNALVR_TEST_HOLD_SPIN=1, a rig test)");
        return;
    }
    if (spinMs.empty()) {
        return;
    }
    char* end = nullptr;
    const double ms = std::strtod(spinMs.c_str(), &end);
    if (end == spinMs.c_str() + spinMs.size() && ms > 0.0 && ms < 20.0) {
        g_spinSeconds.store(ms / 1000.0, std::memory_order_relaxed);
        EVR_LOG(
            "pace: held, the spin before the headset's frame is %.1f ms (ETERNALVR_TEST_HOLD_SPIN_MS, a rig "
            "test)",
            ms);
    } else {
        EVR_LOG(
            "pace: ETERNALVR_TEST_HOLD_SPIN_MS '%s' is not above 0 and under 20 ms; the spin stays %.1f ms",
            spinMs.c_str(), pacing::kHeldSpinSeconds * 1000.0);
    }
}

void frameBegun(std::uint64_t frames) {
    g_frames.store(frames, std::memory_order_release);
}

bool wait(std::unique_lock<std::mutex>& lock,
          std::condition_variable& begun,
          const pacing::HeadsetLoop& loop,
          const pacing::PaceStep& step,
          bool fellBehind,
          bool low,
          bool inPlay) {
    pacing::HeldWaitInput in;
    in.mode = g_mode.load(std::memory_order_relaxed);
    in.spinSeconds = g_spinSeconds.load(std::memory_order_relaxed);
    in.fellBehind = fellBehind;
    in.low = low;
    in.inPlay = inPlay;
    in.loop = loop;
    in.step = step;
    in.nowSeconds = secondsNow();
    const pacing::HeldWaitPlan plan = pacing::planHeldWait(in);
    const auto frameBegunYet = [&loop, &step] {
        return loop.frames >= step.untilFrame;
    };
    const auto sleepOnCondition = [&](double seconds) {
        return begun.wait_for(lock, micros(seconds), frameBegunYet);
    };
    if (!plan.spin) {
        return sleepOnCondition(plan.sleepSeconds);
    }
    if (!g_on.exchange(true, std::memory_order_relaxed)) {
        EVR_LOG(
            "pace: a hold fell behind; from now on, in play, the held render thread spins the last %.1f ms "
            "before each headset frame instead of sleeping, so the CPU's clock stays up while the game is "
            "held "
            "(T-232)",
            in.spinSeconds * 1000.0);
    }
    const Clock::time_point end = Clock::now() + micros(in.step.timeoutSeconds);
    if (plan.sleepSeconds > 0.0) {
        const HANDLE timer = threadTimer();
        bool slept = false;
        if (timer) {
            lock.unlock();
            slept = sleepOn(timer, plan.sleepSeconds);
            lock.lock();
        }
        if (!slept && sleepOnCondition(plan.sleepSeconds)) {
            return true;
        }
    }
    if (frameBegunYet()) {
        return true;
    }
    lock.unlock();
    const bool spun = spin(step.untilFrame, plan.spinLimitSeconds);
    lock.lock();
    if (spun || frameBegunYet()) {
        return true;
    }
    // The frame is late or stalled: the rest of the timeout sleeps.
    const double left = std::chrono::duration<double>(end - Clock::now()).count();
    return left > 0.0 && sleepOnCondition(left);
}

void logSummary() {
    if (!g_on.load(std::memory_order_relaxed)) {
        return;
    }
    const double spun = static_cast<double>(g_spunMicros.exchange(0, std::memory_order_relaxed)) / 1e6;
    const std::uint64_t sleeps = g_sleeps.exchange(0, std::memory_order_relaxed);
    const double lateMs = static_cast<double>(g_wakeLateMicros.exchange(0, std::memory_order_relaxed)) / 1e3;
    EVR_LOG(
        "pace: last 10 s: the hold spun %.0f ms in all (%.1f%% of one core); %llu sleep(s) before it, woken "
        "%.2f ms late on average",
        spun * 1000.0, spun / 10.0 * 100.0, static_cast<unsigned long long>(sleeps),
        sleeps ? lateMs / static_cast<double>(sleeps) : 0.0);
}

} // namespace evr::vkcore::held_wait
