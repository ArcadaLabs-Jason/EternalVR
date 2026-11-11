#include "vkcore/taa_hooks.hpp"

#include "stereo_seq/stereo_taa.hpp"
#include "vkcore/cvar_book.hpp"
#include "vkcore/exposure_hooks.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/refraction_hooks.hpp"
#include "vkcore/runtime_cvars.hpp"
#include "vkcore/scatter_hooks.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/ssdo_hooks.hpp"
#include "vkcore/stereo_hooks.hpp"
#include "vkcore/taa_debug_cvars.hpp"
#include "vkcore/taa_hooks_impl.hpp"
#include "vkcore/taa_locate.hpp"
#include "vkcore/taa_ngx.hpp"
#include "vkcore/taa_ssr.hpp"

#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace evr::vkcore {

namespace taa_detail {

// ---- State ----

std::byte* g_secondSlot = nullptr;                    // eye R's slot (heap, never freed)
std::atomic<std::byte*> g_secondSlotContext{nullptr}; // the device context it was built with

SelectorFn g_outputOriginal = nullptr;
SelectorFn g_historyOriginal = nullptr;
SelectorFn g_opaqueOriginal = nullptr;
std::atomic<bool> g_perEye{false};
std::atomic<bool> g_failedClosed{false};
Counters g_counters;

} // namespace taa_detail

using namespace taa_detail;

namespace {

std::once_flag g_installOnce;
SetCvarFn g_setCvar = nullptr;
bool g_selectorsHooked = false;

// Cvar objects: the forced set, the fail-closed set, then the read-back ones and r_SSR.
std::vector<std::string_view> g_cvarNames;
std::vector<std::byte*> g_cvars;
std::byte* g_numSubSamples = nullptr;
std::byte* g_antialiasing = nullptr;
std::byte* g_safeMode = nullptr;
std::byte* g_jitter = nullptr;
std::byte* g_antiGhosting = nullptr;
std::byte* g_dlssQuality = nullptr;

bool g_distortionHooked = false;

std::atomic<bool> g_decided{false};
std::atomic<int> g_dlssFallbacks{0};
bool g_ngxHooked = false;
std::atomic<std::uint64_t> g_cvarWrites{0};

std::byte* cvarByName(std::string_view name) {
    for (std::size_t i = 0; i < g_cvarNames.size(); ++i) {
        if (g_cvarNames[i] == name) {
            return g_cvars[i];
        }
    }
    return nullptr;
}

// ---- Cvars ----

// Writes `value` through the cvar book (a trip sets it back) unless the cvar already holds it (the launch
// helper puts the same values on the command line, so a write is the exception). The engine itself sets
// cvars from its render threads (r_jitter every backend frame, r_dlssForceReset's countdown, r_antialiasing
// after a DLSS failure). A cvar ETERNALVR_DEBUG_CVARS holds is not written: true when it holds `value`.
bool setCvar(std::string_view name, const char* value) {
    std::byte* cvar = cvarByName(name);
    if (!cvar || !g_setCvar || !mp_guard::allowsGameTouch()) {
        return false;
    }
    const int before = cvarInt(cvar);
    if (taaLeftToDebugCvars(name)) {
        return before == std::atoi(value);
    }
    if (before == std::atoi(value) || !cvar_book::write(name, cvar, g_setCvar, value)) {
        return before == std::atoi(value);
    }
    const std::uint64_t writes = ++g_cvarWrites;
    if (writes <= 30 || writes % 100 == 0) {
        EVR_LOG("%s: %.*s %d -> %s (reads %d; write %llu)", kTag, static_cast<int>(name.size()), name.data(),
                before, value, cvarInt(cvar), static_cast<unsigned long long>(writes));
    }
    return true;
}

// `skip`: a cvar of the set written elsewhere (takeUpscaleQuality).
void applySet(const std::vector<stereo_seq::CvarExpectation>& set, std::string_view skip = {}) {
    for (const auto& c : set) {
        if (c.name == skip) {
            continue;
        }
        // With the scattering, SSDO or refraction history per eye (scatter_hooks.hpp, ssdo_hooks.hpp,
        // refraction_hooks.hpp) its temporal filter stays on.
        if ((c.name == "r_lightScatteringTAA" && scatterPerEyeReady()) ||
            (c.name == "r_SSDOTemporalAA" && ssdoPerEyeReady()) ||
            (c.name == "r_refractionTAA" && refractionPerEyeReady())) {
            setCvar(c.name, "1");
            continue;
        }
        const std::string value(c.value);
        setCvar(c.name, value.c_str());
    }
}

constexpr std::string_view kUpscaleQuality = "r_raytracedReflectionsTemporalUpscaleQuality";

// The forced set's r_raytracedReflectionsTemporalUpscaleQuality 0, read and written back at once here, not in
// applySet: the game's Reflections setting writes it last (after r_SSR), and a write landing between this
// read and a later write in applySet would be put back to 0 unseen, leaving the r_SSR before it held
// (taa_ssr.hpp). The value read; -1 when not located.
int takeUpscaleQuality() {
    const std::byte* cvar = cvarByName(kUpscaleQuality);
    if (!cvar) {
        return -1;
    }
    const int value = cvarInt(cvar);
    if (value != 0) {
        setCvar(kUpscaleQuality, "0");
    }
    return value;
}

stereo_seq::TaaReadiness readiness() {
    stereo_seq::TaaReadiness r;
    r.selectors = g_selectorsHooked;
    r.secondPair = g_secondSlotContext.load(std::memory_order_acquire) != nullptr;
    r.subSamples = g_numSubSamples != nullptr;
    r.exposure = exposureHookInstalled(); // installed at Route S start (exposure_hooks.hpp)
    r.cvarSetter = g_setCvar != nullptr;
    for (const auto& c : stereo_seq::stereoTaaForcedCvars()) {
        r.cvarSetter = r.cvarSetter && cvarByName(c.name) != nullptr;
    }
    for (const auto& c : stereo_seq::stereoTaaFailClosedCvars()) {
        r.cvarSetter = r.cvarSetter && cvarByName(c.name) != nullptr;
    }
    return r;
}

} // namespace

bool installTaaHooks() {
    std::call_once(g_installOnce, [] {
        logTaaDebugCvars();
        GameImage image;
        TaaEngine engine;
        if (!locateGameImage(image, kTag) || !locateTaaEngine(image, engine)) {
            EVR_LOG("%s: a per-eye TAA signature is missing or not unique", kTag);
            return;
        }
        g_setCvar = reinterpret_cast<SetCvarFn>(const_cast<std::byte*>(engine.setCvar));
        for (const auto* set :
             {&stereo_seq::stereoTaaForcedCvars(), &stereo_seq::stereoTaaFailClosedCvars()}) {
            for (const auto& c : *set) {
                g_cvarNames.push_back(c.name);
            }
        }
        for (const std::string_view name : {"r_TAANumSubSamples", "r_jitter", "r_antialiasing",
                                            "r_TAASafeMode", "r_TAAAntiGhosting", "r_dlssQuality", "r_SSR"}) {
            g_cvarNames.push_back(name);
        }
        g_cvars = findCvarObjects(image, g_cvarNames);
        g_numSubSamples = cvarByName("r_TAANumSubSamples");
        g_jitter = cvarByName("r_jitter");
        g_antialiasing = cvarByName("r_antialiasing");
        g_safeMode = cvarByName("r_TAASafeMode");
        g_antiGhosting = cvarByName("r_TAAAntiGhosting");
        g_dlssQuality = cvarByName("r_dlssQuality");
        std::string error;
        if (!installInlineHook(const_cast<std::byte*>(engine.outputSelector),
                               reinterpret_cast<void*>(&outputSelector),
                               reinterpret_cast<void**>(&g_outputOriginal), error) ||
            !installInlineHook(const_cast<std::byte*>(engine.historySelector),
                               reinterpret_cast<void*>(&historySelector),
                               reinterpret_cast<void**>(&g_historyOriginal), error) ||
            !installInlineHook(const_cast<std::byte*>(engine.opaqueSelector),
                               reinterpret_cast<void*>(&opaqueSelector),
                               reinterpret_cast<void**>(&g_opaqueOriginal), error)) {
            EVR_LOG("%s: accumulation selector hook failed: %s", kTag, error.c_str());
            return;
        }
        g_selectorsHooked = true;
        if (engine.distortionSite) {
            g_distortionHooked =
                installMidHookEdit(const_cast<std::byte*>(engine.distortionSite), &onDistortionBind, error);
        }
        g_ngxHooked = installNgxTwins();
        EVR_LOG("%s: accumulation selectors hooked; DLSS per eye %s", kTag,
                g_ngxHooked ? "ready" : "NOT available (DLSS falls back to TAA)");
    });
    const char* missing = stereo_seq::taaMissingPiece(readiness());
    if (missing) {
        EVR_LOG("%s: per-eye TAA is missing %s; the first stereo tick writes the v1 cvars", kTag, missing);
    }
    return missing == nullptr;
}

void taaOnStereoTick() {
    if (!taaRequested() || !mp_guard::allowsGameTouch()) {
        return;
    }
    if (!g_decided.exchange(true)) {
        const char* missing = stereo_seq::taaMissingPiece(readiness());
        if (missing) {
            // Fail closed: no temporal accumulation, as Route S v1. If even that cannot be written, the eyes
            // would share the history: no stereo at all.
            EVR_LOG("%s: per-eye TAA not available (%s): writing the v1 set; %s", kTag, missing,
                    exposureHookInstalled() ? "the exposure index stays per eye"
                                            : "eye R updates its own exposure (no exposure index hook)");
            applySet(stereo_seq::stereoTaaFailClosedCvars());
            g_failedClosed.store(true);
            ssrDecide(false, cvarByName("r_SSR") != nullptr, taaSsrLeftToDebugCvars());
            const bool closed =
                g_antialiasing && g_safeMode && cvarInt(g_antialiasing) == 0 && cvarInt(g_safeMode) == 1;
            if (!closed) {
                seqSetStereoAllowed(false);
                EVR_LOG("%s: the v1 set could not be written; stereo off, mono", kTag);
            }
            return;
        }
        g_perEye.store(true, std::memory_order_release);
        // The layer's run-time stereo set stops holding TAA off: this module owns the TAA cvars now.
        runtime_cvars::setStereoTemporal(stereo_seq::StereoTemporal::PerEye);
        setNgxTwinsActive(g_ngxHooked);
        EVR_LOG("%s: per-eye TAA on: r_antialiasing %d, r_TAASafeMode %d, r_TAANumSubSamples %d", kTag,
                cvarInt(g_antialiasing), cvarInt(g_safeMode), taaNumSubSamples());
        ssrDecide(true, cvarByName("r_SSR") != nullptr, taaSsrLeftToDebugCvars());
    }
    // Every stereo tick: the game applies the player's profile at run time as well (it can bring back
    // r_TAASafeMode, r_antialiasing and the effects), so the set is checked each tick and written only
    // where it differs.
    if (g_failedClosed.load()) {
        ssrRelease();
        applySet(stereo_seq::stereoTaaFailClosedCvars());
        return;
    }
    if (!g_perEye.load()) {
        return;
    }
    // r_SSR (taa_ssr.hpp) from the upscale quality the game's Reflections setting may have written since.
    const char* ssr = ssrHoldTick(takeUpscaleQuality());
    applySet(stereo_seq::stereoTaaForcedCvars(), kUpscaleQuality);
    // Per-eye history is in place: temporal AA on, deliberately.
    const bool safeModeOff = setCvar("r_TAASafeMode", "0");
    // Only once safe mode is off: while it is on, the game writes r_SSR 0 on every render.
    if (ssr && safeModeOff) {
        setCvar("r_SSR", ssr);
    }
    // DLSS without a per-eye feature for eye R would mix the eyes: TAA (per eye) until it is tried again.
    ngxTwinsTick();
    const bool dlssPerEye = g_ngxHooked && !ngxTwinFailed();
    const int current = cvarInt(g_antialiasing);
    const int held = stereo_seq::heldAntialiasing(current, taaDlssRequested(), dlssPerEye);
    if (held != current) {
        if (held == 1 && current == 2 && g_dlssFallbacks.fetch_add(1) < 16) {
            EVR_LOG("%s: DLSS has no per-eye feature for eye R; temporal AA instead", kTag);
        }
        setCvar("r_antialiasing", held == 2 ? "2" : "1");
    }
    // The launcher's DLSS quality (ETERNALVR_STEREO_DLSS_QUALITY) over the game's saved one.
    const int quality = taaDlssQuality();
    if (held == 2 && quality >= 0 && g_dlssQuality && cvarInt(g_dlssQuality) != quality) {
        setCvar("r_dlssQuality", std::to_string(quality).c_str());
    }
}

bool taaPerEyeActive() {
    return g_perEye.load(std::memory_order_acquire) && mp_guard::allowsGameTouch();
}

bool taaFailedClosed() {
    return g_failedClosed.load();
}

bool taaDlssPerEyeReady() {
    return g_ngxHooked && !ngxTwinFailed();
}

int taaNumSubSamples() {
    const int n = g_numSubSamples ? cvarInt(g_numSubSamples) : 0;
    return n > 0 ? n : 32;
}

TaaCounters taaCounters() {
    TaaCounters c;
    c.picks[0] = g_counters.picks[0].load();
    c.picks[1] = g_counters.picks[1].load();
    c.enginePicks = g_counters.enginePicks.load();
    c.opaquePicks = g_counters.opaquePicks.load();
    c.distortionBinds = g_counters.distortionBinds.load();
    for (int e = 0; e < 2; ++e) {
        for (int s = 0; s < 3; ++s) {
            c.tagVsView[e][s] = g_counters.tagVsView[e][s].load();
        }
    }
    c.exposureInFlightDiffers = exposureCounters().inFlightDiffers;
    c.secondPairBuilds = g_counters.secondPairBuilds.load();
    c.ngx = ngxCounters();
    const auto read = [](const std::byte* cvar) {
        return cvar ? cvarInt(cvar) : -1;
    };
    c.antialiasing = read(g_antialiasing);
    c.safeMode = read(g_safeMode);
    c.jitter = read(g_jitter);
    c.antiGhosting = read(g_antiGhosting);
    c.perEye = g_perEye.load();
    // Accumulation render target sizes (the render target object starts with its width and height).
    const auto size = [](const std::byte* slot, int i, int out[2]) {
        void* rt = nullptr;
        if (slot) {
            std::memcpy(&rt, slot + kSlotAccumulationTargets + static_cast<std::size_t>(i) * sizeof(void*),
                        sizeof(rt));
        }
        if (rt) {
            std::memcpy(out, rt, 2 * sizeof(int));
        }
    };
    std::byte* context = g_secondSlotContext.load();
    size(context ? context + kDeviceContextSlot0 : nullptr, 0, c.sizeA);
    size(context ? g_secondSlot : nullptr, 0, c.sizeB);
    return c;
}

} // namespace evr::vkcore
