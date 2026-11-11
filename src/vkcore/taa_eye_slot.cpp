// Per-eye TAA's eye R slot (taa_hooks.hpp): eye R's accumulation images, built with the device context and
// rebuilt on its resizes, and the accumulation selectors and the distortionLastFrameMap bind that give eye R
// its own images in the render-view job.

#include "vkcore/taa_hooks.hpp"
#include "vkcore/taa_hooks_impl.hpp"

#include "stereo_seq/stereo_taa.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/refraction_hooks.hpp"
#include "vkcore/scatter_hooks.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/ssdo_hooks.hpp"
#include "vkcore/stereo_hooks.hpp"
#include "vkcore/taa_locate.hpp"
#include "vkcore/taa_ngx.hpp"
#include "vkcore/taa_resize.hpp"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>

namespace evr::vkcore {

using namespace taa_detail;

namespace {

std::once_flag g_earlyOnce;
SlotBuilderFn g_slotBuilder = nullptr;

std::mutex g_slotMutex;

// The history target the history selector answered last on this thread: the render-view job asks for it
// (0x1C56614) just before it binds the last frame's view colour (0x1C5664B).
thread_local void* t_lastHistory = nullptr;

// The horizontal offset of the view's latched projection (idRenderView projectionMatrix [0][2]): each eye's
// asymmetric frustum is shifted to its own side.
int latchedSide(const std::byte* renderView) {
    float offset = 0.0f;
    std::memcpy(&offset, renderView + render_view_object::kProjection + 2 * sizeof(float), sizeof(offset));
    return offset < -0.01f ? 0 : (offset > 0.01f ? 2 : 1);
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

} // namespace

namespace taa_detail {

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

} // namespace taa_detail

void installTaaEarly() {
    std::call_once(g_earlyOnce, [] {
        if (!routeSRequested()) {
            return;
        }
        // Eye R's scattering volumes, SSDO targets and refraction images are made with the device context
        // too; they need only the eye tags.
        installScatterHooksEarly();
        installSsdoHooksEarly();
        installRefractionHooksEarly();
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

} // namespace evr::vkcore
