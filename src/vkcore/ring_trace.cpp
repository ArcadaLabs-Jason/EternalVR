#include "vkcore/ring_trace.hpp"

#include "vkcore/log.hpp"
#include "vkcore/seq_hooks.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "ring-trace";
constexpr const char* kSiteNames[] = {"upload", "cur-joints", "cur-matrices", "prev-joints", "prev-matrices"};

struct Event {
    RingSite site = RingSite::Upload;
    std::uint8_t chainEye = 0;
    std::uint8_t tagEye = 0xFF;      // the render's own tag (backend frame counter + 1); 0xFF: none
    std::uint8_t inFlightEye = 0xFF; // the tag in flight (seqTagInFlight)
    std::uint32_t engine = 0;
    std::uint32_t given = 0;
    std::uint64_t tagTick = 0;
    std::uint32_t tagRenderFrame = 0;
    std::uint32_t tagBackendFrame = 0;
    std::uint32_t thread = 0;
    std::int64_t qpc = 0;
};

constexpr std::uint64_t kSkip = 20000;
constexpr std::size_t kRecord = 600;

std::once_flag g_once;
std::atomic<bool> g_on{false};
std::atomic<std::uint64_t> g_seen{0};
std::array<Event, kRecord> g_events;
std::atomic<std::size_t> g_written{0};

char eyeLetter(std::uint8_t eye) {
    switch (eye) {
    case static_cast<std::uint8_t>(stereo_seq::Eye::Left):
        return 'L';
    case static_cast<std::uint8_t>(stereo_seq::Eye::Right):
        return 'R';
    case static_cast<std::uint8_t>(stereo_seq::Eye::Mono):
        return 'M';
    default:
        return '-';
    }
}

void logAll() {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    const std::int64_t start = g_events[0].qpc;
    EVR_LOG("%s: %zu events (site, engine counter, given, given %% 3, chain eye, in-flight eye, tag eye, tag "
            "tick, tag "
            "render frame, tag backend frame, thread, ms)",
            kTag, kRecord);
    for (const Event& e : g_events) {
        const double ms =
            static_cast<double>(e.qpc - start) * 1000.0 / static_cast<double>(frequency.QuadPart);
        EVR_LOG("%s: %-13s %u %u %u %c %c %c %llu %u %u %u %.3f", kTag, kSiteNames[static_cast<int>(e.site)],
                e.engine, e.given, e.given % 3, eyeLetter(e.chainEye), eyeLetter(e.inFlightEye),
                eyeLetter(e.tagEye), static_cast<unsigned long long>(e.tagTick), e.tagRenderFrame,
                e.tagBackendFrame, e.thread, ms);
    }
}

} // namespace

void initRingTrace() {
    std::call_once(g_once, [] {
        std::wstring value;
        if (readEnv(L"ETERNALVR_STEREO_RING_TRACE", value) && value == L"1") {
            g_on.store(true, std::memory_order_release);
            EVR_LOG("%s: on: %zu ring events are logged after the first %llu", kTag, kRecord,
                    static_cast<unsigned long long>(kSkip));
        }
    });
}

void ringTraceRecord(RingSite site, std::uint32_t engine, std::uint32_t given) {
    if (!g_on.load(std::memory_order_relaxed) || g_seen.fetch_add(1, std::memory_order_relaxed) < kSkip) {
        return;
    }
    const std::size_t slot = g_written.fetch_add(1, std::memory_order_acq_rel);
    if (slot >= kRecord) {
        return;
    }
    Event e;
    e.site = site;
    e.engine = engine;
    e.given = given;
    e.chainEye = static_cast<std::uint8_t>(seqChainEye());
    if (const auto inFlight = seqTagInFlight()) {
        e.inFlightEye = static_cast<std::uint8_t>(inFlight->eye);
    }
    if (const auto tag = seqTagForBackendFrame(engine + 1u)) {
        e.tagEye = static_cast<std::uint8_t>(tag->eye);
        e.tagTick = tag->tick;
        e.tagRenderFrame = tag->renderFrame;
        e.tagBackendFrame = tag->backendFrame;
    }
    e.thread = GetCurrentThreadId();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    e.qpc = now.QuadPart;
    g_events[slot] = e;
    if (slot == kRecord - 1) {
        logAll();
    }
}

} // namespace evr::vkcore
