#include "vkcore/exposure_hooks.hpp"

#include "stereo_seq/stereo_taa.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/motion_capture.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/scatter_hooks.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/ssdo_hooks.hpp"
#include "vkcore/stereo_hooks.hpp"
#include "vkcore/taa_hooks.hpp"
#include "vkcore/taa_locate.hpp"
#include "vkcore/view_slots.hpp"
#include "vkcore/vrs_nv.hpp"

#include <atomic>
#include <chrono>
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
// The post-process context's render context block (render context + 0x4D9C70) and render view.
constexpr std::size_t kPostProcessBlock = 0x38;
constexpr std::size_t kPostProcessRenderView = 0x40;
// The block's skip-exposure-update byte, latched from renderView_t + 0x74C (stereo_view_fields::
// kSkipAutoExposureUpdate) at 0x1C5E21C; the update in the post-process job (0x1C92D08) tests it.
constexpr std::size_t kBlockSkipExposureUpdate = 0x21;
// At the hook site r14 is that block; its byte +0x9D (render context + 0x4D9D0D, latched from renderView_t +
// 0x634) makes the render adapt its exposure at once (the factor stays 1.0, 0x1C98D46-0x1C98D7D), as
// r_hdrAutoExposureInstant does.
constexpr std::size_t kBlockInstantExposure = 0x9D;
constexpr std::uint64_t kInstantLogged = 20;

std::once_flag g_once;
std::atomic<bool> g_hooked{false};
std::atomic<bool> g_exposureOnce{true};
std::mutex g_mutex;
stereo_seq::ExposurePlanner g_planner;

struct Counters {
    std::atomic<std::uint64_t> held[2]{};
    std::atomic<std::uint64_t> inFlightDiffers{0};
} g_counters;

// ---- Parallel Eye Rendering ----

constexpr const char* kPeTag = "pe-exposure";

std::atomic<bool> g_pe{false};
std::atomic<parallel_eyes::Exposure> g_peMode{parallel_eyes::Exposure::Same};

struct PeCounters {
    std::atomic<std::uint64_t> view0{0};
    std::atomic<std::uint64_t> view0OtherParity{0}; // view 0's engine index is not its frame's parity
    std::atomic<std::uint64_t> view1{0};
    std::atomic<std::uint64_t> view1EngineDiffers{0}; // the engine's index for view 1 differed from view 0's
    std::atomic<std::uint64_t> view1Unskipped{0};     // view 1 would have run its own exposure update
    // Self-checks. The frame count of view 1's render against view 0's last one: the same, or one ahead when
    // view 1's render-view job ran first; other means "same" would not read view 0's image of this frame.
    std::atomic<std::uint64_t> view1FrameSame{0};
    std::atomic<std::uint64_t> view1FrameAhead{0};
    std::atomic<std::uint64_t> view1FrameOther{0};
    // With its skip flag already set the engine's index for view 1 is the parity of view 1's last own update;
    // a change means that update ran (the forced skip byte came too late, or the engine mode let it run).
    std::atomic<std::uint64_t> view1UpdateSeen{0};
    std::atomic<std::uint64_t> instant[2]{}; // renders adapting at once (the block's instant byte), per view
    std::atomic<std::uint64_t> otherView{0};
    std::atomic<long long> lastReport{0};
} g_pe10s;
std::atomic<std::uint32_t> g_view0Frame{0};
std::atomic<bool> g_view0Seen{false};
std::atomic<std::int32_t> g_view1SkipIndex{-1};
std::atomic<std::uint64_t> g_instantTotal{0};

const char* peModeText(parallel_eyes::Exposure mode) {
    switch (mode) {
    case parallel_eyes::Exposure::Same:
        return "view 1 reads the exposure view 0 writes this frame";
    case parallel_eyes::Exposure::Prev:
        return "view 1 reads the exposure view 0 wrote the frame before (ETERNALVR_PE_EXPOSURE=prev)";
    case parallel_eyes::Exposure::Engine:
        return "view 1's index and skip left to the engine, counted only (ETERNALVR_PE_EXPOSURE=engine)";
    }
    return "?";
}

void logParallelEyesCounters() {
    const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count();
    long long last = g_pe10s.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_pe10s.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    const bool engine = g_peMode.load(std::memory_order_relaxed) == parallel_eyes::Exposure::Engine;
    EVR_LOG(
        "%s: last 10 s: view 0 %llu (index not its frame's parity %llu); view 1 %llu, the engine's index "
        "differed from view 0's %llu, its own update %s %llu, its last own update changed %llu, its frame "
        "count as view 0's last %llu / one ahead %llu / other %llu; adapted at once view 0 %llu / view 1 "
        "%llu; "
        "other views %llu",
        kPeTag, static_cast<unsigned long long>(g_pe10s.view0.exchange(0)),
        static_cast<unsigned long long>(g_pe10s.view0OtherParity.exchange(0)),
        static_cast<unsigned long long>(g_pe10s.view1.exchange(0)),
        static_cast<unsigned long long>(g_pe10s.view1EngineDiffers.exchange(0)),
        engine ? "ran (no skip flag)" : "blocked (no skip flag)",
        static_cast<unsigned long long>(g_pe10s.view1Unskipped.exchange(0)),
        static_cast<unsigned long long>(g_pe10s.view1UpdateSeen.exchange(0)),
        static_cast<unsigned long long>(g_pe10s.view1FrameSame.exchange(0)),
        static_cast<unsigned long long>(g_pe10s.view1FrameAhead.exchange(0)),
        static_cast<unsigned long long>(g_pe10s.view1FrameOther.exchange(0)),
        static_cast<unsigned long long>(g_pe10s.instant[0].exchange(0)),
        static_cast<unsigned long long>(g_pe10s.instant[1].exchange(0)),
        static_cast<unsigned long long>(g_pe10s.otherView.exchange(0)));
}

// `context` is the post-process context (rsi). View 0 keeps the engine's index, its backend frame's parity:
// it never skips its update, so the frame count it last updated in is the frame before. View 1's index
// becomes that same parity (Same) or the other one (Prev), and its skip byte is set, also on a frame whose
// eye pose was not written (presenter_stereo.cpp's onEyeView returns before setting the flag there), so that
// view 1 does not write the shared exposure images or the luminance chain; the post-process job reads the
// byte later (inferred, docs/VR_STEREO.md), which the "last own update changed" count checks.
//
// Both views: a render whose block asks to adapt at once is counted, the first kInstantLogged with a line
// each (a whole-image exposure jump in one frame; which game event sets it is not known).
void onParallelEyesExposureIndex(std::byte* context, const std::byte* instantBlock) {
    std::byte* renderView = nullptr;
    std::memcpy(&renderView, context + kPostProcessRenderView, sizeof(renderView));
    if (!renderView) {
        ++g_pe10s.otherView;
        return;
    }
    std::int32_t view = -1;
    std::memcpy(&view, renderView + render_view_object::kViewIndex, sizeof(view));
    std::uint32_t frame = 0;
    std::memcpy(&frame, context + kPostProcessBackendFrame, sizeof(frame));
    std::int32_t index = 0;
    std::memcpy(&index, context + kPostProcessExposureIndex, sizeof(index));
    const auto parity = static_cast<std::int32_t>(frame & 1u);
    if ((view == 0 || view == 1) && instantBlock && instantBlock[kBlockInstantExposure] != std::byte{0}) {
        ++g_pe10s.instant[view];
        const std::uint64_t n = ++g_instantTotal;
        if (n <= kInstantLogged) {
            EVR_LOG(
                "%s: view %d adapts its exposure at once (the render's instant flag), backend frame %u (%llu "
                "so far%s)",
                kPeTag, view, frame, static_cast<unsigned long long>(n),
                n == kInstantLogged ? "; later ones are counted in the 10 s lines only" : "");
        }
    }
    if (view == 0) {
        ++g_pe10s.view0;
        if (index != parity) {
            ++g_pe10s.view0OtherParity;
        }
        g_view0Frame.store(frame, std::memory_order_relaxed);
        g_view0Seen.store(true, std::memory_order_release);
        logParallelEyesCounters();
        return;
    }
    if (view != 1) {
        ++g_pe10s.otherView;
        return;
    }
    ++g_pe10s.view1;
    if (index != parity) {
        ++g_pe10s.view1EngineDiffers;
    }
    if (g_view0Seen.load(std::memory_order_acquire)) {
        const std::uint32_t view0Frame = g_view0Frame.load(std::memory_order_relaxed);
        ++(frame == view0Frame        ? g_pe10s.view1FrameSame
           : frame == view0Frame + 1u ? g_pe10s.view1FrameAhead
                                      : g_pe10s.view1FrameOther);
    }
    std::byte* block = nullptr;
    std::memcpy(&block, context + kPostProcessBlock, sizeof(block));
    const bool unskipped = block && block[kBlockSkipExposureUpdate] == std::byte{0};
    if (unskipped) {
        ++g_pe10s.view1Unskipped;
    } else if (block) {
        const std::int32_t before = g_view1SkipIndex.exchange(index, std::memory_order_relaxed);
        if (before >= 0 && before != index) {
            ++g_pe10s.view1UpdateSeen;
        }
    }
    const parallel_eyes::Exposure mode = g_peMode.load(std::memory_order_relaxed);
    if (mode == parallel_eyes::Exposure::Engine) {
        return;
    }
    const std::int32_t chosen = mode == parallel_eyes::Exposure::Prev ? parity ^ 1 : parity;
    std::memcpy(context + kPostProcessExposureIndex, &chosen, sizeof(chosen));
    if (unskipped) {
        block[kBlockSkipExposureUpdate] = std::byte{1};
    }
}

// ---- Auto-exposure index (render-view job, after the engine stored its choice) ----

void onExposureIndex(const HookRegisters& regs) {
    if (g_pe.load(std::memory_order_acquire)) {
        // Route S does not run on an engine Parallel Eye Rendering changed; after a multiplayer guard trip
        // view 1 is no longer touched, and view 0 keeps the engine's index throughout.
        if (parallelEyesTouch()) {
            onParallelEyesExposureIndex(reinterpret_cast<std::byte*>(regs.rsi),
                                        reinterpret_cast<const std::byte*>(regs.r14));
        }
        return;
    }
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

bool installOnce(const char* tag) {
    std::call_once(g_once, [tag] {
        if (!mp_guard::allowsGameTouch()) {
            EVR_LOG("%s: the multiplayer guard is not armed; the auto-exposure index is left to the engine",
                    tag);
            return;
        }
        GameImage image;
        const std::byte* site = locateGameImage(image, tag) ? locateExposureSite(image, tag) : nullptr;
        std::string error;
        if (!site || !installMidHook(const_cast<std::byte*>(site), &onExposureIndex, error)) {
            EVR_LOG("%s: auto-exposure index hook not installed%s%s", tag, error.empty() ? "" : ": ",
                    error.c_str());
            return;
        }
        g_hooked.store(true, std::memory_order_release);
        EVR_LOG("%s: auto-exposure index hooked at RVA 0x%X", tag, image.rva(site));
    });
    return g_hooked.load(std::memory_order_acquire);
}

} // namespace

bool installExposureHook(bool exposureOnce) {
    g_exposureOnce.store(exposureOnce, std::memory_order_release);
    return installOnce(kTag);
}

bool installParallelEyesExposureHook(parallel_eyes::Exposure mode) {
    g_peMode.store(mode, std::memory_order_relaxed);
    g_pe.store(true, std::memory_order_release);
    const bool hooked = installOnce(kPeTag);
    EVR_LOG("%s: %s", kPeTag,
            hooked ? peModeText(mode)
                   : "NOT hooked: view 1 reads auto-exposure image 0 every frame (the eyes may differ while "
                     "exposure changes)");
    return hooked;
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
    EVR_LOG(
        "%s: per-eye TAA %s; auto-exposure index per eye %s, eye R %s; scattering history per eye %s; SSDO "
        "history per eye %s",
        kTag,
        taaRequested() ? "requested (the exposure index is held from its first stereo tick)"
                       : "off (ETERNALVR_STEREO_TAA=0)",
        !hooked                          ? "NOT hooked"
        : exposureOnce || taaRequested() ? "hooked"
                                         : "hooked, left to the engine (one chain with eye L)",
        !exposureOnce ? "updates its own exposure (ETERNALVR_STEREO_EXPOSURE_ONCE=0)"
        : hooked      ? "takes eye L's exposure"
                      : "updates its own exposure (one chain with eye L)",
        scatterHooksInstalled() ? "hooked" : "off", ssdoHooksInstalled() ? "hooked" : "off");
}

ExposureCounters exposureCounters() {
    ExposureCounters c;
    c.held[0] = g_counters.held[0].load();
    c.held[1] = g_counters.held[1].load();
    c.inFlightDiffers = g_counters.inFlightDiffers.load();
    return c;
}

} // namespace evr::vkcore
