// Parallel Eye Rendering: view 1's own stencil view of its depth clone (clone_stencil.hpp). Each clone build
// makes it with the engine's own maker and keeps it in the map; view 1's binds of the device context's
// stencil view ('viewStencilImage') take it; the engine's resize releases every one made since the last
// (without stable names they stay, clone_stencil.hpp).

#include "vkcore/clone_stencil.hpp"
#include "vkcore/log.hpp"
#include "vkcore/parallel_eyes_settings.hpp"
#include "vkcore/view_clone_map.hpp"
#include "vkcore/view_slots.hpp"
#include "vkcore/view_swap_watch.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace evr::vkcore::view_clone {

namespace {

constexpr const char* kTag = "view-clones";

// ---- Build 25216728 (RVAs) ----

constexpr std::size_t kDcDepth = 0x230;       // the device context's depth image ('_viewdepth')
constexpr std::size_t kDcStencilView = 0x438; // its stencil view: {image, VkImageView}
constexpr std::size_t kViewField = 0x8;       // a stencil view's VkImageView (a set of images: their list)
// 0x1C21600 (the device context's resize) frees the old handle and makes the new one:
// `call 0x1C4ACE0` (image, handle) at 0x1C2176F, `call 0x1C49B50` (image) at 0x1C2177B, then
// `mov [rdi + 0x438], rax`. 0x1C21B80 binds it as 'viewStencilImage': `mov r8, [rdi + 0x438]` at 0x1C21C56.
constexpr std::uint32_t kMakeView = 0x1C49B50;
constexpr std::uint32_t kFreeView = 0x1C4ACE0;
constexpr std::uint32_t kFreeCall = 0x1C2176F;
constexpr std::uint32_t kMakeCall = 0x1C2177B;
constexpr std::uint32_t kStore = 0x1C21780;
constexpr std::uint32_t kBindLoad = 0x1C21C56;
constexpr std::size_t kImageGpu = 0xE8; // an image's VkImage (the one the view is made over)
using MakeViewFn = std::uintptr_t (*)(std::uintptr_t image);
using FreeViewFn = void (*)(std::uintptr_t image, std::uintptr_t handle);

bool g_on = false; // set by checkStencilView before the hooks install, never changed after
std::mutex g_mutex;
clone_stencil::Ledger g_made; // the builds and the engine's resize, under g_mutex
bool g_failLogged = false;    // the builds only
std::atomic<std::uint64_t> g_binds{0};

bool callsAt(std::uint32_t site, std::uint32_t target) {
    std::int32_t rel = 0;
    std::memcpy(&rel, base() + site + 1, sizeof(rel));
    return base()[site] == std::byte{0xE8} && site + 5 + rel == target;
}

void release(std::uintptr_t view, std::uint32_t imageFlags) {
    alignas(8) clone_stencil::StandIn image = clone_stencil::standIn(imageFlags);
    reinterpret_cast<FreeViewFn>(const_cast<std::byte*>(base() + kFreeView))(
        reinterpret_cast<std::uintptr_t>(image.data()), view);
}

} // namespace

bool checkStencilView() {
    // `mov [rdi + 0x438], rax` and `mov r8, [rdi + 0x438]`
    constexpr std::byte kStoreBytes[] = {std::byte{0x48}, std::byte{0x89}, std::byte{0x87}, std::byte{0x38},
                                         std::byte{0x04}, std::byte{0x00}, std::byte{0x00}};
    constexpr std::byte kLoadBytes[] = {std::byte{0x4C}, std::byte{0x8B}, std::byte{0x87}, std::byte{0x38},
                                        std::byte{0x04}, std::byte{0x00}, std::byte{0x00}};
    const bool known = callsAt(kFreeCall, kFreeView) && callsAt(kMakeCall, kMakeView) &&
                       std::memcmp(base() + kStore, kStoreBytes, sizeof(kStoreBytes)) == 0 &&
                       std::memcmp(base() + kBindLoad, kLoadBytes, sizeof(kLoadBytes)) == 0;
    const bool left = (parallelEyesSettings().cloneSkip.groups & parallel_eyes::kSkipStencil) != 0;
    g_on = known && !left;
    if (!known) {
        EVR_LOG("%s: the device context's stencil view is not as known (RVA 0x%X); view 1 binds view 0's "
                "(the augment outline in eye 1 follows eye 0's demons)",
                kTag, kBindLoad);
    } else if (left) {
        EVR_LOG("%s: the device context's stencil view stays the engine's (ETERNALVR_TEST_VIEW_CLONE_SKIP="
                "stencil): view 1's augment outline follows view 0's demons",
                kTag);
    }
    return true;
}

void makeStencilView(Map& map, std::uintptr_t dc, int build) {
    const auto engineView = read<std::uintptr_t>(dc + kDcStencilView);
    const std::uintptr_t engineImage = engineView ? read<std::uintptr_t>(engineView) : 0;
    const std::uintptr_t clone = engineImage ? lookup(map, engineImage) : 0;
    const std::uintptr_t gpuImage = clone ? read<std::uintptr_t>(clone + kImageGpu) : 0;
    const clone_stencil::Plan plan = clone_stencil::plan(
        g_on, engineView, engineImage, read<std::uintptr_t>(dc + kDcDepth), clone, gpuImage);
    if (plan != clone_stencil::Plan::Make) {
        if (plan != clone_stencil::Plan::Off) {
            EVR_LOG("%s: build %d: view 1 binds the device context's stencil view %p (%s)", kTag, build,
                    reinterpret_cast<void*>(engineView), clone_stencil::planText(plan));
        }
        return;
    }
    // Never null: the maker stores into the handle it allocates right away.
    const std::uintptr_t view =
        reinterpret_cast<MakeViewFn>(const_cast<std::byte*>(base() + kMakeView))(clone);
    const auto flags = read<std::uint32_t>(clone + clone_stencil::kImageFlags);
    const auto field = read<std::uintptr_t>(view + kViewField);
    const std::uintptr_t first =
        (flags & clone_stencil::kImageSet) && field ? read<std::uintptr_t>(field) : 0;
    if (!clone_stencil::viewMade(flags, field, first)) {
        // Never bound: released now (a null view queues nothing).
        if (clone_stencil::releasable(flags, field)) {
            release(view, flags);
        }
        if (!g_failLogged) {
            g_failLogged = true;
            EVR_LOG(
                "%s: build %d: view 1's stencil view over its depth clone %p was not made (no image view); "
                "view 1 binds the engine's in this map",
                kTag, build, reinterpret_cast<void*>(clone));
        }
        return;
    }
    std::size_t waiting = 0;
    {
        std::lock_guard lock(g_mutex);
        g_made.add(view, flags);
        waiting = g_made.size();
    }
    map.stencil = {view, engineView, clone, gpuImage};
    EVR_LOG(
        "%s: build %d: view 1's stencil view %p made over its depth clone %p (the engine's %p over %p); %zu "
        "made since the engine's last resize",
        kTag, build, reinterpret_cast<void*>(view), reinterpret_cast<void*>(clone),
        reinterpret_cast<void*>(engineView), reinterpret_cast<void*>(engineImage), waiting);
}

bool swapStencilView(HookRegisters& r, const Map& map) {
    if (!map.stencil.view || r.r8 != map.stencil.engineView) {
        return false;
    }
    const std::uintptr_t dc = deviceContext();
    std::uintptr_t gpuNow = 0;
    const bool gpuRead = swap_watch::readAt(map.stencil.clone + kImageGpu, gpuNow);
    const std::uintptr_t own = clone_stencil::bindFor(
        map.stencil, r.r8, dc ? read<std::uintptr_t>(dc + kDcStencilView) : 0, gpuRead, gpuNow);
    if (!own) {
        return false;
    }
    r.r8 = own;
    g_binds.fetch_add(1, std::memory_order_relaxed);
    countUse(map, map.stencil.clone, clone_census::kImageBinds);
    return true;
}

void releaseStencilViews() {
    std::vector<clone_stencil::Made> made;
    {
        std::lock_guard lock(g_mutex);
        made = g_made.take();
    }
    for (const clone_stencil::Made& m : made) {
        release(m.view, m.imageFlags);
    }
    if (!made.empty()) {
        EVR_LOG("%s: the engine's resize: %zu stencil view(s) of view 1 released", kTag, made.size());
    }
}

std::uint64_t stencilViewBinds() {
    return g_binds.load(std::memory_order_relaxed);
}

} // namespace evr::vkcore::view_clone
