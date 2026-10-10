// Parallel Eye Rendering: view 1's own passes where the engine has one for the frame (view_clone_map.hpp):
// its screen pass, its environment, its light scattering's wait for its volumes, its shadow atlas work (left
// out: it shades with view 0's atlas) and its light and decal tile list pool. Each part can be turned off
// for an experiment with ETERNALVR_TEST_VIEW_OFF (parallel_eyes_settings.hpp).

#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/view_clone_map.hpp"
#include "vkcore/view_slots.hpp"
#include "vkcore/view_snapshot.hpp"
#include "vkcore/view_swap_guard.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace evr::vkcore::view_clone {

namespace {

constexpr const char* kTag = "view-clones";

std::uintptr_t context1() {
    return reinterpret_cast<std::uintptr_t>(viewSlotsContext1());
}

// ---- View 1's screen pass ----

// The screen pass 0x1CDF6E0 (tone map / gamma) runs once per view on contexts that are not view 1's table
// contexts. It is a compute dispatch: its output is the screen target dc+0x508 ('_swapchain0', r14) whose
// colour image it keeps at [rsp + 0x60] and binds as a parameter at 0x1CDFC4A; it binds r14 itself at
// 0x1CDF83E. Its source is the final target 0x66E3208's colour, bound at 0x1CDFC28. Both views tone-mapped
// view 0's source into the one screen image. View 1's pass (entry r12 at the target bind, [rsp + 0x38]
// later; view index +0x20) takes view 1's final image and writes view 1's clone of the screen target, the
// image the eye copy gives eye 1 (presenter_eyes.hpp). On with the screen-pass eye copy (the default).
constexpr std::uint32_t kScreenPassBindReturn = 0x1CDF843;
constexpr std::uint32_t kScreenPassSourceReturn = 0x1CDFC2D;
constexpr std::uint32_t kScreenPassOutputReturn = 0x1CDFC4F; // its output's bind ([rsp + 0x60] + 0xC8)
constexpr std::size_t kScreenPassOutput = 0x60;              // the caller's stack: the output image
constexpr std::size_t kScreenPassEntry = 0x38;               // the caller's stack: the render-list entry
constexpr std::size_t kEntryViewIndex = 0x20;
constexpr std::size_t kEntryWorld = 0x30; // null on loading frames, when a map load may free the clones
bool g_screenPass = false;
std::atomic<std::uint64_t> g_screenPassSwaps[2]{}; // view 1's screen pass: target, source
std::atomic<std::uint64_t> g_screenPassDead[2]{};  // not swapped: the clone had no GPU image

// A clone the engine freed (a map load frees unreferenced scratch images) has no GPU image at +0xC8 until
// the map is rebuilt; binding it crashes the dispatch's parameter resolve 0x1C33320.
bool liveImage(std::uintptr_t image) {
    return image && read<std::uintptr_t>(image + kImageHandle) != 0;
}

bool calledFrom(const HookRegisters& r, std::uint32_t returnRva) {
    return read<std::uintptr_t>(r.rsp) == baseAddress() + returnRva;
}

std::uintptr_t callerStack(const HookRegisters& r, std::size_t offset) {
    return r.rsp + 8 + offset; // the hook sits on the callee's first instruction
}

// View 1's entry with a world: without one a map load may be freeing the clones.
bool view1WithWorld(std::uintptr_t entry) {
    return entry && read<std::int32_t>(entry + kEntryViewIndex) == 1 &&
           read<std::uintptr_t>(entry + kEntryWorld);
}

// ---- View 1's light and decal tile lists ----

// The device context's tile list pool (+0x630: one buffer; +0x638..+0x670 its parts: light bitfield and list,
// probe list, light frustum work buckets, decal bitfield and list, decal work buckets, vis box work buckets)
// is sized by 0x1C20F40(device context, width, height, create) and bound only in 0x1CFB7B0, which reads it
// from the engine's device context. Both views' binning compute filled the one pool, so each lit with the
// other's lists (both views dark). View 1 gets its own: the same call on a zeroed device context beside each
// of the engine's, and view 1's binds take its parts.
constexpr std::uint32_t kPoolSize = 0x1C20F40;
constexpr std::uint32_t kPoolCalls[] = {0x1C200D5, 0x1C21B4F};
constexpr std::size_t kPoolFirst = 0x630;
constexpr std::size_t kPoolLast = 0x670;
constexpr std::size_t kBufferHandle = 0x18; // what the binds pass for a buffer
using PoolSizeFn = void (*)(void* dc, std::int32_t width, std::int32_t height, bool create);
std::byte* g_pool1 = nullptr; // view 1's device context for the pool only (zeroed but the pool fields)
std::atomic<std::uint64_t> g_poolBinds{0};

// At the engine's call (rcx = its device context, which the global may not hold yet while it is being made;
// edx = width, r8d = height, r9b = create).
void onPoolSize(HookRegisters& r) {
    const std::uintptr_t dc = deviceContext();
    if (!g_pool1 || (dc && r.rcx != dc) || !viewSlotsActive() || !mp_guard::allowsGameTouch()) {
        return;
    }
    const bool create =
        (r.r9 & 0xFF) != 0 || !read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(g_pool1) + kPoolFirst);
    reinterpret_cast<PoolSizeFn>(const_cast<std::byte*>(base() + kPoolSize))(
        g_pool1, static_cast<std::int32_t>(r.rdx), static_cast<std::int32_t>(r.r8), create);
    EVR_LOG("%s: view 1's tile list pool sized for %dx%d", kTag, static_cast<std::int32_t>(r.rdx),
            static_cast<std::int32_t>(r.r8));
}

// r8 = a buffer handle of the engine's pool bound on view 1's parameter block: view 1's part instead. True
// when r8 is one of the pool's (handled).
bool swapPoolBind(HookRegisters& r) {
    const std::uintptr_t dc = deviceContext();
    if (!dc) {
        return false;
    }
    for (std::size_t off = kPoolFirst; off <= kPoolLast; off += 8) {
        const auto engine = read<std::uintptr_t>(dc + off);
        if (!engine || r.r8 != engine + kBufferHandle) {
            continue;
        }
        const auto own = read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(g_pool1) + off);
        if (own && isView1Context(r.rcx, true)) {
            r.r8 = own + kBufferHandle;
            g_poolBinds.fetch_add(1, std::memory_order_relaxed);
        }
        return true;
    }
    return false;
}

// ---- View 1's light scattering volumes ----

// The light scattering pass 0x1C70420 binds 'lightscatteringnumvolumes' (0x1C70744) from the volume count at
// [arguments + 0xD0] (the view's block + 0x184E94), which the per-view setup clears each frame (0x1C5F5E8)
// and the volume filler 0x1D00010 (a job of the render-view main 0x1CFA640) stores. View 1's binning waits
// for view 0's (view_binning.cpp), which delays view 1's filler, so view 1's pass mostly read 0 volumes:
// no haze in eye 1 (e1m3). At its frame check 0x1C705A7 view 1's pass waits, at most 3 ms, for a count
// when view 0's pass had volumes.
constexpr std::uint32_t kScatterFrameCheck = 0x1C705A7;
constexpr std::size_t kScatterVolumes = 0xD0;
std::atomic<std::int32_t> g_view0Volumes{0};
std::atomic<std::uint64_t> g_volumeWaits[3]{}; // not needed, waited, timed out

void onScatterVolumes(const HookRegisters& r) {
    const auto volumes = read<std::uintptr_t>(r.r14 + kScatterVolumes);
    if (!volumes || !parallelEyesTouch()) {
        return;
    }
    const auto* count = reinterpret_cast<const volatile std::int32_t*>(volumes);
    if (!isView1Context(r.rsi, false)) {
        g_view0Volumes.store(*count, std::memory_order_relaxed);
        return;
    }
    if (*count != 0 || g_view0Volumes.load(std::memory_order_relaxed) == 0) {
        g_volumeWaits[0].fetch_add(1, std::memory_order_relaxed);
        return;
    }
    LARGE_INTEGER frequency;
    LARGE_INTEGER start;
    LARGE_INTEGER now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    do {
        YieldProcessor();
        QueryPerformanceCounter(&now);
    } while (*count == 0 && (now.QuadPart - start.QuadPart) * 1000 < frequency.QuadPart * 3);
    g_volumeWaits[*count != 0 ? 1 : 2].fetch_add(1, std::memory_order_relaxed);
}

// ---- View 1's environment ----

// The game fills only the world's first render view's environment: 0x18DFC40 (rdi = world, rsi = its
// environment [world + 0x71DAE0]) calls 0x1CED520(environment, view, view + 0x28A64 (the view origin, an
// input: it picks the environment volumes around the camera), view + 0x28998 (the sun), view + 0x289C0 (the
// render parameter set: sky lighting, fog...), view + 0x289B0, view + 0x28D80). The second render view kept
// an empty set: no sky lighting (lighting flag bit 0, key 0x3982650) and no fog in eye 1 (e1m3). After the
// call (0x18DFCDE, the game thread) the same call fills view 1's.
constexpr std::uint32_t kEnvironmentFilled = 0x18DFCDE;
constexpr std::uint32_t kEnvironmentFill = 0x1CED520;
constexpr std::size_t kWorldRenderViewForIndex = 0x118; // vtable slot; index 1 = the second render view
using EnvironmentFillFn = void (*)(
    void* environment, void* view, void* origin, void* sun, void* parms, void* field289B0, void* field28D80);
using RenderViewForIndexFn = std::byte* (*)(void* world, int index);
std::atomic<std::uint64_t> g_view1Environments{0};

void onEnvironmentFilled(const HookRegisters& r) {
    auto* world = reinterpret_cast<void*>(r.rdi);
    if (!world || !r.rsi || !viewSlotsActive() || !mp_guard::allowsGameTouch()) {
        return;
    }
    const auto vtable = read<std::uintptr_t>(r.rdi);
    const auto forIndex =
        reinterpret_cast<RenderViewForIndexFn>(read<std::uintptr_t>(vtable + kWorldRenderViewForIndex));
    std::byte* view0 = forIndex(world, 0);
    std::byte* view1 = forIndex(world, 1);
    if (!view1 || view1 == view0) {
        return;
    }
    reinterpret_cast<EnvironmentFillFn>(const_cast<std::byte*>(base() + kEnvironmentFill))(
        reinterpret_cast<void*>(r.rsi), view1, view1 + 0x28A64, view1 + 0x28998, view1 + 0x289C0,
        view1 + 0x289B0, view1 + 0x28D80);
    if (g_view1Environments.fetch_add(1, std::memory_order_relaxed) == 0) {
        EVR_LOG("%s: view 1's environment filled (render view %p, view 0's %p)", kTag,
                static_cast<void*>(view1), static_cast<void*>(view0));
    }
}

// ---- Shadows ----

// View 1 skips its whole shadow atlas pass (0x1CA2610, rcx = its shadow block, render context +0x5226F8: the
// cached static tile copies within the atlas, the tile passes and draws) by resuming at a plain `ret`
// (0x1CBB5E7), and shades with view 0's atlas (shadow maps do not depend on the view). With only its tile
// draws left out, view 1's tile passes and copies still wrote into the one atlas: both eyes lost shadows
// (rig runs cpe2 vs csk6, ccl1). Its tile draw 0x1CA3A30 (`call 0x1C32750` at 0x1CA3CB3, rcx = the command
// context) is left out as well: drawing its tiles into the one atlas left view 1 dark (rig runs cg6a, cvs4).
constexpr std::uint32_t kAtlasPass = 0x1CA2610;
constexpr std::uint32_t kPlainRet = 0x1CBB5E7;
constexpr std::uint32_t kTileDraw = 0x1CA3CB3;
constexpr std::uint32_t kAfterTileDraw = 0x1CA3CB8;
constexpr int kShadowCellView1 = 6 * 4 + 1; // command table cell: category 6 (cluster setup shadows), slot 1
std::atomic<std::uint64_t> g_atlasSkips{0};
std::atomic<std::uint64_t> g_tileDrawSkips{0};

void onAtlasPass(HookRegisters& r) {
    const std::uintptr_t c1 = context1();
    if (c1 && r.rcx == c1 + kShadowBlock && parallelEyesTouch()) {
        r.resumeAt = baseAddress() + kPlainRet;
        g_atlasSkips.fetch_add(1, std::memory_order_relaxed);
    }
}

void onTileDraw(HookRegisters& r) {
    const auto* table = reinterpret_cast<const std::uintptr_t*>(base() + kCommandTable);
    if (r.rcx && r.rcx == table[kShadowCellView1] && parallelEyesTouch()) {
        r.resumeAt = baseAddress() + kAfterTileDraw;
        g_tileDrawSkips.fetch_add(1, std::memory_order_relaxed);
    }
}

bool poolOn() {
    return partOn(parallel_eyes::kBinds) && partOn(parallel_eyes::kPool); // the pool's binds go through them
}

bool installPool() {
    for (const std::uint32_t rva : kPoolCalls) {
        if (!hookAt(rva, &onPoolSize, "tile list pool")) {
            return false;
        }
    }
    return true;
}

// The Vulkan command buffer the screen pass records into: rcx at the target bind is the engine's command
// context (table cell [12, 0]; its current command buffer at [[context + 0x118]]), or null.
VkCommandBuffer screenPassCommandBuffer(const HookRegisters& r) {
    constexpr std::size_t kContextCommandBuffer = 0x118;
    const auto* table = reinterpret_cast<const std::uintptr_t*>(base() + kCommandTable);
    for (int c = 0; c < kTableCategories * kTableSlots; ++c) {
        if (table[c] && table[c] == r.rcx) {
            const auto holder = read<std::uintptr_t>(r.rcx + kContextCommandBuffer);
            return holder ? read<VkCommandBuffer>(holder) : VK_NULL_HANDLE;
        }
    }
    return VK_NULL_HANDLE;
}

} // namespace

void notePassTargetBind(HookRegisters& r, const Map* map) {
    if (!g_screenPass || !calledFrom(r, kScreenPassBindReturn)) {
        return;
    }
    if (!view1WithWorld(r.r12)) {
        // View 0's screen pass: its command buffer moves the swapchain image it draws to PRESENT_SRC.
        if (r.r12 && read<std::int32_t>(r.r12 + kEntryViewIndex) == 0 && r.r14 == r.rdx) {
            view_snapshot::noteView0Pass(screenPassCommandBuffer(r));
        }
        return;
    }
    const std::uintptr_t clone = lookup(*map, r.rdx);
    if (!clone || r.r14 != r.rdx) {
        return;
    }
    const auto image = read<std::uintptr_t>(clone + kTargetColors);
    if (!liveImage(image)) {
        g_screenPassDead[0].fetch_add(1, std::memory_order_relaxed);
        return;
    }
    std::memcpy(reinterpret_cast<void*>(callerStack(r, kScreenPassOutput)), &image, sizeof(image));
    // View 0's pass of this frame was left out after a swapchain recreate (view_swap_guard.hpp): no eye 0
    // image of this frame is ever presented, so view 1's is not copied; its presents keep the last pair.
    if (!viewSwapGuardTakeView0LeftOut()) {
        view_snapshot::arm(image, screenPassCommandBuffer(r));
    }
    r.rdx = clone;
    r.r14 = clone;
    g_screenPassSwaps[0].fetch_add(1, std::memory_order_relaxed);
    countUse(*map, clone, clone_census::kTargetBinds);
}

bool screenPassOutputBind(const HookRegisters& r) {
    return g_screenPass && calledFrom(r, kScreenPassOutputReturn);
}

bool swapPassImageBind(HookRegisters& r, const Map* map) {
    if (g_pool1 && swapPoolBind(r)) {
        return true;
    }
    // The screen pass's source bind (0x1C53330 called from 0x1CDFC28, r8 = image + 0xC8).
    if (!g_screenPass || !map || !map->finalImage || !calledFrom(r, kScreenPassSourceReturn)) {
        return false;
    }
    if (!view1WithWorld(read<std::uintptr_t>(callerStack(r, kScreenPassEntry)))) {
        return true;
    }
    if (!liveImage(map->finalImage)) {
        g_screenPassDead[1].fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    r.r8 = map->finalImage + kImageHandle;
    g_screenPassSwaps[1].fetch_add(1, std::memory_order_relaxed);
    countUse(*map, map->finalImage, clone_census::kImageBinds);
    return true;
}

bool prepareViewOnePasses() {
    if (partOn(parallel_eyes::kShadows) && base()[kPlainRet] != std::byte{0xC3}) {
        EVR_LOG("%s: RVA 0x%X is not a `ret`", kTag, kPlainRet);
        return false;
    }
    if (!poolOn()) {
        return true;
    }
    for (const std::uint32_t rva : kPoolCalls) {
        std::int32_t rel = 0;
        std::memcpy(&rel, base() + rva + 1, sizeof(rel));
        if (base()[rva] != std::byte{0xE8} || rva + 5 + rel != kPoolSize) {
            EVR_LOG("%s: RVA 0x%X is not the tile list pool's sizing call", kTag, rva);
            return false;
        }
    }
    g_pool1 =
        static_cast<std::byte*>(VirtualAlloc(nullptr, kDcSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    return g_pool1 != nullptr;
}

bool installViewOnePasses() {
    const parallel_eyes::Settings& s = parallelEyesSettings();
    g_screenPass = s.eyeCopy == parallel_eyes::EyeCopy::Screen && partOn(parallel_eyes::kScreen);
    if (partOn(parallel_eyes::kEnv) &&
        !watchAt(kEnvironmentFilled, &onEnvironmentFilled, "view 1 environment")) {
        return false;
    }
    if (partOn(parallel_eyes::kVolumes) &&
        !watchAt(kScatterFrameCheck, &onScatterVolumes, "light scattering volume")) {
        return false;
    }
    if (partOn(parallel_eyes::kShadows) && (!hookAt(kTileDraw, &onTileDraw, "view 1 shadow tile draw") ||
                                            !hookAt(kAtlasPass, &onAtlasPass, "view 1 shadow atlas pass"))) {
        return false;
    }
    return !poolOn() || installPool();
}

void reportPasses() {
    EVR_LOG(
        "%s: view 1's screen pass took its target %llu, source %llu (no GPU image: %llu, %llu); tile list "
        "pool binds %llu; light scattering volumes ready %llu, waited for %llu, gave up %llu; shadow atlas "
        "passes skipped %llu, tile draws %llu",
        kTag, static_cast<unsigned long long>(g_screenPassSwaps[0].load()),
        static_cast<unsigned long long>(g_screenPassSwaps[1].load()),
        static_cast<unsigned long long>(g_screenPassDead[0].load()),
        static_cast<unsigned long long>(g_screenPassDead[1].load()),
        static_cast<unsigned long long>(g_poolBinds.load()),
        static_cast<unsigned long long>(g_volumeWaits[0].load()),
        static_cast<unsigned long long>(g_volumeWaits[1].load()),
        static_cast<unsigned long long>(g_volumeWaits[2].load()),
        static_cast<unsigned long long>(g_atlasSkips.load()),
        static_cast<unsigned long long>(g_tileDrawSkips.load()));
}

} // namespace evr::vkcore::view_clone
