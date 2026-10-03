#pragma once

// Engine side of the native two-view stereo path (docs/VR_STEREO.md; the static findings and every
// address below are in docs/rig-findings/stereo-reentry.md, Steam build 25216728).
//
// id Tech 7 renders every screen view of a frame through its own idRenderView: its own latch, matrices,
// Umbra request and render-list entry, all from the same game tick. The hooks here give a frame two
// side-by-side screen views, one per eye:
//
// - Layout: the screen-view build (RVA 0x17E8740) reads its layout from frameBuilder + 0x2A70 just
//   after it asked the game system for the current world. A hook at that read points it at a two-view
//   side-by-side table in this DLL for a world that has a second render view.
// - Second render view: each idRenderWorldLocal constructs one idRenderView, and RenderViewForIndex(1)
//   returns null. The layout hook gives the current world a second one (the engine's own allocator and
//   constructor) and the world vtable's RenderViewForIndex returns it for index 1; a hook in the world
//   destructor frees it. The world's own view list stays at one view.
// - One view slot: per-view GPU and job state (the device context's view slot, the renderer's per-view
//   block, the world's visibility context) is indexed by idRenderView::viewIndex, and this build has room
//   for one (r_maxRenderViews is 1 and the storage behind it is sized for one: stereo-reentry.md, live
//   section). The per-eye hook files the second screen view under slot 0 as well, so the eyes render one
//   after the other through the same slot.
// - Per-eye view: a hook in the screen-views loop (RVA 0x1C754BC), after the engine copied a screen
//   view's renderView_t into its idRenderView and before the latch, the Umbra request and the render,
//   lets the sink turn that view into one eye. A second hook after the latch (RVA 0x1C75772) reads what
//   the engine built from it.

#include <cstddef>
#include <cstdint>

namespace evr::vkcore {

// renderView_t fields used by the stereo path (type info of build 25216728; see view_hook.hpp for the
// others).
namespace stereo_view_fields {
inline constexpr std::size_t kDiscontinuousViewPosition = 0xC; // bool
inline constexpr std::size_t kForceFullResolution = 0x11;      // bool
inline constexpr std::size_t kInhibitModelFovScale = 0x13;
inline constexpr std::size_t kWeaponFovX = 0x30;
inline constexpr std::size_t kWeaponFovY = 0x34;
inline constexpr std::size_t kExplicitProjection = 0x50; // idRenderMatrix, 16 floats row-major
inline constexpr std::size_t kUseExplicitProjection = 0x90;
inline constexpr std::size_t kSubSampleIndex = 0x652;
inline constexpr std::size_t kUpsamplerSubSampleIndex = 0x654;
inline constexpr std::size_t kSkipAutoExposureUpdate = 0x74C; // bool
// bool: the TAA pass treats the view's history as invalid for its next three renders (read at RVA
// 0x1C57081 into the post-process context, docs/rig-findings/stereo-temporal.md).
inline constexpr std::size_t kDisableTssaaNextFewFrames = 0x750;
} // namespace stereo_view_fields

// idRenderView fields (the view's renderView_t `g` is at +0).
namespace render_view_object {
inline constexpr std::size_t kViewIndex = 0x28990;
inline constexpr std::size_t kLatched = 0x289D0;                // renderView_t r, the latched copy
inline constexpr std::size_t kProjection = 0x29340;             // projectionMatrix built by the latch
inline constexpr std::size_t kCenteredViewProjection = 0x296B0; // built by the latch (RVA 0x1CE1400)
inline constexpr std::size_t kOwningWorld = 0x29918;
inline constexpr std::size_t kSize = 0x29950;
} // namespace render_view_object

class StereoHookSink {
public:
    virtual ~StereoHookSink() = default;
    // Screen view `viewIndex` is about to be latched: `renderView` is its idRenderView (renderView_t at
    // +0, which the sink may rewrite), `firstViewG` the first screen view's renderView_t (its TAA jitter
    // index is the one the engine set this frame). Runs on a render job thread.
    virtual void onEyeView(std::byte* renderView, int viewIndex, const std::byte* firstViewG) = 0;
    // The latch built this idRenderView's matrices from what onEyeView left. Route S repairs the centred
    // matrix here (presenter_seq.cpp); everything else only reads.
    virtual void onEyeLatched(std::byte* renderView, int viewIndex) = 0;
};

// ETERNALVR_MODE=stereo only runs as one of the live experiments of docs/rig-findings/stereo-reentry.md,
// chosen with ETERNALVR_STEREO_EXPERIMENT: this build's renderer keeps per-view state for one render
// view per frame (docs/VR_STEREO.md), so two views crash it and there is no stereo to fall back on.
enum class StereoExperiment {
    None,     // not asked for, or ETERNALVR_MODE=stereo without an experiment: head-tracked mono
    LeftEye,  // "left-eye" (E2, E3): the game's one view rendered as the left eye (explicit projection)
    TwoViews, // "two-views" (E4): two side-by-side screen views sharing view slot 0; crashes this build
};
// The experiment ETERNALVR_STEREO_EXPERIMENT names; without it TwoViews when Parallel Eye Rendering is
// installed (view_slots.hpp: the two views each with their own per-view storage), else None.
StereoExperiment stereoExperimentFromEnv();

struct StereoHookStatus {
    bool eyeView = false;  // per-eye hook and post-latch hook
    bool twoViews = false; // layout hook, second render view, world destructor hook
};

// Locates and installs the hooks once per process (later calls return the first result). With
// twoViews false only the per-eye hooks are installed: the game keeps one view (live experiment E2).
StereoHookStatus installStereoHooks(StereoHookSink* sink, bool twoViews);

// The two-view experiment: installs the hooks on a thread of its own as soon as the game's device exists,
// before its first map loads (a map's visibility contexts are made at load time), and asks for two
// views. The XR worker's installStereoHooks call later only sets the sink.
void startStereoHooksEarly();

// Replaces the sink; waits for callbacks in progress.
void setStereoHookSink(StereoHookSink* sink);

// Asks for the two-view layout (true) or the game's own (false) from the next frame on.
void requestTwoViews(bool enabled);

// Engine counters for live experiment E7: the render frame counter (renderSystem + 0x10) and the
// backend frame counter (renderBackend + 0xB0); 0 when unknown.
struct EngineFrameCounters {
    std::uint32_t renderFrames = 0;
    std::uint32_t backendFrames = 0;
};
EngineFrameCounters readEngineFrameCounters();

} // namespace evr::vkcore
