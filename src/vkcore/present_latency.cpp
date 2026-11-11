// Release-to-present timing (present_latency.hpp): the glue between the layer's threads and
// pacing::PresentLatency, the clock, and the 10 s lines.

#include "vkcore/present_latency.hpp"

#include "features/pacing/present_latency.hpp"
#include "gpu_timing/stats.hpp"
#include "vkcore/cpu_timing.hpp"
#include "vkcore/hook_guard.hpp"
#include "vkcore/log.hpp"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cwchar>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace evr::vkcore::present_latency {

namespace {

// Allocated once and never destroyed (no static destructor at process exit; see layer_entry.cpp).
struct State {
    std::mutex mutex;
    pacing::PresentLatency latency;
    bool csvWanted = false;
    bool csvTried = false;
    std::FILE* csv = nullptr; // the XR worker's alone
};

State& state() {
    static State& s = *[] {
        auto* made = new State;
        std::wstring value;
        made->csvWanted = readEnv(L"ETERNALVR_TEST_LATENCY_CSV", value) && value == L"1";
        made->latency.keepRecords(made->csvWanted);
        return made;
    }();
    return s;
}

double secondsOf(long long qpc) {
    static const double frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    return static_cast<double>(qpc) / frequency;
}

double now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return secondsOf(t.QuadPart);
}

// "mean/p50/p95/p99/max" in ms, or "-" for no samples.
std::string spread(const std::vector<double>& samples) {
    if (samples.empty()) {
        return "-";
    }
    const gpu_timing::Summary s = gpu_timing::summarize(samples);
    char text[96];
    std::snprintf(text, sizeof text, "%.2f/%.2f/%.2f/%.2f/%.2f", s.mean, s.p50, s.p95, s.p99, s.max);
    return text;
}

// The low end, "min/p1/p5/p50" in ms (the slack before a deadline), or "-".
std::string lowEnd(const std::vector<double>& samples) {
    if (samples.empty()) {
        return "-";
    }
    std::vector<double> negated;
    negated.reserve(samples.size());
    for (const double v : samples) {
        negated.push_back(-v);
    }
    const gpu_timing::Summary s = gpu_timing::summarize(std::move(negated));
    char text[96];
    std::snprintf(text, sizeof text, "%.2f/%.2f/%.2f/%.2f", -s.max, -s.p99, -s.p95, -s.p50);
    return text;
}

// The game's present hook calls in after its copy's submit and after the present, where nothing may throw: an
// image without the memory for its record is left out of the statistics.
HookGuard g_handOverGuard("the latency record of a hand-over");
HookGuard g_presentGuard("the latency record of a present");

double cell(double t) {
    return std::isnan(t) ? -1.0 : t;
}

void writeCsv(State& s, const std::vector<pacing::LatencyRecord>& records) {
    if (!s.csvTried) {
        s.csvTried = true;
        const std::wstring dir = logDirectory();
        if (!dir.empty()) {
            wchar_t name[96];
            swprintf_s(name, L"\\eternalvr-latency-%lu.csv", GetCurrentProcessId());
            s.csv = _wfsopen((dir + name).c_str(), L"w", _SH_DENYWR);
        }
        if (s.csv) {
            std::fputs(
                "log_s,value,released,held,held_choice,first_present,hand_over,present_returned,deadline,"
                "frames_at_release,frames_at_hand_over,shown_frame,wait_called,wait_returned,chosen,"
                "taken,copied,end_called\n",
                s.csv);
        }
    }
    if (!s.csv) {
        return;
    }
    const double logged = logSeconds(); // the 10 s line these images end in
    for (const pacing::LatencyRecord& r : records) {
        std::fprintf(
            s.csv,
            "%.3f,%llu,%.6f,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%llu,%llu,%llu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
            logged, static_cast<unsigned long long>(r.value), cell(r.released), r.held ? 1 : 0,
            cell(r.heldChoice), cell(r.firstPresent), cell(r.handOver), cell(r.presentReturned),
            cell(r.deadline), static_cast<unsigned long long>(r.framesAtRelease),
            static_cast<unsigned long long>(r.framesAtHandOver),
            static_cast<unsigned long long>(r.shownFrame), cell(r.waitCalled), cell(r.waitReturned),
            cell(r.chosen), cell(r.taken), cell(r.copied), cell(r.endCalled));
    }
    std::fflush(s.csv);
}

} // namespace

bool enabled() {
    static const bool on = cpu_timing::enabled();
    return on;
}

void onHandOver(std::uint64_t value) {
    if (!enabled()) {
        return;
    }
    g_handOverGuard.run([value] {
        State& s = state();
        const double t = now();
        std::lock_guard lock(s.mutex);
        s.latency.handOver(value, t);
    });
}

void afterPresent(bool held) {
    if (!enabled()) {
        return;
    }
    g_presentGuard.run([held] {
        State& s = state();
        const double t = now();
        std::lock_guard lock(s.mutex);
        s.latency.presentReturned(t, held);
    });
}

void onChoice(long long waitReturnedQpc, double waitFrameMs) {
    if (!enabled()) {
        return;
    }
    State& s = state();
    const double t = now();
    const double returned = secondsOf(waitReturnedQpc);
    std::lock_guard lock(s.mutex);
    s.latency.frameChosen(returned - waitFrameMs / 1000.0, returned, t);
}

void onTaken(std::uint64_t value) {
    if (!enabled()) {
        return;
    }
    State& s = state();
    const double t = now();
    std::lock_guard lock(s.mutex);
    s.latency.slotTaken(value, t);
}

void onCopied(std::uint64_t value) {
    if (!enabled()) {
        return;
    }
    State& s = state();
    const double t = now();
    std::lock_guard lock(s.mutex);
    s.latency.copied(value, t);
}

void beforeEndFrame() {
    if (!enabled()) {
        return;
    }
    State& s = state();
    const double t = now();
    std::lock_guard lock(s.mutex);
    s.latency.endCalled(t);
}

void logSummary() {
    if (!enabled()) {
        return;
    }
    State& s = state();
    pacing::LatencyWindow w;
    {
        std::lock_guard lock(s.mutex);
        w = s.latency.take();
    }
    if (w.handOvers == 0) {
        return;
    }
    EVR_LOG(
        "latency: game, last 10 s (ms mean/p50/p95/p99/max): %llu image(s) handed over; release to the "
        "hand-over %s (to the first present %s, from it to the hand-over %s); hand-over to the present "
        "hook's return (the present and the hold) %s; %llu released by a headset frame: its choice to the "
        "release %s, the next choice less the hand-over (min/p1/p5/p50) %s, %llu late",
        static_cast<unsigned long long>(w.handOvers), spread(w.releaseToHandOver).c_str(),
        spread(w.releaseToFirstPresent).c_str(), spread(w.firstPresentToHandOver).c_str(),
        spread(w.handOverToReturn).c_str(), static_cast<unsigned long long>(w.heldReleases),
        spread(w.wake).c_str(), lowEnd(w.slack).c_str(), static_cast<unsigned long long>(w.late));
    EVR_LOG(
        "latency: headset, last 10 s (ms mean/p50/p95/p99/max): %llu image(s) shown, %llu by the first "
        "headset frame after the hand-over, %llu later; %llu never shown, %llu not followed; hand-over to "
        "xrEndFrame %s: "
        "before xrWaitFrame %s, in it %s, to the choice %s, the GPU fence %s, the copy %s, to xrEndFrame "
        "%s; between two frames' choices %s",
        static_cast<unsigned long long>(w.shownFirst + w.shownLater),
        static_cast<unsigned long long>(w.shownFirst), static_cast<unsigned long long>(w.shownLater),
        static_cast<unsigned long long>(w.neverShown), static_cast<unsigned long long>(w.unfollowed),
        spread(w.handOverToEnd).c_str(), spread(w.beforeWaitFrame).c_str(), spread(w.inWaitFrame).c_str(),
        spread(w.beginToChoice).c_str(), spread(w.choiceToTaken).c_str(), spread(w.takenToCopied).c_str(),
        spread(w.copiedToEnd).c_str(), spread(w.choiceInterval).c_str());
    if (s.csvWanted) {
        writeCsv(s, w.records);
    }
}

} // namespace evr::vkcore::present_latency
