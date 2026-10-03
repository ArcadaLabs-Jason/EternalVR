// Parallel Eye Rendering: view 1's passes store and bind its clones instead of the engine's shared targets
// (view_clone_map.hpp). Every hook here changes only view 1's work, and only while parallelEyesTouch() allows
// it (view_slots.hpp).

#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/view_clone_map.hpp"
#include "vkcore/view_slots.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>

namespace evr::vkcore::view_clone {

namespace {

constexpr const char* kTag = "view-clones";

// The per-view setups' stores of shared targets into their blocks: `mov [base + disp], src` (qword).
// Found by a scan of 0x1C54650, 0x1C5F070, 0x1C5F1B0, 0x1C5F810, 0x1C60050, 0x1C60450, 0x1C60BF0, 0x1C60EF0.
// 0x1C5F2D5: the shadow atlas target into the shadow job's block (context +0x522760; the job copies cached
// tiles within it). 0x1C5F6C7: the device context into the render-view pass arguments (context +0x6A7DD8),
// where view 1 gets its own (view_clones.cpp).
constexpr std::uint32_t kStoreSites[] = {
    0x1C5681B, 0x1C5684F, 0x1C56886, 0x1C56898, 0x1C568A7, 0x1C5696B, 0x1C569A8, 0x1C56A2E, 0x1C56A40,
    0x1C56A80, 0x1C56B6C, 0x1C56BC5, 0x1C56BD3, 0x1C56C01, 0x1C56C0F, 0x1C56C1D, 0x1C56C2B, 0x1C56C40,
    0x1C56C4E, 0x1C56C5C, 0x1C56C6A, 0x1C56C78, 0x1C56CAA, 0x1C56CDA, 0x1C56D09, 0x1C56D14, 0x1C56D1F,
    0x1C56D2E, 0x1C57033, 0x1C5F116, 0x1C5F12D, 0x1C5F159, 0x1C5F2EA, 0x1C5F452, 0x1C5F8B8, 0x1C5F8EB,
    0x1C5F95D, 0x1C5FB10, 0x1C5FB25, 0x1C5FB33, 0x1C5FD1B, 0x1C5FFA6, 0x1C5FFBB, 0x1C5FFC9, 0x1C60190,
    0x1C601DF, 0x1C60247, 0x1C60337, 0x1C6034C, 0x1C6035A, 0x1C6041E, 0x1C60425, 0x1C605CD, 0x1C606B5,
    0x1C606CA, 0x1C606FA, 0x1C60869, 0x1C6087E, 0x1C6088C, 0x1C609A4, 0x1C609B9, 0x1C609C7, 0x1C60AEF,
    0x1C60B4D, 0x1C60BC6, 0x1C60D23, 0x1C60E7A, 0x1C60FBF, 0x1C60FD4, 0x1C60FE2, 0x1C5F2D5, 0x1C5F6C7};
constexpr std::size_t kSiteCount = std::size(kStoreSites);

// 0x1C54650 binds the depth pyramid for the lighting passes by name: `mov r8, [rax + 0x2F8]` (rax = the
// device context, r12 = the render context), then `add r8, 0xC8` here.
constexpr std::uint32_t kPyramidBindSite = 0x1C55376;
constexpr std::uint32_t kTargetBind = 0x1C34410; // (context, target, ...)
constexpr std::uint32_t kImageBind = 0x1C53330;  // (parameter block, parameter, image + 0xC8)
// Passes mark each image they write in a command context (0x1C4A130: image, context, index: sets 0x80 in the
// image's per-context state block, image +0x130), which the engine's barriers follow. View 1's passes marked
// the engine's image, so its clones got no barriers between view 1's writes and reads (the depth downsample
// chain: tile-shaped holes in view 1, e1m3). View 1's marks go to the clone.
constexpr std::uint32_t kMarkWritten = 0x1C4A130;

struct Store {
    int base = -1; // register numbers as instruction encodings name them
    int source = -1;
    std::int32_t disp = 0;
};
std::array<Store, kSiteCount> g_stores{};
std::atomic<std::uint64_t> g_stored[2]{}; // view 1's stores: value replaced by its clone, not cloned
std::atomic<std::uint64_t> g_pyramidBinds{0};
std::atomic<std::uint64_t> g_targetBinds[2]{}; // view 1: cloned, not cloned
std::atomic<std::uint64_t> g_imageBinds[2]{};
std::atomic<std::uint64_t> g_marks[2]{}; // view 1: clone marked, image without a clone

std::uintptr_t context1() {
    return reinterpret_cast<std::uintptr_t>(viewSlotsContext1());
}

// ---- The store hooks ----

template <std::size_t I>
void onStore(HookRegisters& r) {
    const Store& s = g_stores[I];
    const std::uintptr_t c1 = context1();
    const std::uintptr_t dest = registerByNumber(r, s.base) + static_cast<std::intptr_t>(s.disp);
    if (!c1 || dest - c1 >= kRenderContextSize || !parallelEyesTouch()) {
        return; // not into view 1's render context
    }
    const Map* map = currentMap();
    std::uintptr_t& value = registerByNumber(r, s.source);
    if (const std::uintptr_t c = map ? lookup(*map, value) : 0) {
        value = c;
        g_stored[0].fetch_add(1, std::memory_order_relaxed);
    } else {
        g_stored[1].fetch_add(1, std::memory_order_relaxed);
    }
}

template <std::size_t... I>
constexpr std::array<MidHookEditCallback, sizeof...(I)> storeCallbacks(std::index_sequence<I...>) {
    return {&onStore<I>...};
}
constexpr auto kStoreCallbacks = storeCallbacks(std::make_index_sequence<kSiteCount>{});

// `REX.W 89 /r` with a [base + disp8/disp32] operand (SIB with no index allowed).
bool decodeStore(const std::byte* p, Store& out) {
    const auto rex = std::to_integer<std::uint8_t>(p[0]);
    if ((rex & 0xF8) != 0x48 || std::to_integer<std::uint8_t>(p[1]) != 0x89) {
        return false;
    }
    const auto modrm = std::to_integer<std::uint8_t>(p[2]);
    const int mod = modrm >> 6;
    int rm = modrm & 7;
    std::size_t at = 3;
    if (mod == 0 || mod == 3) {
        return false;
    }
    if (rm == 4) {
        const auto sib = std::to_integer<std::uint8_t>(p[at++]);
        if (((sib >> 3) & 7) != 4) {
            return false; // an index register
        }
        rm = sib & 7;
    }
    out.source = ((rex >> 2) & 1) << 3 | ((modrm >> 3) & 7);
    out.base = (rex & 1) << 3 | rm;
    if (mod == 1) {
        out.disp = static_cast<std::int8_t>(std::to_integer<std::uint8_t>(p[at]));
    } else {
        std::memcpy(&out.disp, p + at, sizeof(out.disp));
    }
    return true;
}

// ---- The bind hooks: view 1's passes bind the clones ----

// 0x1C55376: r8 = the depth pyramid the setup binds by name; view 1's setup (r12 = its render context) binds
// the clone.
void onPyramidBind(HookRegisters& r) {
    const std::uintptr_t c1 = context1();
    const Map* map = currentMap();
    if (!c1 || r.r12 != c1 || !map || !parallelEyesTouch()) {
        return;
    }
    if (const std::uintptr_t c = lookup(*map, r.r8)) {
        r.r8 = c;
        g_pyramidBinds.fetch_add(1, std::memory_order_relaxed);
    }
}

// Many passes load the engine's targets from their globals and bind them on their command context.
void onTargetBind(HookRegisters& r) {
    const Map* map = currentMap();
    if (!map || !parallelEyesTouch()) {
        return;
    }
    notePassTargetBind(r, map); // view 1's screen pass (its own command contexts are not view 1's)
    if (!isView1Context(r.rcx, false)) {
        return;
    }
    const std::uintptr_t c = lookup(*map, r.rdx);
    g_targetBinds[c ? 0 : 1].fetch_add(1, std::memory_order_relaxed);
    if (c) {
        r.rdx = c;
    }
}

void onImageBind(HookRegisters& r) {
    if (!parallelEyesTouch()) {
        return;
    }
    const Map* map = currentMap();
    if (swapPassImageBind(r, map)) {
        return;
    }
    if (!map || r.r8 < kImageHandle || !isView1Context(r.rcx, true)) {
        return;
    }
    const std::uintptr_t c = lookup(*map, r.r8 - kImageHandle);
    g_imageBinds[c ? 0 : 1].fetch_add(1, std::memory_order_relaxed);
    if (c) {
        r.r8 = c + kImageHandle;
    }
}

void onMarkWritten(HookRegisters& r) {
    const Map* map = currentMap();
    if (!map || !isView1Context(r.rdx, false) || !parallelEyesTouch()) {
        return;
    }
    const std::uintptr_t c = lookup(*map, r.rcx);
    g_marks[c ? 0 : 1].fetch_add(1, std::memory_order_relaxed);
    if (c) {
        r.rcx = c;
    }
}

} // namespace

bool checkBinds() {
    const std::byte* game = base();
    for (std::size_t i = 0; i < kSiteCount; ++i) {
        if (!decodeStore(game + kStoreSites[i], g_stores[i])) {
            EVR_LOG("%s: RVA 0x%X is not a qword register store", kTag, kStoreSites[i]);
            return false;
        }
    }
    // `add r8, 0xC8`
    constexpr std::byte kBindBytes[] = {std::byte{0x49}, std::byte{0x81}, std::byte{0xC0}, std::byte{0xC8}};
    if (std::memcmp(game + kPyramidBindSite, kBindBytes, sizeof(kBindBytes)) != 0) {
        EVR_LOG("%s: RVA 0x%X is not the depth pyramid's bind", kTag, kPyramidBindSite);
        return false;
    }
    return true;
}

bool installBinds() {
    for (std::size_t i = 0; i < kSiteCount; ++i) {
        if (!hookAt(kStoreSites[i], kStoreCallbacks[i], "store")) {
            return false;
        }
    }
    if (!hookAt(kPyramidBindSite, &onPyramidBind, "depth pyramid bind")) {
        return false;
    }
    if (!partOn(parallel_eyes::kBinds)) {
        EVR_LOG("%s: bind hooks off (ETERNALVR_TEST_VIEW_OFF)", kTag);
        return true;
    }
    return hookAt(kTargetBind, &onTargetBind, "target bind") &&
           hookAt(kImageBind, &onImageBind, "image bind") &&
           hookAt(kMarkWritten, &onMarkWritten, "write mark");
}

void reportBinds() {
    EVR_LOG("%s: view 1's stores cloned %llu / not %llu; pyramid binds %llu; target binds cloned %llu / not "
            "%llu, image binds cloned %llu / not %llu; write marks cloned %llu / not %llu",
            kTag, static_cast<unsigned long long>(g_stored[0].load()),
            static_cast<unsigned long long>(g_stored[1].load()),
            static_cast<unsigned long long>(g_pyramidBinds.load()),
            static_cast<unsigned long long>(g_targetBinds[0].load()),
            static_cast<unsigned long long>(g_targetBinds[1].load()),
            static_cast<unsigned long long>(g_imageBinds[0].load()),
            static_cast<unsigned long long>(g_imageBinds[1].load()),
            static_cast<unsigned long long>(g_marks[0].load()),
            static_cast<unsigned long long>(g_marks[1].load()));
}

} // namespace evr::vkcore::view_clone
