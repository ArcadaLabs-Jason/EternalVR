#include "vkcore/view_clones.hpp"

#include "vkcore/log.hpp"
#include "vkcore/runtime_cvars.hpp"
#include "vkcore/view_clone_make.hpp"
#include "vkcore/view_clone_map.hpp"
#include "vkcore/view_slots.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace evr::vkcore {

namespace view_clone {

namespace {

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

constexpr std::size_t kDcDepthPyramidViews = 0x300; // one per mip, from 0x1C48E30
constexpr int kDepthPyramidMips = 5;
constexpr std::uint32_t kImageMipView = 0x1C48E30; // (image, mip)
using ImageMipViewFn = void* (*)(void* image, int mip);

const std::byte* g_base = nullptr;
std::byte* g_dc1 = nullptr;             // view 1's device context
std::atomic<const Map*> g_map{nullptr}; // published once built

struct Source {
    std::uintptr_t at;    // where the engine keeps it
    std::uintptr_t value; // the object
    std::int32_t width;   // its size when the clones were made
    std::int32_t height;
};
std::vector<Source> g_sources; // render thread only
int g_builds = 0;
// Changes of the engine's targets before the clones turn off (the first set counts as one). With stable names
// a rebuild leaves only its render target objects behind (about 40 KB of CPU memory); with a new set of names
// each build (ETERNALVR_TEST_VIEW_CLONE_NAMES=build) it left the whole old set allocated until a map load.
constexpr int kMaxBuilds = 32;
constexpr int kMaxBuildsNewNames = 8;
int g_remakes = 0; // builds after the engine freed a clone
constexpr int kMaxRemakes = 64;
int g_forced = 0;            // builds ETERNALVR_TEST_VIEW_CLONE_REBUILD asked for
bool g_stableNames = false;  // stable names, with the resize hook that purges (prepareViewClones)
std::uint64_t g_builtAt = 0; // GetTickCount64 at the last build
std::atomic<bool> g_stopped{false};
std::uint64_t g_nextReport = 0;
std::uint64_t g_censusAt = 0; // the census of the last build is reported from then on (0: reported)
constexpr std::uint64_t kCensusAfterMs = 120000;
bool g_on = false;
std::byte* g_shadow = nullptr; // the global region's shadow

template <typename T>
T at(std::uint32_t rva) {
    return reinterpret_cast<T>(const_cast<std::byte*>(g_base + rva));
}

// View 1's device context for this frame: the engine's, with the clones in place.
void refreshDc1(const Map& map) {
    const std::uintptr_t dc = deviceContext();
    if (!g_dc1 || !dc || map.dcFields.empty()) {
        return;
    }
    std::memcpy(g_dc1, reinterpret_cast<const void*>(dc), kDcSize);
    for (const auto& [offset, value] : map.dcFields) {
        std::memcpy(g_dc1 + offset, &value, sizeof(value));
    }
}

// ---- What a build leaves out ----

template <typename List, typename T>
bool contains(const List& list, T value) {
    return std::find(std::begin(list), std::end(list), value) != std::end(list);
}

// What ETERNALVR_TEST_VIEW_CLONE_SKIP leaves out of this build: the depth of field targets only while the
// layer holds r_dof 0; never kKeptSlots or kKeptFields.
struct Skips {
    std::vector<std::uint32_t> slots;
    std::vector<std::size_t> fields;
    bool gui = false;

    bool slot(std::uint32_t rva) const { return contains(slots, rva); }
    bool field(std::size_t offset) const { return contains(fields, offset); }
};

Skips skipsNow() {
    const parallel_eyes::CloneSkip& knob = parallelEyesSettings().cloneSkip;
    Skips s;
    if (knob.groups & parallel_eyes::kSkipSlot0) {
        s.fields.insert(s.fields.end(), std::begin(kSlot0Targets), std::end(kSlot0Targets));
    }
    if ((knob.groups & parallel_eyes::kSkipDof) && runtime_cvars::holds("r_dof", "0")) {
        s.fields.insert(s.fields.end(), std::begin(kDofTargets), std::end(kDofTargets));
        s.slots.insert(s.slots.end(), std::begin(kDofSlots), std::end(kDofSlots));
    } else if (knob.groups & parallel_eyes::kSkipDof) {
        EVR_LOG("%s: build %d: the depth of field clones stay: the layer does not hold r_dof 0 (or was "
                "writing its cvars)",
                kTag, g_builds);
    }
    s.gui = (knob.groups & parallel_eyes::kSkipGui) != 0;
    if (knob.groups & parallel_eyes::kSkipFlares) {
        s.slots.insert(s.slots.end(), std::begin(kFlareSlots), std::end(kFlareSlots));
    }
    if (knob.groups & parallel_eyes::kSkipMotionBlur) {
        s.slots.insert(s.slots.end(), std::begin(kMotionBlurSlots), std::end(kMotionBlurSlots));
    }
    if (knob.groups & parallel_eyes::kSkipRefraction) {
        s.fields.insert(s.fields.end(), std::begin(kRefractionFields), std::end(kRefractionFields));
    }
    for (const std::uint32_t rva : knob.slots) {
        if ((contains(kImageSlots, rva) || contains(kTargetSlots, rva)) && !contains(kKeptSlots, rva)) {
            s.slots.push_back(rva);
        } else if (g_builds == 0) {
            EVR_LOG(
                "%s: ETERNALVR_TEST_VIEW_CLONE_SKIP: 0x%X is not a slot the clones may leave out; ignored",
                kTag, rva);
        }
    }
    for (const std::uint32_t f : knob.fields) {
        if ((contains(kContextFields, f) || contains(kRefractionFields, f) || contains(kDcCloneFields, f) ||
             contains(kDcCloneTargets, f)) &&
            !contains(kKeptFields, f)) {
            s.fields.push_back(f);
        } else if (g_builds == 0) {
            EVR_LOG("%s: ETERNALVR_TEST_VIEW_CLONE_SKIP: dc+0x%X is not a field the clones may leave out; "
                    "ignored",
                    kTag, f);
        }
    }
    return s;
}

// ---- Building ----

// The object at `slotAddress` (a global slot or a device context field), watched for changes; left shared
// with `leaveOut`.
void add(Builder& b, std::uintptr_t slotAddress, Kind expected, bool leaveOut) {
    const auto value = read<std::uintptr_t>(slotAddress);
    if (leaveOut) {
        if (value) {
            b.noteShared(b.source, value);
        }
        return;
    }
    if (!value) {
        // Made later by the engine (dc+0x548 is null at the first build): watched, so its arrival rebuilds
        // the clones.
        g_sources.push_back({slotAddress, 0, 0, 0});
        return;
    }
    const Kind kind = kindOf(value);
    if (kind == Kind::None || (expected != Kind::None && kind != expected)) {
        if (g_builds == 0) {
            EVR_LOG("%s: slot %p holds %p, not a%s; left shared", kTag, reinterpret_cast<void*>(slotAddress),
                    reinterpret_cast<void*>(value),
                    expected == Kind::Image    ? "n image"
                    : expected == Kind::Target ? " target"
                                               : " target or image");
        }
        return;
    }
    std::int32_t w = 0;
    std::int32_t h = 0;
    sizeOf(value, kind, w, h);
    g_sources.push_back({slotAddress, value, w, h});
    if (kind == Kind::Target) {
        b.cloneTarget(value);
    } else {
        b.cloneImage(value);
    }
}

bool sourcesChanged() {
    for (const Source& s : g_sources) {
        const auto value = read<std::uintptr_t>(s.at);
        if (value != s.value) {
            return true;
        }
        const Kind kind = kindOf(value);
        std::int32_t w = 0;
        std::int32_t h = 0;
        if (kind != Kind::None) {
            sizeOf(value, kind, w, h);
        }
        if (w != s.width || h != s.height) {
            return true;
        }
    }
    return false;
}

// The first clone the engine has freed or reused (a map load frees unreferenced scratch images), or null.
const Clone* goneClone(const Map& map, const char*& why) {
    for (const Clone& c : map.clones) {
        if (!c.target && (why = cloneGone(c)) != nullptr) {
            return &c;
        }
    }
    return nullptr;
}

// View 1's device context: its own depth pyramid (with the pyramid's mip views) and light scattering history,
// and every field whose object has a clone.
void buildDc1(Builder& b, std::uintptr_t dc, Map& map, const Skips& skips) {
    b.group = Group::View1Context;
    for (const std::size_t f : kDcCloneFields) {
        const auto image = read<std::uintptr_t>(dc + f);
        b.source = fieldText("dc1+", f);
        if (skips.field(f)) {
            b.noteShared(b.source, image);
        } else if (kindOf(image) == Kind::Image) {
            b.cloneImage(image);
        }
    }
    for (const std::size_t f : kDcCloneTargets) {
        const auto target = read<std::uintptr_t>(dc + f);
        b.source = fieldText("dc1+", f);
        if (skips.field(f)) {
            b.noteShared(b.source, target);
        } else if (kindOf(target) == Kind::Target) {
            b.cloneTarget(target);
        } else if (g_builds == 0) {
            EVR_LOG("%s: dc+0x%zX %p is not a target; left shared", kTag, f, reinterpret_cast<void*>(target));
        }
    }
    for (std::size_t off = 0; off < kDcSize; off += 8) {
        const auto engine = read<std::uintptr_t>(dc + off);
        if (const std::uintptr_t c = engine ? b.mapped(engine) : 0) {
            map.dcFields.emplace_back(off, c);
        }
    }
    if (const std::uintptr_t c = b.mapped(read<std::uintptr_t>(dc + kDcDepthPyramid))) {
        for (int mip = 0; mip < kDepthPyramidMips; ++mip) {
            const auto view = reinterpret_cast<std::uintptr_t>(
                at<ImageMipViewFn>(kImageMipView)(reinterpret_cast<void*>(c), mip));
            map.dcFields.emplace_back(kDcDepthPyramidViews + 8 * static_cast<std::size_t>(mip), view);
        }
    }
    if (const std::uintptr_t c = b.mapped(read<std::uintptr_t>(dc + kDcScreenTarget))) {
        map.screenImage = read<std::uintptr_t>(c + kTargetColors);
    }
    map.entries.emplace_back(dc, reinterpret_cast<std::uintptr_t>(g_dc1));
}

bool build() {
    const std::uintptr_t dc = deviceContext();
    if (!dc) {
        return false;
    }
    g_sources.clear();
    const Skips skips = skipsNow();
    Builder b;
    b.build = g_builds;
    b.stableNames = g_stableNames;
    if (const auto gui = read<std::uintptr_t>(baseAddress() + kGuiTarget);
        skips.gui && kindOf(gui) == Kind::Target) {
        for (std::size_t k = 0; k < 8; ++k) {
            if (const auto image = read<std::uintptr_t>(gui + kTargetColors + 8 * k)) {
                b.sharedImages.push_back(image);
                b.noteShared(fieldText("", kGuiTarget) + ".c" + std::to_string(k), image);
            }
        }
    }
    b.group = Group::GlobalImages;
    for (const std::uint32_t s : kImageSlots) {
        b.source = fieldText("", s);
        add(b, reinterpret_cast<std::uintptr_t>(g_base + s), Kind::Image, skips.slot(s));
    }
    b.group = Group::GlobalTargets;
    for (const std::uint32_t s : kTargetSlots) {
        b.source = fieldText("", s);
        add(b, reinterpret_cast<std::uintptr_t>(g_base + s), Kind::Target, skips.slot(s));
    }
    b.group = Group::ContextFields;
    for (const std::size_t f : kContextFields) {
        b.source = fieldText("dc+", f);
        add(b, dc + f, Kind::None, skips.field(f));
    }
    for (const std::size_t f : kRefractionFields) {
        b.source = fieldText("dc+", f);
        add(b, dc + f, Kind::None, skips.field(f));
    }
    // The shadow of the global region: the engine's values with the clones in their place, and the addresses
    // of the cloned slots (the setups hand some arrays over by address) mapped into it.
    const std::size_t regionSize = kRegionEnd - kRegionStart;
    std::memcpy(g_shadow, g_base + kRegionStart, regionSize);
    auto* map = new Map;
    for (std::size_t off = 0; off < regionSize; off += 8) {
        const auto engine =
            read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(g_base + kRegionStart + off));
        if (const std::uintptr_t c = engine ? b.mapped(engine) : 0) {
            std::memcpy(g_shadow + off, &c, sizeof(c));
            map->entries.emplace_back(reinterpret_cast<std::uintptr_t>(g_base + kRegionStart + off),
                                      reinterpret_cast<std::uintptr_t>(g_shadow + off));
        }
    }
    if (g_dc1) {
        buildDc1(b, dc, *map, skips);
    }
    map->entries.insert(map->entries.end(), b.entries.begin(), b.entries.end());
    std::sort(map->entries.begin(), map->entries.end());
    map->clones = std::move(b.clones);
    map->leftOut = std::move(b.leftOut);
    const auto finalTarget = read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(g_base + kFinalTarget));
    if (const std::uintptr_t finalClone = finalTarget ? b.mapped(finalTarget) : 0) {
        map->finalImage = read<std::uintptr_t>(finalClone + kTargetColors);
    }
    EVR_LOG("%s: build %d: %d image(s), %d target(s) cloned, %d failed; %zu map entries, %zu device context "
            "field(s); view 1's final image %p, screen image %p",
            kTag, g_builds, b.images, b.targets, b.failed, map->entries.size(), map->dcFields.size(),
            reinterpret_cast<void*>(map->finalImage), reinterpret_cast<void*>(map->screenImage));
    indexClones(*map);
    logBuild(*map, g_builds);
    if (b.stableNames) {
        const int waiting = queueOrphans(b);
        EVR_LOG("%s: build %d: image clones by name: %d kept, %d allocated again, %d new, %d made beside one "
                "still allocated at another size; %d wait for the engine's next resize to be purged",
                kTag, g_builds, b.kept, b.remade, b.made, b.beside, waiting);
    } else if (g_builds > 0) {
        EVR_LOG(
            "%s: build %d: new names (ETERNALVR_TEST_VIEW_CLONE_NAMES=build); the clones made before stay "
            "allocated until a map load frees them",
            kTag, g_builds);
    }
    if (b.sharedCount) {
        EVR_LOG(
            "%s: build %d: %d object(s) left shared (ETERNALVR_TEST_VIEW_CLONE_SKIP=%s), %s not cloned: %s",
            kTag, g_builds, b.sharedCount,
            parallel_eyes::cloneSkipText(parallelEyesSettings().cloneSkip).c_str(),
            clone_census::megabytes(b.sharedBytes).c_str(), b.shared.c_str());
    }
    g_builtAt = GetTickCount64();
    g_censusAt = censusOn() ? g_builtAt + kCensusAfterMs : 0;
    refreshDc1(*map); // before the map hands it out
    g_map.store(map, std::memory_order_release);
    ++g_builds;
    return true;
}

// The engine's resize, after its device idle (kResizeIdle): every clone purged, made again at the next build.
void onResizeIdle(const HookRegisters&) {
    if (!g_on || !g_stableNames || g_stopped.load(std::memory_order_relaxed) || !parallelEyesTouch()) {
        return;
    }
    const Map* map = g_map.load(std::memory_order_acquire);
    g_map.store(nullptr, std::memory_order_release); // its images go now
    std::string waiting;
    const int purged = releaseClones(map, waiting);
    EVR_LOG("%s: the engine's resize: %d image clone(s) purged%s%s; made again at the next two-view frame",
            kTag, purged, waiting.empty() ? "" : ", of them waiting: ", waiting.c_str());
}

} // namespace

const std::byte* base() {
    return g_base;
}

const Map* currentMap() {
    return g_map.load(std::memory_order_acquire);
}

} // namespace view_clone

using namespace view_clone;

bool prepareViewClones(const std::byte* gameBase) {
    g_base = gameBase;
    if (!parallelEyesSettings().clones) {
        return true;
    }
    g_shadow = static_cast<std::byte*>(
        VirtualAlloc(nullptr, kRegionEnd - kRegionStart, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!g_shadow) {
        return false;
    }
    if (partOn(parallel_eyes::kDcCopy)) {
        g_dc1 =
            static_cast<std::byte*>(VirtualAlloc(nullptr, kDcSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!g_dc1) {
            return false;
        }
    }
    // `call 0x1C32A80` then `mov sil, 1`; else the clones keep the names of each build, as before, and a
    // build never purges.
    std::int32_t rel = 0;
    std::memcpy(&rel, g_base + kResizeIdleCall + 1, sizeof(rel));
    constexpr std::byte kAfter[] = {std::byte{0x40}, std::byte{0xB6}, std::byte{0x01}};
    const bool idle = g_base[kResizeIdleCall] == std::byte{0xE8} &&
                      kResizeIdleCall + 5 + rel == kDeviceIdle &&
                      std::memcmp(g_base + kResizeIdle, kAfter, sizeof(kAfter)) == 0;
    g_stableNames = !parallelEyesSettings().cloneBuildNames && idle;
    if (!idle) {
        EVR_LOG("%s: RVA 0x%X is not the engine's resize device idle; new names each build, as before", kTag,
                kResizeIdleCall);
    }
    return checkBinds() && prepareViewOnePasses();
}

bool installViewClones() {
    if (!parallelEyesSettings().clones) {
        EVR_LOG("%s: off (ETERNALVR_TEST_VIEW_CLONES=0): view 1 shares the engine's targets", kTag);
        return true;
    }
    if (!installBinds() || !installViewOnePasses() ||
        (g_stableNames && !watchAt(kResizeIdle, &onResizeIdle, "resize device idle"))) {
        return false;
    }
    g_on = true;
    const parallel_eyes::Settings& s = parallelEyesSettings();
    EVR_LOG("%s: on: %zu image slots, %zu target slots, %zu device context fields%s; left shared "
            "(ETERNALVR_TEST_VIEW_CLONE_SKIP): %s; census %s; %s; rebuilds asked for: %s",
            kTag, std::size(kImageSlots), std::size(kTargetSlots),
            std::size(kContextFields) + std::size(kRefractionFields),
            g_dc1 ? ", view 1's device context" : "", parallel_eyes::cloneSkipText(s.cloneSkip).c_str(),
            s.cloneCensus ? "on" : "off (ETERNALVR_TEST_VIEW_CLONE_LOG=0)",
            g_stableNames       ? "stable names, purged at the engine's resize"
            : s.cloneBuildNames ? "new names each build (ETERNALVR_TEST_VIEW_CLONE_NAMES=build)"
                                : "new names each build",
            s.cloneRebuildSeconds ? ("every " + std::to_string(s.cloneRebuildSeconds) + " s, " +
                                     std::to_string(parallel_eyes::kForcedRebuilds) + " times")
                                        .c_str()
                                  : "none");
    return true;
}

bool viewClonesPrepare() {
    if (!g_on || g_stopped.load(std::memory_order_relaxed)) {
        return false;
    }
    const Map* map = g_map.load(std::memory_order_relaxed);
    const char* why = nullptr;
    if (const Clone* gone = map ? goneClone(*map, why) : nullptr) {
        // Not counted against kMaxBuilds: each map load frees them once.
        EVR_LOG("%s: clone '%s' %p was freed or reused by the engine (%s); making the clones again", kTag,
                gone->name.c_str(), reinterpret_cast<void*>(gone->image), why);
        g_map.store(nullptr, std::memory_order_release);
        if (++g_remakes > kMaxRemakes) {
            g_stopped.store(true);
            EVR_LOG("%s: %d remakes; clones off, view 0 alone from now on", kTag, kMaxRemakes);
            return false;
        }
        return build();
    }
    const parallel_eyes::Settings& settings = parallelEyesSettings();
    if (map && !sourcesChanged()) {
        refreshDc1(*map);
        const std::uint64_t now = GetTickCount64();
        if (settings.cloneRebuildSeconds > 0 && g_forced < parallel_eyes::kForcedRebuilds &&
            now >= g_builtAt + 1000ull * static_cast<std::uint64_t>(settings.cloneRebuildSeconds)) {
            // Not counted against kMaxBuilds.
            EVR_LOG("%s: rebuild %d of %d asked for (ETERNALVR_TEST_VIEW_CLONE_REBUILD=%d)", kTag, ++g_forced,
                    parallel_eyes::kForcedRebuilds, settings.cloneRebuildSeconds);
            return build();
        }
        if (now >= g_nextReport) {
            g_nextReport = now + 60000;
            reportBinds();
            reportPasses();
            if (g_censusAt && now >= g_censusAt) {
                g_censusAt = 0;
                reportCensus(*map, g_builds - 1);
            }
        }
        return false;
    }
    if (g_builds - g_remakes - g_forced >= (g_stableNames ? kMaxBuilds : kMaxBuildsNewNames)) {
        g_stopped.store(true);
        g_map.store(nullptr, std::memory_order_release);
        EVR_LOG("%s: %d builds and the engine's targets still change; clones off, view 0 alone from now on",
                kTag, g_builds);
        return false;
    }
    return build();
}

bool viewClonesStopped() {
    return g_stopped.load(std::memory_order_relaxed);
}

void* viewClonesFinalImage() {
    const Map* map = g_map.load(std::memory_order_acquire);
    return map ? reinterpret_cast<void*>(map->finalImage) : nullptr;
}

void* viewClonesScreenImage() {
    const Map* map = g_map.load(std::memory_order_acquire);
    return map ? reinterpret_cast<void*>(map->screenImage) : nullptr;
}

} // namespace evr::vkcore
