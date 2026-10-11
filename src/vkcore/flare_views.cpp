#include "vkcore/flare_views.hpp"

#include "features/flares/flare_book.hpp"
#include "stereo_seq/stereo_taa.hpp"
#include "vkcore/flare_core.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/view_slots.hpp"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-flares";

struct RouteSCounters {
    std::atomic<std::uint64_t> eyes{0};
    std::atomic<std::uint64_t> rebuilt{0};
    std::atomic<std::uint64_t> skipped{0};
    std::atomic<std::uint64_t> changed{0};
    std::atomic<std::uint64_t> odd{0};
    std::atomic<std::uint64_t> missed{0}; // an eye found only another render's records
    std::atomic<std::uint64_t> lastReport{0};
} g_routeS;

std::atomic<bool> g_loggedFirst{false};
std::atomic<bool> g_loggedMissed{false};

void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_routeS.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_routeS.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    const flare_core::RecordCounts r = flare_core::takeRecordCounts();
    const auto take = [](std::atomic<std::uint64_t>& c) {
        return static_cast<unsigned long long>(c.exchange(0));
    };
    const auto ull = [](std::uint64_t v) {
        return static_cast<unsigned long long>(v);
    };
    EVR_LOG(
        "%s: last 10 s: %llu eye(s), %llu flare(s) rebuilt from the eye's view (%llu returned early, %llu "
        "moved, %llu odd); recorded %llu (%llu twice, %llu over the cap, %llu late, %llu not written by the "
        "engine), %llu eye(s) found another render's; %llu render(s) with flares no eye took so far (mono)",
        kTag, take(g_routeS.eyes), take(g_routeS.rebuilt), take(g_routeS.skipped), take(g_routeS.changed),
        take(g_routeS.odd), ull(r.recorded), ull(r.duplicate), ull(r.full), ull(r.late), ull(r.unwritten),
        take(g_routeS.missed), ull(flare_core::rendersNotTaken()));
}

bool requested() {
    std::wstring value;
    std::string narrow;
    if (readEnv(L"ETERNALVR_FLARES_PER_EYE", value)) {
        for (const wchar_t c : value) {
            narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
    }
    return stereo_seq::switchValue(narrow, true);
}

std::once_flag g_routeSOnce;
bool g_routeSInstalled = false;

void add(std::atomic<std::uint64_t>& counter, std::uint64_t n = 1) {
    counter.fetch_add(n, std::memory_order_relaxed);
}

} // namespace

bool installFlareViewHooks() {
    std::call_once(g_routeSOnce, [] {
        if (!requested()) {
            EVR_LOG("%s: off (ETERNALVR_FLARES_PER_EYE=0): lens flares stay at the head-centred position in "
                    "both eyes",
                    kTag);
            return;
        }
        g_routeSInstalled = flare_core::install(
            kTag, "not installed: lens flares stay at the head-centred position in both eyes",
            ": each Route S eye's lens flares are built again from that eye's view after its latch "
            "(ETERNALVR_FLARES_PER_EYE=0 turns it off)");
    });
    return g_routeSInstalled;
}

void rebuildFlaresForEye(std::byte* renderView) {
    if (renderView == nullptr || !g_routeSInstalled || viewSlotsActive() || !flare_core::live() ||
        !mp_guard::allowsGameTouch()) {
        return;
    }
    flares::RenderKey key;
    if (!flare_core::renderKeyOf(reinterpret_cast<std::uintptr_t>(renderView), key)) {
        return;
    }
    const flare_core::HalfResult r = flare_core::rebuild(renderView, key, 0, 0, nullptr);
    if (r.fault) {
        return;
    }
    if (r.taken == 0) {
        if (r.otherRender) {
            add(g_routeS.missed);
            if (!g_loggedMissed.exchange(true)) {
                EVR_LOG(
                    "%s: an eye's latch (render view %p, world frame %u) found the flares of another render "
                    "only; that eye keeps the engine's flares",
                    kTag, static_cast<void*>(renderView), key.frame);
            }
        }
        report();
        return;
    }
    add(g_routeS.eyes);
    add(g_routeS.rebuilt, r.done);
    add(g_routeS.skipped, r.skipped);
    add(g_routeS.changed, r.changed);
    add(g_routeS.odd, r.odd);
    if (r.done > 0 && !g_loggedFirst.exchange(true)) {
        EVR_LOG(
            "%s: first eye with its own flares: %zu of %zu flare(s) rebuilt from render view %p (world frame "
            "%u)",
            kTag, r.done, r.taken, static_cast<void*>(renderView), key.frame);
    }
    report();
}

} // namespace evr::vkcore
