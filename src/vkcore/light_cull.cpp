// A second opinion from the view frustum for the lights Umbra hides (light_cull.hpp).

#include "vkcore/light_cull.hpp"

#include "vkcore/game_code.hpp"
#include "vkcore/light_cull_builds.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

namespace evr::vkcore {
namespace {

constexpr const char* kTag = "light-cull";
constexpr std::size_t kSettings = 0x18;     // rdi: the gather's context; [rdi+18h] its settings
constexpr std::size_t kSkipLightCpu = 0x2B; // settings: r_skipLightCPUCulling
static_assert(light_cull::everyRowChecked(), "every offset a build row uses has a byte check");
static_assert(light_cull::timestampsUnique(), "no two build rows share a PE timestamp");

enum Gate { NoGate = 0, GateA, GateB, GateSphere, Gates };

// Written once at install, before any hook can fire.
std::uintptr_t g_frustum = 0;
std::uintptr_t g_keep = 0;
std::uintptr_t g_drop[Gates] = {}; // per gate, where its own drop goes
std::once_flag g_once;
bool g_installed = false;
// Set once every hook is in: until then the hooks keep the engine's own verdicts.
std::atomic<bool> g_live{false};

// The gate whose drop the light in flight on this thread is getting a frustum test for (NoGate: the engine's
// own frustum path, with Umbra off). The gather runs one light at a time on a job thread.
thread_local int t_asking = NoGate;

// Counted on the gather jobs' threads for the periodic line: relaxed, tried once per 256 lights tested.
struct Counters {
    std::atomic<std::uint64_t> kept[Gates] = {}; // dropped by the gate, kept by the frustum test
    std::atomic<std::uint64_t> dropped{0};       // dropped by both
    std::atomic<std::uint64_t> tested{0};
    std::atomic<std::uint64_t> lastReport{0};
} g_counters;

void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counters.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_counters.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    const auto take = [](std::atomic<std::uint64_t>& c) {
        return static_cast<unsigned long long>(c.exchange(0));
    };
    const unsigned long long a = take(g_counters.kept[GateA]);
    const unsigned long long b = take(g_counters.kept[GateB]);
    const unsigned long long s = take(g_counters.kept[GateSphere]);
    EVR_LOG("%s: lights Umbra hid that the view frustum kept: %llu by the visible-light set, %llu by the "
            "occlusion buffer, %llu by the sphere test; %llu hidden by both",
            kTag, a, b, s, take(g_counters.dropped));
}

// At one of Umbra's drops: a light Umbra hides goes to the frustum test, as with Umbra off, and is dropped
// only when that test drops it too.
void onGate(HookRegisters& r, Gate gate, bool hidden) {
    if (!hidden || !g_live.load(std::memory_order_acquire) || !mp_guard::allowsGameTouch()) {
        return;
    }
    std::uintptr_t settings = 0;
    std::memcpy(&settings, reinterpret_cast<const void*>(r.rdi + kSettings), sizeof(settings));
    r.rax = settings; // as on the game's own way in (the anchor, 0x1C7851E on Steam): the settings block
    if (*reinterpret_cast<const std::uint8_t*>(settings + kSkipLightCpu) != 0) {
        r.resumeAt = g_keep; // r_skipLightCPUCulling: the frustum test is off, so every light is kept
        return;
    }
    t_asking = gate;
    r.resumeAt = g_frustum;
}
void onGateA(HookRegisters& r) {
    onGate(r, GateA, (r.rax & 0xFF) == 0);
}
void onGateB(HookRegisters& r) {
    onGate(r, GateB, (r.rax & 0xFFFFFFFF) == 0);
}
void onSphere(HookRegisters& r) {
    onGate(r, GateSphere, (r.rax & 0xFF) == 0);
}

// At the frustum test's verdict: a light it drops takes the drop of the gate that asked (gate B clears the
// light's done bit, so another area of the view can still keep it).
void onVerdict(HookRegisters& r) {
    const int gate = t_asking;
    if (gate == NoGate) {
        return;
    }
    t_asking = NoGate;
    const bool drops = (r.rax & 0xFF) != 0;
    if (drops) {
        r.resumeAt = g_drop[gate];
        g_counters.dropped.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_counters.kept[gate].fetch_add(1, std::memory_order_relaxed);
    }
    if ((g_counters.tested.fetch_add(1, std::memory_order_relaxed) & 0xFF) == 0) {
        report();
    }
}

void install() {
    std::wstring value;
    const bool set = readEnv(L"ETERNALVR_LIGHT_FRUSTUM", value);
    if (!lightFrustumWanted(set, value)) {
        EVR_LOG("%s: off (ETERNALVR_LIGHT_FRUSTUM=0); lights keep Umbra's culling", kTag);
        return;
    }
    if (!mp_guard::allowsGameTouch()) {
        EVR_LOG("%s: the multiplayer guard is not armed; not installed", kTag);
        return;
    }
    GameText text;
    if (!findGameText(text)) {
        EVR_LOG("%s: the game's code cannot be read", kTag);
        return;
    }
    const light_cull::Build* build = light_cull::findBuild(text.timestamp);
    if (!build) {
        EVR_LOG(
            "%s: game build 0x%08X is not one this was read on (Steam build 25216728, Game Pass 1.0.56.0); "
            "lights keep Umbra's culling",
            kTag, text.timestamp);
        return;
    }
    const std::byte* at = findUniqueInText(text, kTag, "the light gather's Umbra test", build->signature);
    if (!at) {
        EVR_LOG("%s: the light gather's Umbra test is not in this build (%s); lights keep Umbra's culling",
                kTag, build->name);
        return;
    }
    if (rvaOf(text, at) != build->anchorRva) {
        EVR_LOG("%s: the light gather's Umbra test is at RVA 0x%X, not 0x%X as on %s; lights keep Umbra's "
                "culling",
                kTag, rvaOf(text, at), build->anchorRva, build->name);
        return;
    }
    const std::byte* end = text.bytes.data() + text.bytes.size();
    for (const light_cull::ByteCheck& e : build->checks) {
        if (at + e.offset + e.size > end || std::memcmp(at + e.offset, e.bytes, e.size) != 0) {
            EVR_LOG("%s: unexpected code for the %s at RVA 0x%X (%s); lights keep Umbra's culling", kTag,
                    e.what, rvaOf(text, at + e.offset), build->name);
            return;
        }
    }
    const light_cull::GatherOffsets& o = build->at;
    const auto address = [&](std::size_t offset) {
        return reinterpret_cast<std::uintptr_t>(at + offset);
    };
    g_frustum = address(o.frustum);
    g_keep = address(o.keep);
    g_drop[GateA] = address(o.frustumDrop);
    g_drop[GateB] = address(o.gateBDrop);
    g_drop[GateSphere] = address(o.frustumDrop);
    // The verdict's hook first: a gate can only send a light there once it is in (and all stay inert until
    // g_live anyway).
    struct Hook {
        std::size_t offset;
        MidHookEditCallback callback;
    };
    const Hook hooks[] = {
        {o.verdict, &onVerdict}, {o.gateA, &onGateA}, {o.gateB, &onGateB}, {o.sphere, &onSphere}};
    std::string error;
    for (const Hook& h : hooks) {
        if (!installMidHookEdit(const_cast<std::byte*>(at + h.offset), h.callback, error)) {
            // Hooks already in stay in (they cannot be removed) and keep Umbra's verdicts (g_live).
            EVR_LOG("%s: hook at RVA 0x%X failed: %s; lights keep Umbra's culling", kTag,
                    rvaOf(text, at + h.offset), error.c_str());
            return;
        }
    }
    g_installed = true;
    g_live.store(true, std::memory_order_release);
    if (set) {
        EVR_LOG("%s: on (ETERNALVR_LIGHT_FRUSTUM=\"%ls\"; only 0 turns it off) on %s: a light Umbra hides is "
                "dropped only when the view frustum drops it too (gates at RVA 0x%X, 0x%X, 0x%X)",
                kTag, value.c_str(), build->name, rvaOf(text, at + o.gateA), rvaOf(text, at + o.gateB),
                rvaOf(text, at + o.sphere));
    } else {
        EVR_LOG("%s: on by default (ETERNALVR_LIGHT_FRUSTUM=0 turns it off) on %s: a light Umbra hides is "
                "dropped only when the view frustum drops it too (gates at RVA 0x%X, 0x%X, 0x%X)",
                kTag, build->name, rvaOf(text, at + o.gateA), rvaOf(text, at + o.gateB),
                rvaOf(text, at + o.sphere));
    }
}

} // namespace

bool installLightFrustumCull() {
    std::call_once(g_once, install);
    return g_installed;
}

} // namespace evr::vkcore
