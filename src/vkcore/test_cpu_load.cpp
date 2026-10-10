#include "vkcore/test_cpu_load.hpp"

#include "stereo_seq/adaptive_eyes.hpp"
#include "vkcore/log.hpp"
#include "vkcore/window_timing.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace evr::vkcore::test_cpu_load {

namespace {

std::once_flag g_once;
std::atomic<bool> g_on{false};
stereo_seq::CpuLoadSpec g_spec;
std::uint64_t g_startMicros = 0;

// `where`: the place the load runs, for the line.
void read(const char* where) {
    std::wstring value;
    if (!readEnv(L"ETERNALVR_TEST_CPU_LOAD_MS", value) || value.empty()) {
        return;
    }
    std::string narrow;
    for (const wchar_t c : value) {
        narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    const std::optional<stereo_seq::CpuLoadSpec> spec = stereo_seq::parseCpuLoad(narrow);
    if (!spec) {
        EVR_LOG("test: ETERNALVR_TEST_CPU_LOAD_MS is not <ms>[,<seconds on>,<seconds off>] (ms 0 to 100); no "
                "load");
        return;
    }
    g_spec = *spec;
    g_startMicros = window_timing::nowMicros();
    g_on.store(true, std::memory_order_release);
    EVR_LOG("test: CPU load %.2f ms at every render's %s%s (ETERNALVR_TEST_CPU_LOAD_MS, a test knob)",
            g_spec.ms, where,
            g_spec.onSeconds > 0.0 && g_spec.offSeconds > 0.0
                ? (", on for " + std::to_string(g_spec.onSeconds) + " s, off for " +
                   std::to_string(g_spec.offSeconds) + " s, repeating")
                      .c_str()
                : "");
}

void load(const char* where) {
    std::call_once(g_once, read, where);
    if (!g_on.load(std::memory_order_acquire)) {
        return;
    }
    const std::uint64_t start = window_timing::nowMicros();
    const double ms = stereo_seq::cpuLoadMsAt(g_spec, static_cast<double>(start - g_startMicros) / 1e6);
    const auto until = start + static_cast<std::uint64_t>(ms * 1000.0);
    while (window_timing::nowMicros() < until) {
        // Busy: a processor that is slower, not one that sleeps.
    }
}

} // namespace

void atFrameEnd() {
    load("frame end");
}

void atViewDispatch() {
    load("view dispatch (Parallel Eye Rendering)");
}

} // namespace evr::vkcore::test_cpu_load
