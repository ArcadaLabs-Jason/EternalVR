#include "vkcore/exposure_hooks.hpp"

#include "stereo_seq/stereo_taa.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/motion_capture.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/scatter_hooks.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/taa_hooks.hpp"
#include "vkcore/taa_locate.hpp"
#include "vkcore/vrs_nv.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-exposure";

constexpr std::size_t kPostProcessExposureIndex = 0x140;
constexpr std::size_t kPostProcessBackendFrame = 0x148;

std::once_flag g_once;
std::atomic<bool> g_hooked{false};
std::atomic<bool> g_exposureOnce{true};
std::mutex g_mutex;
stereo_seq::ExposurePlanner g_planner;

struct Counters {
    std::atomic<std::uint64_t> held[2]{};
    std::atomic<std::uint64_t> inFlightDiffers{0};
} g_counters;

// ---- Auto-exposure index (render-view job, after the engine stored its choice) ----

void onExposureIndex(const HookRegisters& regs) {
    // The render's own tag, found by the backend frame counter the render-view job stored in the context for
    // this render (RVA 0x1C570D2), the one the engine's own index was just chosen from. The tag in flight
    // reads the counter again now and would name the next render if the render thread's swap came in
    // between; the count of such renders is the self-check. Where foveation asks the render passes' frames
    // (and the multiplayer guard allows it), the read is noted for the render passes of the frame whether or
    // not the index is held; without either, nothing is read.
    const bool held = exposureIndexHeld();
    const bool note = vrs_nv::passesFollowFrames() && mp_guard::allowsGameTouch();
    if (!held && !note) {
        return;
    }
    auto* context = reinterpret_cast<std::byte*>(regs.rsi);
    std::uint32_t counter = 0;
    std::memcpy(&counter, context + kPostProcessBackendFrame, sizeof(counter));
    if (note) {
        seqNoteRenderViewCounter(counter);
    }
    if (!held) {
        return;
    }
    const std::optional<stereo_seq::RenderTag> tag = seqTagForBackendFrame(counter + 1u);
    const std::optional<stereo_seq::RenderTag> inFlight = seqTagInFlight();
    if ((inFlight ? inFlight->eye : stereo_seq::Eye::Mono) != (tag ? tag->eye : stereo_seq::Eye::Mono)) {
        ++g_counters.inFlightDiffers;
    }
    if (!tag) {
        return;
    }
    std::int32_t index = 0;
    {
        std::lock_guard lock(g_mutex);
        index = g_planner.indexFor(*tag);
    }
    std::memcpy(context + kPostProcessExposureIndex, &index, sizeof(index));
    ++g_counters.held[tag->eye == stereo_seq::Eye::Right ? 1 : 0];
    noteMotionTarget(*tag, context); // ETERNALVR_CAPTURE_MOTION
}

} // namespace

bool installExposureHook(bool exposureOnce) {
    g_exposureOnce.store(exposureOnce, std::memory_order_release);
    std::call_once(g_once, [] {
        if (!mp_guard::allowsGameTouch()) {
            EVR_LOG("%s: the multiplayer guard is not armed; the auto-exposure index is left to the engine",
                    kTag);
            return;
        }
        GameImage image;
        const std::byte* site = locateGameImage(image, kTag) ? locateExposureSite(image, kTag) : nullptr;
        std::string error;
        if (!site || !installMidHook(const_cast<std::byte*>(site), &onExposureIndex, error)) {
            EVR_LOG("%s: auto-exposure index hook not installed%s%s", kTag, error.empty() ? "" : ": ",
                    error.c_str());
            return;
        }
        g_hooked.store(true, std::memory_order_release);
        EVR_LOG("%s: auto-exposure index hooked at RVA 0x%X", kTag, image.rva(site));
    });
    return g_hooked.load(std::memory_order_acquire);
}

bool exposureHookInstalled() {
    return g_hooked.load(std::memory_order_acquire);
}

bool exposureIndexHeld() {
    stereo_seq::ExposureGate gate;
    gate.hooked = g_hooked.load(std::memory_order_acquire);
    gate.routeS = seqHooksActive();
    gate.gameTouch = mp_guard::allowsGameTouch();
    gate.taaRequested = taaRequested();
    gate.taaPerEye = taaPerEyeActive();
    gate.taaFailedClosed = taaFailedClosed();
    gate.exposureOnce = g_exposureOnce.load(std::memory_order_acquire);
    return stereo_seq::exposureIndexHeld(gate);
}

void logStereoTemporalMode() {
    const bool hooked = exposureHookInstalled();
    const bool exposureOnce = g_exposureOnce.load(std::memory_order_acquire);
    EVR_LOG("%s: per-eye TAA %s; auto-exposure index per eye %s, eye R %s; scattering history per eye %s",
            kTag,
            taaRequested() ? "requested (the exposure index is held from its first stereo tick)"
                           : "off (ETERNALVR_STEREO_TAA=0)",
            !hooked                          ? "NOT hooked"
            : exposureOnce || taaRequested() ? "hooked"
                                             : "hooked, left to the engine (one chain with eye L)",
            !exposureOnce ? "updates its own exposure (ETERNALVR_STEREO_EXPOSURE_ONCE=0)"
            : hooked      ? "takes eye L's exposure"
                          : "updates its own exposure (one chain with eye L)",
            scatterHooksInstalled() ? "hooked" : "off");
}

ExposureCounters exposureCounters() {
    ExposureCounters c;
    c.held[0] = g_counters.held[0].load();
    c.held[1] = g_counters.held[1].load();
    c.inFlightDiffers = g_counters.inFlightDiffers.load();
    return c;
}

} // namespace evr::vkcore
