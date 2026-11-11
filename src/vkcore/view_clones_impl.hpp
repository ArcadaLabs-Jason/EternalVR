#pragma once

// Internal to Parallel Eye Rendering's clones (view_clones.hpp), shared by view_clones.cpp (their state, the
// prepare and install, each frame's check) and view_clone_build.cpp (a build of the clones and their map):
// the engine's slots and fields the clones are made for, and the state both use.

#include "vkcore/view_clone_map.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace evr::vkcore::view_clone {

constexpr const char* kTag = "view-clones";

// ---- Build 25216728 (RVAs) ----

constexpr std::uint32_t kFinalTarget = 0x66E3208;

// The global region the engine's resize (0x1CD9D20) walks: render target objects and images.
constexpr std::uint32_t kRegionStart = 0x66E2F70;
constexpr std::uint32_t kRegionEnd = 0x66E3410;
// 0x66E3308..0x66E3360: the depth downsample chain (_viewdepthdownres*: 2x..32x max z, min z, reverse 16f)
// the depth pass's compute 0x1C73530 writes; shared, each view culled against the other's depth (tile-shaped
// holes in both eyes in e1m3, rig run c3d1). The shadow atlas (0x66E2E70, 8192x8192) is not cloned: view 1
// shades with view 0's (view_one_passes.cpp).
constexpr std::uint32_t kTargetSlots[] = {
    0x66E31F8, 0x66E3200, 0x66E3208, 0x66E3210, 0x66E3218, 0x66E3220, 0x66E3228, 0x66E3230, 0x66E3238,
    0x66E3240, 0x66E3248, 0x66E3250, 0x66E3258, 0x66E3260, 0x66E3268, 0x66E3270, 0x66E3278, 0x66E3280,
    0x66E3288, 0x66E3290, 0x66E3298, 0x66E32A0, 0x66E32A8, 0x66E32B0, 0x66E32B8, 0x66E32C0, 0x66E32C8,
    0x66E32D0, 0x66E32D8, 0x66E32E0, 0x66E32E8, 0x66E32F0, 0x66E32F8, 0x66E3300, 0x66E3308, 0x66E3310,
    0x66E3318, 0x66E3320, 0x66E3328, 0x66E3330, 0x66E3338, 0x66E3340, 0x66E3348, 0x66E3350, 0x66E3358,
    0x66E3360, 0x66E3368, 0x66E3370, 0x66E3378, 0x66E3400};
// 0x66E30E8 (256x256, 6 mips) and the device context's +0x338..+0x360 (kDcCloneFields) are written by both
// views' opaque passes in e1m3 (shared-writes report, rig run c3d5); not seen in e1m2's opening.
constexpr std::uint32_t kImageSlots[] = {
    0x66E2F78, 0x66E2F80, 0x66E2F88, 0x66E2F90, 0x66E2F98, 0x66E2FA0, 0x66E2FA8, 0x66E2FC8, 0x66E30A0,
    0x66E30A8, 0x66E30B0, 0x66E30D8, 0x66E30E0, 0x66E30F0, 0x66E30F8, 0x66E3100, 0x66E3108, 0x66E3110,
    0x66E3118, 0x66E3180, 0x66E3188, 0x66E3190, 0x66E3198, 0x66E31C0, 0x66E30E8};
// Device context fields: the scene target and its neighbours (targets or images, told apart at run time), the
// geometry decal id target 0x5E0 (else view 1 draws its ids into view 0's image), viewDepth, and the
// ray-traced reflection images 0x498..0x4D8 (made when ray tracing turns on): the temporal upscale picks its
// current and previous image by the frame counter's parity, the same in both views, so with one set each
// eye's history held the other eye's reflections.
constexpr std::size_t kContextFields[] = {0x230, 0x498, 0x4A0, 0x4A8, 0x4B0, 0x4B8, 0x4C0,
                                          0x4C8, 0x4D0, 0x4D8, 0x510, 0x518, 0x520, 0x528,
                                          0x530, 0x538, 0x540, 0x548, 0x5E0, 0x5E8};
// The refraction's device context fields (glass and other refractive surfaces; made by 0x1C1CD80): its
// accumulation images _refractionAccumulationBuffer<pair><update> (+0x440..+0x458, half size) and their
// targets (+0x5F0..+0x608), a pair per refraction update, and the mask tiles' pair _refractionMaskTiles32x0/1
// (+0x488/+0x490). The refraction pass (0x1C63DB0) writes [parity * 2 + update] and blends [(parity ^ 1) * 2
// + update], its last frame's; the mask pass (0x1C64360) writes the tiles [parity] and reads [parity ^ 1].
// The parity is the frame counter's (transparency context + 0xF8), the same in both views, so with one set
// the two views wrote the same images and each blended its glass with the other view's last frame. The
// frame's mask chain goes with them: _refractionMask (+0x460, its target +0x610) and its tiles
// (+0x468..+0x480), written and read by each view's mask pass. The passes read the engine's device context
// global; view 1's binds and write marks take the clones (view_clone_binds.cpp).
// ETERNALVR_TEST_VIEW_CLONE_SKIP=refract leaves them shared.
constexpr std::size_t kRefractionFields[] = {0x440, 0x448, 0x450, 0x458, 0x460, 0x468, 0x470, 0x478,
                                             0x480, 0x488, 0x490, 0x5F0, 0x5F8, 0x600, 0x608, 0x610};

// View 1's device context: a copy refreshed every frame, with the images below replaced by clones. The pass
// arguments (a store view_clone_binds.cpp maps) hand it to the render-view passes, e.g. 0x1C2A630 that builds
// the depth pyramid.
constexpr std::size_t kDcDepthPyramid = 0x2F8;
// Cloned for view 1's device context only: light scattering history (both views' opaque passes write them)
// and the depth pyramid.
constexpr std::size_t kDcCloneFields[] = {0x2D0, 0x2D8, 0x2E0, 0x2E8, kDcDepthPyramid, 0x338, 0x340,
                                          0x348, 0x350, 0x358, 0x360};
// Per-view history render targets: the accumulation buffers (+0x60..+0x80), ambient occlusion and depth of
// field accumulation (+0x550..+0x580), the screen target. Auto exposure and its luminance (+0x598..+0x5D8)
// stay shared: view 1 skips its exposure update and reads view 0's (presenter_stereo.cpp,
// exposure_hooks.cpp), as Route S's eye R does.
constexpr std::size_t kDcCloneTargets[] = {0x060, 0x068, 0x070, 0x078, 0x080, 0x550,          0x558,
                                           0x560, 0x568, 0x570, 0x578, 0x580, kDcScreenTarget};

// ---- Left out by ETERNALVR_TEST_VIEW_CLONE_SKIP (parallel_eyes_settings.hpp) ----

// slot0: slot 0's TAA targets, which the slot builder 0x1C20150 makes into the view slot (+0x8): the
// accumulation pair +0x60/+0x68 (0x1C2060A), the opaque accumulation +0x70 (0x1C20787) and distortion +0x78
// (0x1C20481). View 1 takes slot 1's through the redirected slot sites (view_slots.cpp). viewColor +0x80
// (0x1C208D9) stays cloned: view 1 binds it (the clone census of rig run pint-pe-a: `_viewcolor0`, 17.2 MB,
// 12471 image binds by view 1 with it left out). The one fixed-offset read of slot 0, 0x1CDAC6E (+0x80),
// runs once a frame after both views (0x1CDAC20, called at 0x1CDB321).
constexpr std::size_t kSlot0Targets[] = {0x060, 0x068, 0x070, 0x078};
// dof: the depth of field accumulation targets +0x568..+0x580 (over dofAccBufferFar0/1 and Near0/1 at
// +0x2A0..+0x2B8, made by 0x1C1CD80) and the layer targets 0x66E32A0 (_dofLayerFar00, Near00; stored at
// 0x1CD43AE) and 0x66E32A8 (Far01, Near01; 0x1CD433B). The three targets after them (0x66E32B0..0x66E32C0,
// 0x1CD4421) hold images of the _viewColorScaled chain and stay.
constexpr std::size_t kDofTargets[] = {0x568, 0x570, 0x578, 0x580};
constexpr std::uint32_t kDofSlots[] = {0x66E32A0, 0x66E32A8};
// gui: the GUI target's colours (_gui); its depth, _upscaledOpaqueDepth, stays view 1's (view 1's setup
// stores it into its block, 0x1C56D2E).
constexpr std::uint32_t kGuiTarget = 0x66E31F8;
constexpr std::uint32_t kFlareSlots[] = {0x66E3378};                 // flares: _cineLensflares (0x1CD51FE)
constexpr std::uint32_t kMotionBlurSlots[] = {0x66E32D0, 0x66E32D8}; // mblur: _velocityTileMax0/1 (0x1CD2538)
// Never left out, besides view 1's final image and screen target: its own water simulation (spectrum and
// normals 0x66E30D8..0x66E30E8; the device context's displacement, SSR accumulation and verts +0x338..
// +0x360), on which view 1's water work relies.
constexpr std::uint32_t kKeptSlots[] = {kFinalTarget, 0x66E30D8, 0x66E30E0, 0x66E30E8};
constexpr std::size_t kKeptFields[] = {kDcScreenTarget, 0x338, 0x340, 0x348, 0x350, 0x358, 0x360};

// The engine's resize 0x1CDD3C0 (called by frame begin 0x1CD9750 at 0x1CD9906, before the frame's views are
// dispatched): when a size changed it waits for the device to be idle (`call 0x1C32A80` at 0x1CDD56A, its
// vkDeviceWaitIdle), then resizes the device context (0x1C21600 at 0x1CDD67E) and the global targets and
// images (0x1CDD6D0 at 0x1CDD6A7), whose purges free the images' state blocks at once. No job of the frame
// before marks an image there, or the engine's own resize would free a state block under it: the clones are
// purged there and made again by the next build (view_clone_make.hpp). Hooked after the call, on `mov sil,
// 1`.
constexpr std::uint32_t kResizeIdle = 0x1CDD56F;
constexpr std::uint32_t kResizeIdleCall = 0x1CDD56A;
constexpr std::uint32_t kDeviceIdle = 0x1C32A80;

// ---- State (view_clones.cpp) ----

extern const std::byte* g_base;
extern std::byte* g_dc1;
extern std::atomic<const Map*> g_map;
extern int g_builds;
extern bool g_stableNames;
extern std::uint64_t g_builtAt;
extern std::uint64_t g_censusAt;
extern std::byte* g_shadow;

void refreshDc1(const Map& map);

// ---- The build (view_clone_build.cpp) ----

bool sourcesChanged();
const Clone* goneClone(const Map& map, const char*& why);
bool build();

} // namespace evr::vkcore::view_clone
