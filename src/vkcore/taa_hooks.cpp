#include "vkcore/taa_hooks.hpp"

#include "stereo_seq/stereo_taa.hpp"
#include "vkcore/cvar_book.hpp"
#include "vkcore/exposure_hooks.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/runtime_cvars.hpp"
#include "vkcore/scatter_hooks.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/ssdo_hooks.hpp"
#include "vkcore/stereo_hooks.hpp"
#include "vkcore/taa_locate.hpp"
#include "vkcore/taa_ngx.hpp"
#include "vkcore/taa_resize.hpp"

#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-taa";

// ---- Engine layouts (docs/rig-findings/stereo-temporal.md) ----

constexpr std::size_t kRenderSystemDeviceContext = 0xF58;
// The device context's per-view slot i starts at +0x8 + i * 0xA8; in a slot, the accumulation render
// targets are at +0x58 and +0x60 (images at +0x30 and +0x38), and +0x0 is the index its image names use.
constexpr std::size_t kDeviceContextSlot0 = 0x8;
constexpr std::size_t kSlotSize = 0xA8;
constexpr std::size_t kSlotAccumulationTargets = 0x58;
constexpr std::size_t kSlotOpaqueTarget = 0x68;
constexpr std::uint32_t kSecondSlotIndex = 1; // its images are named "_accumulationBuffer10" and so on

using SelectorFn = void* (*)(void* renderSystem, const std::byte* renderView);
using SlotBuilderFn = void (*)(void* deviceContext, void* slot, std::uint64_t deviceContextIndex);
using SetCvarFn = cvar_book::SetStringFn;

// ---- State ----

std::once_flag g_earlyOnce;
std::once_flag g_installOnce;
SlotBuilderFn g_slotBuilder = nullptr;

std::mutex g_slotMutex;
std::byte* g_secondSlot = nullptr;                    // eye R's slot (heap, never freed)
std::atomic<std::byte*> g_secondSlotContext{nullptr}; // the device context it was built with

SelectorFn g_outputOriginal = nullptr;
SelectorFn g_historyOriginal = nullptr;
SelectorFn g_opaqueOriginal = nullptr;
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

std::atomic<bool> g_perEye{false};
// The history target the history selector answered last on this thread: the render-view job asks for it
// (0x1C56614) just before it binds the last frame's view colour (0x1C5664B).
thread_local void* t_lastHistory = nullptr;
bool g_distortionHooked = false;

std::atomic<bool> g_decided{false};
std::atomic<int> g_dlssFallbacks{0};
bool g_ngxHooked = false;
std::atomic<bool> g_failedClosed{false};
std::atomic<std::uint64_t> g_cvarWrites{0};
// r_SSR held at the player's value while per-eye history is in place (decideSsrHold).
std::string g_ssrValue;
std::atomic<bool> g_ssrHeld{false};

struct Counters {
    std::atomic<std::uint64_t> picks[2]{};
    std::atomic<std::uint64_t> enginePicks{0};
    std::atomic<std::uint64_t> secondPairBuilds{0};
    std::atomic<int> loggedPicks{0};
    std::atomic<std::uint64_t> opaquePicks{0};
    std::atomic<std::uint64_t> distortionBinds{0};
    // Output picks by the tag's eye (L, R) and by the side the latched projection is shifted to (left of
    // centre, centred, right): each tag eye should see one side only.
    std::atomic<std::uint64_t> tagVsView[2][3]{};
} g_counters;

// The horizontal offset of the view's latched projection (idRenderView projectionMatrix [0][2]): each eye's
// asymmetric frustum is shifted to its own side.
int latchedSide(const std::byte* renderView) {
    float offset = 0.0f;
    std::memcpy(&offset, renderView + render_view_object::kProjection + 2 * sizeof(float), sizeof(offset));
    return offset < -0.01f ? 0 : (offset > 0.01f ? 2 : 1);
}

std::byte* cvarByName(std::string_view name) {
    for (std::size_t i = 0; i < g_cvarNames.size(); ++i) {
        if (g_cvarNames[i] == name) {
            return g_cvars[i];
        }
    }
    return nullptr;
}

// ---- Eye R's slot, built with the device context (the renderer's start-up thread) ----

void onSlotBuilt(const HookRegisters& regs) {
    if (static_cast<std::uint32_t>(regs.rbx) != 0 || !mp_guard::allowsGameTouch()) {
        return; // only after slot 0; a render view index above 0 does not exist on this build
    }
    auto* deviceContext = reinterpret_cast<std::byte*>(regs.r14);
    std::uint32_t contextIndex = 0;
    std::memcpy(&contextIndex, deviceContext, sizeof(contextIndex));
    std::lock_guard lock(g_slotMutex);
    // A fresh slot for every device context: the builder frees what a slot held before, and a previous
    // device context's objects may belong to a device that is gone.
    auto* slot = new std::byte[kSlotSize]{};
    std::memcpy(slot, &kSecondSlotIndex, sizeof(kSecondSlotIndex));
    g_secondSlotContext.store(nullptr, std::memory_order_release);
    g_slotBuilder(deviceContext, slot, contextIndex);
    void* targets[2] = {};
    std::memcpy(targets, slot + kSlotAccumulationTargets, sizeof(targets));
    ++g_counters.secondPairBuilds;
    EVR_LOG("%s: eye R's accumulation images built with device context %p (index %u): render targets %p %p",
            kTag, static_cast<void*>(deviceContext), contextIndex, targets[0], targets[1]);
    if (targets[0] && targets[1]) {
        g_secondSlot = slot;
        g_secondSlotContext.store(deviceContext, std::memory_order_release);
    }
}

// ---- Eye R's slot follows the engine's resizes ----

using ContextResizeFn = void (*)(void* deviceContext, const int* size, const int* upscaledSize);
ContextResizeFn g_contextResizeOriginal = nullptr;
std::atomic<std::uint64_t> g_secondResizes{0};

// The device context resizes slot 0's targets in place when the render size changes. Resizing eye R's targets
// one by one made the engine ask for a new block of about 1 GB whatever the size (931 MB at 1415x1415, 1.28
// GB at 1280x1400), which a 12 GB card's budget refused behind a modal "Failed to allocate video memory" box
// (the game looked hung). The slot builder allocates the whole set as at start-up, so it runs again on eye
// R's slot (it frees what the slot held), inside the engine's own resize call. Only when a target eye R uses
// differs: a DLSS resize changes only the view colour (taa_resize.hpp), and a rebuild would then make eye R's
// opaque accumulation smaller than the engine's.
void contextResize(void* deviceContext, const int* size, const int* upscaledSize) {
    g_contextResizeOriginal(deviceContext, size, upscaledSize);
    auto* context = static_cast<std::byte*>(deviceContext);
    if (context != g_secondSlotContext.load(std::memory_order_acquire) || !g_secondSlot ||
        !mp_guard::allowsGameTouch() || g_failedClosed.load()) {
        return; // after a failure eye R's targets are no longer used
    }
    const std::byte* engineSlot = context + kDeviceContextSlot0;
    const unsigned before = slotSizeMismatches(engineSlot, g_secondSlot);
    if (before == 0) {
        return;
    }
    logSlotSizes(kTag, engineSlot, g_secondSlot, size, upscaledSize);
    if ((before & kEyeRTargets) == 0) {
        EVR_LOG("%s: eye R's slot kept (the targets it uses match the engine's)", kTag);
        return;
    }
    std::uint32_t contextIndex = 0;
    std::memcpy(&contextIndex, context, sizeof(contextIndex));
    std::lock_guard lock(g_slotMutex);
    g_slotBuilder(context, g_secondSlot, contextIndex);
    void* pair[2] = {};
    std::memcpy(pair, g_secondSlot + kSlotAccumulationTargets, sizeof(pair));
    const unsigned after = slotSizeMismatches(engineSlot, g_secondSlot);
    logSlotSizes(kTag, engineSlot, g_secondSlot, size, upscaledSize);
    ++g_secondResizes;
    EVR_LOG("%s: device context resized to %dx%d; eye R's slot rebuilt (%llu time(s)): render targets %p %p, "
            "size mismatch mask 0x%X (eye R uses 0x%X)",
            kTag, size ? size[0] : 0, size ? size[1] : 0,
            static_cast<unsigned long long>(g_secondResizes.load()), pair[0], pair[1], after, kEyeRTargets);
    if (!pair[0] || !pair[1] || (after & kEyeRTargets) != 0) {
        // Both eyes take the v1 set (no temporal AA) for the rest of the session, from the next stereo tick.
        g_failedClosed.store(true);
        g_perEye.store(false, std::memory_order_release);
        EVR_LOG(
            "%s: eye R's rebuilt slot is not usable; per-eye TAA off, the v1 set for the rest of the session",
            kTag);
    }
}

// ---- Accumulation selectors (render-view job of each backend frame) ----

void* select(stereo_seq::AccumRole role,
             void* renderSystem,
             const std::byte* renderView,
             SelectorFn original) {
    std::int32_t viewIndex = -1;
    if (renderView) {
        std::memcpy(&viewIndex, renderView + render_view_object::kViewIndex, sizeof(viewIndex));
    }
    std::byte* context = nullptr;
    if (renderSystem) {
        std::memcpy(&context, static_cast<std::byte*>(renderSystem) + kRenderSystemDeviceContext,
                    sizeof(context));
    }
    const bool second = context && context == g_secondSlotContext.load(std::memory_order_acquire);
    if (!g_perEye.load(std::memory_order_acquire) || viewIndex != 0 || !second ||
        !mp_guard::allowsGameTouch()) {
        ++g_counters.enginePicks;
        return original(renderSystem, renderView);
    }
    const std::optional<stereo_seq::RenderTag> tag = seqTagInFlight();
    const stereo_seq::AccumPick pick = stereo_seq::pickAccumulation(role, tag ? &*tag : nullptr, true);
    if (pick.engine) {
        ++g_counters.enginePicks;
        return original(renderSystem, renderView);
    }
    const std::byte* slot = pick.pair == 0 ? context + kDeviceContextSlot0 : g_secondSlot;
    void* target = nullptr;
    std::memcpy(&target,
                slot + kSlotAccumulationTargets + static_cast<std::size_t>(pick.index) * sizeof(void*),
                sizeof(target));
    if (!target) {
        ++g_counters.enginePicks;
        return original(renderSystem, renderView);
    }
    ++g_counters.picks[pick.pair];
    if (role == stereo_seq::AccumRole::Output) {
        ++g_counters.tagVsView[stereo_seq::eyeIndex(tag->eye)][latchedSide(renderView)];
        noteNgxOutput(target, *tag); // the render's DLSS evaluation finds its tag by this image
    }
    if (g_counters.loggedPicks.fetch_add(1) < 8) {
        EVR_LOG("%s: %s of backend frame for eye %s (its frame %u): pair %d image %d", kTag,
                role == stereo_seq::AccumRole::Output ? "output" : "history", stereo_seq::eyeName(tag->eye),
                tag->eyeSeq, pick.pair, pick.index);
    }
    return target;
}

void* outputSelector(void* renderSystem, const std::byte* renderView) {
    return select(stereo_seq::AccumRole::Output, renderSystem, renderView, g_outputOriginal);
}

void* historySelector(void* renderSystem, const std::byte* renderView) {
    void* target = select(stereo_seq::AccumRole::History, renderSystem, renderView, g_historyOriginal);
    t_lastHistory = target;
    return target;
}

// The opaque accumulation (one image per slot, read and written by the TAA pass): eye R's slot for eye R.
void* opaqueSelector(void* renderSystem, const std::byte* renderView) {
    std::int32_t viewIndex = -1;
    if (renderView) {
        std::memcpy(&viewIndex, renderView + render_view_object::kViewIndex, sizeof(viewIndex));
    }
    if (g_perEye.load(std::memory_order_acquire) && viewIndex == 0 && g_secondSlot &&
        mp_guard::allowsGameTouch()) {
        const std::optional<stereo_seq::RenderTag> tag = seqTagInFlight();
        if (tag && tag->eye == stereo_seq::Eye::Right) {
            void* target = nullptr;
            std::memcpy(&target, g_secondSlot + kSlotOpaqueTarget, sizeof(target));
            if (target) {
                ++g_counters.opaquePicks;
                return target;
            }
        }
    }
    return g_opaqueOriginal(renderSystem, renderView);
}

// ---- distortionLastFrameMap (render-view job) ----

// The engine binds the view colour image, the previous frame's scene colour, as distortionLastFrameMap
// (refraction and distortion materials read it). One image for both eyes: each eye would see the other's
// frame through them. The eye's own history image (its previous frame, after TAA) is bound instead.
void onDistortionBind(HookRegisters& regs) {
    if (!g_perEye.load(std::memory_order_acquire) || !t_lastHistory || !mp_guard::allowsGameTouch()) {
        return;
    }
    std::uintptr_t image = 0;
    std::memcpy(&image, static_cast<std::byte*>(t_lastHistory) + 0x10, sizeof(image));
    if (image) {
        regs.r8 = image;
        ++g_counters.distortionBinds;
    }
    t_lastHistory = nullptr;
}

// ---- Cvars ----

// Writes `value` through the cvar book (a trip sets it back) unless the cvar already holds it (the launch
// helper puts the same values on the command line, so a write is the exception). The engine itself sets
// cvars from its render threads (r_jitter every backend frame, r_dlssForceReset's countdown, r_antialiasing
// after a DLSS failure).
bool setCvar(std::string_view name, const char* value) {
    std::byte* cvar = cvarByName(name);
    if (!cvar || !g_setCvar || !mp_guard::allowsGameTouch()) {
        return false;
    }
    const int before = cvarInt(cvar);
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

void applySet(const std::vector<stereo_seq::CvarExpectation>& set) {
    for (const auto& c : set) {
        // With the scattering or SSDO history per eye (scatter_hooks.hpp, ssdo_hooks.hpp) its temporal filter
        // stays on.
        if ((c.name == "r_lightScatteringTAA" && scatterPerEyeReady()) ||
            (c.name == "r_SSDOTemporalAA" && ssdoPerEyeReady())) {
            setCvar(c.name, "1");
            continue;
        }
        const std::string value(c.value);
        setCvar(c.name, value.c_str());
    }
}

// Screen-space reflections keep no history of their own: they read the last frame's colour through the
// history selector above, so with per-eye history each eye reads its own, and r_SSR is held at the player's
// value (ETERNALVR_STEREO_SSR, stereo_seq::stereoSsrCvar). Without it the game writes r_SSR 0 on every render
// itself (r_TAASafeMode 1, 0x1C6FCC0), which is right then: the eyes would share that colour.
void decideSsrHold(bool perEye) {
    const std::string& setting = taaSsrSetting();
    const auto c = stereo_seq::stereoSsrCvar(setting);
    if (!c) {
        EVR_LOG("%s: r_SSR is left as the game has it (ETERNALVR_STEREO_SSR=%s)", kTag, setting.c_str());
    } else if (!perEye) {
        EVR_LOG("%s: r_SSR is not held: per-eye TAA failed closed", kTag);
    } else if (!cvarByName(c->name)) {
        EVR_LOG("%s: r_SSR not located; left as the game has it", kTag);
    } else {
        g_ssrValue = std::string(c->value);
        g_ssrHeld.store(true);
        EVR_LOG("%s: r_SSR held at %s (ETERNALVR_STEREO_SSR=%s; per-eye TAA)", kTag, g_ssrValue.c_str(),
                setting.empty() ? "unset" : setting.c_str());
    }
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

void installTaaEarly() {
    std::call_once(g_earlyOnce, [] {
        if (!routeSRequested()) {
            return;
        }
        // Eye R's scattering volumes and SSDO targets are made with the device context too; they need only
        // the eye tags.
        installScatterHooksEarly();
        installSsdoHooksEarly();
        if (!taaRequested()) {
            return;
        }
        if (!mp_guard::allowsGameTouch()) {
            EVR_LOG("%s: the multiplayer guard is not armed; eye R's images are not built (per-eye TAA off)",
                    kTag);
            return;
        }
        GameImage image;
        TaaSlotSite site;
        if (!locateGameImage(image, kTag) || !locateTaaSlotSite(image, site)) {
            EVR_LOG("%s: device context slot loop not found; per-eye TAA off", kTag);
            return;
        }
        g_slotBuilder = reinterpret_cast<SlotBuilderFn>(const_cast<std::byte*>(site.slotBuilder));
        std::string error;
        if (!installInlineHook(const_cast<std::byte*>(site.contextResize),
                               reinterpret_cast<void*>(&contextResize),
                               reinterpret_cast<void**>(&g_contextResizeOriginal), error)) {
            EVR_LOG("%s: device context resize hook failed: %s; per-eye TAA off", kTag, error.c_str());
            return;
        }
        if (!installMidHook(const_cast<std::byte*>(site.hookSite), &onSlotBuilt, error)) {
            EVR_LOG("%s: slot loop hook failed: %s; per-eye TAA off", kTag, error.c_str());
            return;
        }
        EVR_LOG("%s: slot loop hooked at RVA 0x%X: eye R's accumulation images are built with the renderer",
                kTag, image.rva(site.hookSite));
    });
}

bool installTaaHooks() {
    std::call_once(g_installOnce, [] {
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
            decideSsrHold(false);
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
        decideSsrHold(true);
    }
    // Every stereo tick: the game applies the player's profile at run time as well (it can bring back
    // r_TAASafeMode, r_antialiasing and the effects), so the set is checked each tick and written only
    // where it differs.
    if (g_failedClosed.load()) {
        if (g_ssrHeld.exchange(false)) {
            EVR_LOG("%s: r_SSR no longer held: per-eye TAA failed closed", kTag);
        }
        applySet(stereo_seq::stereoTaaFailClosedCvars());
        return;
    }
    if (!g_perEye.load()) {
        return;
    }
    applySet(stereo_seq::stereoTaaForcedCvars());
    // Per-eye history is in place: temporal AA on, deliberately.
    const bool safeModeOff = setCvar("r_TAASafeMode", "0");
    // Only once safe mode is off: while it is on, the game writes r_SSR 0 on every render.
    if (g_ssrHeld.load() && safeModeOff) {
        setCvar("r_SSR", g_ssrValue.c_str());
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
