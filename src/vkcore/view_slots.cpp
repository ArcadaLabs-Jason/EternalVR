#include "vkcore/view_slots.hpp"

#include "vkcore/game_code.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/view_async.hpp"
#include "vkcore/view_binning.hpp"
#include "vkcore/view_clones.hpp"
#include "vkcore/view_frames.hpp"
#include "vkcore/view_redirects.hpp"
#include "vkcore/view_slots_storage.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>
#include <utility>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "parallel eyes";

// ---- Sites (RVAs in build 25216728; docs/rig-findings/perf-multiview-slots.md section 3) ----

// r_maxRenderViews: `mov rax, [rip + cvar]` at the visibility context count (RVA 0x1D17144); the cvar's
// data holds the value at +8.
constexpr std::uint32_t kMaxRenderViewsLoad = 0x1D17144;

// The device context's view slot: `imul dst, index, 0xA8`, then `[dst + dc + field]`. The hook goes on the
// next instruction. 0x1D02053 multiplies index + 1. 0x1C1A196 is the constructor's slot loop.
enum class SlotKind : std::uint8_t { Index, IndexPlusOne, Constructor };
struct SlotSite {
    std::uint32_t rva;
    SlotKind kind;
};
constexpr SlotSite kSlotSites[] = {
    {0x1C1A196, SlotKind::Constructor},  {0x1C54B20, SlotKind::Index}, {0x1C5663D, SlotKind::Index},
    {0x1C568DD, SlotKind::Index},        {0x1C56946, SlotKind::Index}, {0x1C56B2D, SlotKind::Index},
    {0x1C56B7A, SlotKind::Index},        {0x1C56DB1, SlotKind::Index}, {0x1C57F4C, SlotKind::Index},
    {0x1C5CBE0, SlotKind::Index},        {0x1C5F131, SlotKind::Index}, {0x1C5FB41, SlotKind::Index},
    {0x1C5FFD7, SlotKind::Index},        {0x1C60368, SlotKind::Index}, {0x1C6089A, SlotKind::Index},
    {0x1C609D5, SlotKind::Index},        {0x1C60FF0, SlotKind::Index}, {0x1C92CEF, SlotKind::Index},
    {0x1C98CCA, SlotKind::Index},        {0x1CBB5B0, SlotKind::Index}, {0x1CBB6D0, SlotKind::Index},
    {0x1CDF8B8, SlotKind::Index},        {0x1CDF9B2, SlotKind::Index}, {0x1CEEECE, SlotKind::Index},
    {0x1CF0307, SlotKind::Index},        {0x1CFB7DF, SlotKind::Index}, {0x1D00841, SlotKind::Index},
    {0x1D02053, SlotKind::IndexPlusOne}, {0x1CBB587, SlotKind::Index},
};
constexpr std::size_t kSlotStride = 0xA8;
constexpr std::size_t kSlotOffset = 0x8; // slot 0 in the device context

// The device context's occlusion-query state: `mov dst, [rax + rcx*8 + 0x220]` (rax = device context, rcx =
// view index). Where dst is rcx the hook goes on the instruction and moves the index to a cell of ours; the
// one site with another dst (0x1C5DF48, r8) is hooked after it.
constexpr std::uint32_t kOcclusionIntoIndex[] = {0x1C5C64D, 0x1C5C6A3, 0x1C5C9CD, 0x1C5F3E3,
                                                 0x1C5FE44, 0x1C607A6, 0x1C7FF78};
constexpr std::uint32_t kOcclusionIntoR8 = 0x1C5DF48;
constexpr std::size_t kOcclusionInstruction = 8;
constexpr std::size_t kOcclusionTable = 0x220;
constexpr std::size_t kOcclusionSize = 0x2BC200;

// The render thread (idRenderThread): its render context holder at +0x2F8 (one pointer), the render list at
// +0xA0 (stride 0x118) with the count at +0xA8.
constexpr std::size_t kHolder = 0x2F8;
constexpr std::size_t kRenderList = 0xA0;
constexpr std::size_t kRenderCount = 0xA8;
constexpr std::size_t kRenderEntry = 0x118;
constexpr std::size_t kEntryRenderView =
    0x30; // null for a view without a world (the dispatcher passes it on)
// `lea rcx, [r14 + 0x2F8]` in the render thread body before its per-view calls; hooked on the call after.
constexpr std::uint32_t kHolderLeas[] = {0x1CD840A, 0x1CD84C7, 0x1CD8507};
constexpr std::size_t kLeaSize = 7;
// `mov rbx, [rcx + rax*8 + 0x2F8]` (rcx = render thread, rax = view index) in RVA 0x1CD70B0; hooked on the
// instruction, the index is moved to a cell of ours when it is 1.
constexpr std::uint32_t kHolderIndexed = 0x1CD7721;
// The job setup's loop that copies the render list into its one-view descriptor (RVA 0x1CDCD90): the hook
// at the loop head keeps it to one view.
constexpr std::uint32_t kSetupLoopHead = 0x1CDCF30;
// The per-view dispatcher (holder, descriptor): per view, setup, packet, render-view job chain.
constexpr std::uint32_t kDispatcher = 0x1C5CD00;
constexpr std::size_t kDescriptorSize = 0x90;
constexpr std::size_t kDescriptorViews = 0x28;
constexpr std::size_t kDescriptorCount = 0x30;
constexpr std::size_t kDescriptorMoreViews = 0x36;
// A render context: 0x71B170 bytes, built by RVA 0x1C593E0, then two fields cleared (RVA 0x1C59380).
constexpr std::uint32_t kContextCtor = 0x1C593E0;
constexpr std::size_t kContextSize = 0x71B170;
constexpr std::size_t kContextClear[] = {0x71AE00, 0x71AE08};
// The device context's resize (deviceContext, size, upscaledSize) and the slot builder (deviceContext, slot,
// deviceContextIndex), as per-eye TAA uses them (taa_locate.hpp).
constexpr std::uint32_t kContextResize = 0x1C21600;
constexpr std::uint32_t kSlotBuilder = 0x1C20150;

using DispatcherFn = void (*)(void* holder, std::byte* descriptor);
using ContextCtorFn = void* (*)(void* context);
using ContextResizeFn = void (*)(void* deviceContext, const int* size, const int* upscaledSize);
using SlotBuilderFn = void (*)(void* deviceContext, void* slot, std::uint64_t deviceContextIndex);

std::atomic<bool> g_active{false};
// Set before the install's first change to the engine: Route S does not run on the changed engine, whether
// the install then completes (g_active) or a hook after the change fails (view 0 alone for the session).
std::atomic<bool> g_changed{false};
// After a guard trip: frames the game built with two views before it may still be rendered, or their view 1
// work still be running; false after kSettledFrames one-view frames in a row.
std::atomic<bool> g_lingering{true};
constexpr int kSettledFrames = 3;
std::atomic<int> g_oneViewRun{0}; // one-view frames in a row since the trip
// Whether the frames just sent rendered view 1, for the eye copy (view_frames.hpp): the same window.
View1Frames g_view1Frames{kSettledFrames};
const std::byte* g_base = nullptr;
std::byte* g_maxViews = nullptr; // r_maxRenderViews' data

std::byte* g_slot1 = nullptr; // view 1's device context slot
std::atomic<std::byte*> g_deviceContext{nullptr};
std::atomic<std::intptr_t> g_slotDelta{0};   // slot 1 - (dc + slot 0 + stride)
std::byte* g_occlusion1 = nullptr;           // view 1's occlusion-query state
std::byte* g_occlusionCell = nullptr;        // holds g_occlusion1, 8-aligned
std::atomic<std::byte*> g_context1{nullptr}; // view 1's render context
std::mutex g_contextMutex;
std::array<std::byte*, 2> g_holder{};
std::byte** g_contextCell = nullptr; // holds view 1's render context, 8-aligned
DispatcherFn g_dispatcher = nullptr;
ContextResizeFn g_contextResize = nullptr;
std::atomic<std::uint64_t> g_slotRedirects{0};
std::atomic<std::uint64_t> g_occlusionRedirects{0};
std::atomic<std::uint64_t> g_twoViewFrames{0};
std::atomic<std::uint64_t> g_oneViewFrames{0}; // two views but no world (loading, menus): view 0 alone
std::atomic<std::uint64_t> g_trippedFrames{0}; // two views after a guard trip: view 0 alone

template <typename T>
T at(std::uint32_t rva) {
    return reinterpret_cast<T>(const_cast<std::byte*>(g_base + rva));
}

// ---- Device context slot ----
// The storage hooks below keep the engine's one-view storage safe whenever view index 1 shows up (the
// renderer loops over r_maxRenderViews, which stays 2): they act while installed, guard or not, and change
// nothing for view index 0.

void buildSlot1(std::byte* deviceContext) {
    g_deviceContext.store(deviceContext, std::memory_order_release);
    const auto delta = reinterpret_cast<std::intptr_t>(g_slot1) -
                       reinterpret_cast<std::intptr_t>(deviceContext + kSlotOffset + kSlotStride);
    g_slotDelta.store(delta, std::memory_order_release);
    EVR_LOG("%s: device context %p: view slot 1 at %p", kTag, static_cast<void*>(deviceContext),
            static_cast<void*>(g_slot1));
}

template <int Reg, SlotKind Kind>
void onSlotSite(HookRegisters& r) {
    if (!g_active.load(std::memory_order_acquire)) {
        return;
    }
    std::uintptr_t& product = registerByNumber(r, Reg);
    if constexpr (Kind == SlotKind::Constructor) {
        if (product != kSlotStride) {
            return;
        }
        buildSlot1(reinterpret_cast<std::byte*>(r.r14)); // the constructor keeps the device context in r14
    } else if (product != (Kind == SlotKind::IndexPlusOne ? 2 * kSlotStride : kSlotStride)) {
        return;
    }
    product += static_cast<std::uintptr_t>(g_slotDelta.load(std::memory_order_acquire));
    g_slotRedirects.fetch_add(1, std::memory_order_relaxed);
}

template <SlotKind Kind, std::size_t... R>
constexpr std::array<MidHookEditCallback, 16> slotCallbacks(std::index_sequence<R...>) {
    return {&onSlotSite<static_cast<int>(R), Kind>...};
}
constexpr auto kIndexCallbacks = slotCallbacks<SlotKind::Index>(std::make_index_sequence<16>{});
constexpr auto kPlusOneCallbacks = slotCallbacks<SlotKind::IndexPlusOne>(std::make_index_sequence<16>{});
constexpr auto kCtorCallbacks = slotCallbacks<SlotKind::Constructor>(std::make_index_sequence<16>{});

// The slot follows the engine's resizes: the builder makes the whole set again (it frees what the slot held),
// as per-eye TAA does for its eye R slot.
void contextResize(void* deviceContext, const int* size, const int* upscaledSize) {
    g_contextResize(deviceContext, size, upscaledSize);
    if (!g_active.load(std::memory_order_acquire) ||
        deviceContext != g_deviceContext.load(std::memory_order_acquire) || !mp_guard::allowsGameTouch()) {
        return;
    }
    std::uint32_t index = 0;
    std::memcpy(&index, deviceContext, sizeof(index));
    at<SlotBuilderFn>(kSlotBuilder)(deviceContext, g_slot1, index);
    EVR_LOG("%s: device context resized to %dx%d; view slot 1 rebuilt", kTag, size ? size[0] : 0,
            size ? size[1] : 0);
}

// ---- Occlusion-query state ----

void onOcclusionIntoIndex(HookRegisters& r) {
    if (r.rcx != 1 || !g_active.load(std::memory_order_acquire)) {
        return;
    }
    // [rax + rcx*8 + 0x220] now reads our cell.
    r.rcx = (reinterpret_cast<std::uintptr_t>(g_occlusionCell) - r.rax - kOcclusionTable) / 8;
    g_occlusionRedirects.fetch_add(1, std::memory_order_relaxed);
}

void onOcclusionIntoR8(HookRegisters& r) {
    if (r.rcx == 1 && g_active.load(std::memory_order_acquire)) {
        r.r8 = reinterpret_cast<std::uintptr_t>(g_occlusion1);
        g_occlusionRedirects.fetch_add(1, std::memory_order_relaxed);
    }
}

// ---- Render contexts and the dispatcher ----

std::byte* context1() {
    if (std::byte* c = g_context1.load(std::memory_order_acquire)) {
        return c;
    }
    std::lock_guard lock(g_contextMutex);
    if (std::byte* c = g_context1.load()) {
        return c;
    }
    auto* memory = static_cast<std::byte*>(
        VirtualAlloc(nullptr, kContextSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!memory) {
        EVR_LOG("%s: no memory for view 1's render context", kTag);
        return nullptr;
    }
    at<ContextCtorFn>(kContextCtor)(memory);
    for (const std::size_t field : kContextClear) {
        std::memset(memory + field, 0, sizeof(void*));
    }
    g_context1.store(memory, std::memory_order_release);
    EVR_LOG("%s: view 1's render context built at %p", kTag, static_cast<void*>(memory));
    return memory;
}

std::byte** holderOf(std::byte* renderThread) {
    g_holder[0] = *reinterpret_cast<std::byte**>(renderThread + kHolder);
    g_holder[1] = context1();
    return g_holder.data();
}

// The render thread's one context pointer becomes a two-entry array; entry 0 is the engine's own.
void onHolderLea(HookRegisters& r) {
    if (g_active.load(std::memory_order_acquire)) {
        r.rcx = reinterpret_cast<std::uintptr_t>(holderOf(reinterpret_cast<std::byte*>(r.r14)));
    }
}

void onHolderIndexed(HookRegisters& r) {
    if (r.rax != 1 || !g_contextCell || !g_active.load(std::memory_order_acquire)) {
        return;
    }
    *g_contextCell = context1();
    r.rax = (reinterpret_cast<std::uintptr_t>(g_contextCell) - r.rcx - kHolder) / 8;
}

// The descriptor has room for one view; the dispatcher hook runs the others. A no-op for one view.
void onSetupLoopHead(HookRegisters& r) {
    if (r.r15 > 1 && g_active.load(std::memory_order_acquire)) {
        r.r15 = 1;
    }
}

// One view of the frame through the engine's dispatcher, as a one-view descriptor ("more views follow" when
// asked). The copy lives on the stack: that holds only because the engine's dispatcher is done with the
// descriptor when it returns, as the rig runs show.
void dispatchView(void* holder, const std::byte* descriptor, std::byte* entry, bool moreViews) {
    alignas(16) std::byte one[kDescriptorSize];
    std::memcpy(one, descriptor, sizeof(one));
    const std::int32_t single = 1;
    std::memcpy(one + kDescriptorViews, &entry, sizeof(entry));
    std::memcpy(one + kDescriptorCount, &single, sizeof(single));
    if (moreViews) {
        one[kDescriptorMoreViews] = std::byte{1};
    }
    g_dispatcher(holder, one);
}

void logCounts() {
    EVR_LOG("%s: %llu two-view frame(s); view 0 alone: %llu without a world, %llu after a guard trip; slot "
            "redirects %llu, occlusion redirects %llu",
            kTag, static_cast<unsigned long long>(g_twoViewFrames.load()),
            static_cast<unsigned long long>(g_oneViewFrames.load()),
            static_cast<unsigned long long>(g_trippedFrames.load()),
            static_cast<unsigned long long>(g_slotRedirects.load()),
            static_cast<unsigned long long>(g_occlusionRedirects.load()));
    viewRedirectsLogCounts();
}

void dispatcher(void* holder, std::byte* descriptor) {
    std::byte* thread = static_cast<std::byte*>(holder) - kHolder;
    std::int32_t count = 0;
    std::memcpy(&count, thread + kRenderCount, sizeof(count));
    if (!g_active.load(std::memory_order_acquire)) {
        // An install that failed after changing the engine: view 0 alone, as after a guard trip
        // (r_maxRenderViews stays 1 then, so a frame has one view anyway).
        if (count > 1 && g_changed.load(std::memory_order_acquire)) {
            dispatchView(holder, descriptor, *reinterpret_cast<std::byte**>(thread + kRenderList), false);
            return;
        }
        g_dispatcher(holder, descriptor);
        return;
    }
    if (count <= 1) {
        if (!mp_guard::allowsGameTouch() && g_oneViewRun.fetch_add(1) + 1 >= kSettledFrames &&
            g_lingering.exchange(false, std::memory_order_acq_rel)) {
            EVR_LOG(
                "%s: after the multiplayer guard trip no frame holds view 1 any more; %llu frame(s) with it "
                "ended with view 0 alone",
                kTag, static_cast<unsigned long long>(g_trippedFrames.load()));
        }
        viewBinningOneViewFrame();
        g_view1Frames.frame(false);
        g_dispatcher(holder, descriptor);
        return;
    }
    // A frame without a world view (loading screens, menus): every view's no-world jobs would take view 0's
    // render context (they pick it by the render view) and record into the same contexts at once. Such a
    // frame goes the engine's one-view way. So does every frame after a multiplayer guard trip: view 1 is
    // not rendered (the frames the game built with it before the trip finish with view 0 alone).
    std::byte* list = *reinterpret_cast<std::byte**>(thread + kRenderList);
    bool world = true;
    for (std::int32_t i = 0; i < count; ++i) {
        std::byte* renderView = nullptr;
        std::memcpy(&renderView, list + static_cast<std::size_t>(i) * kRenderEntry + kEntryRenderView,
                    sizeof(renderView));
        world = world && renderView;
    }
    const bool armed = mp_guard::allowsGameTouch();
    g_oneViewRun.store(0, std::memory_order_relaxed);
    if (!world || !armed) {
        if ((armed ? g_oneViewFrames : g_trippedFrames).fetch_add(1, std::memory_order_relaxed) == 0 &&
            !armed) {
            EVR_LOG("%s: the multiplayer guard has tripped: view 0 alone from now on", kTag);
        }
        viewBinningOneViewFrame();
        g_view1Frames.frame(false);
        dispatchView(holder, descriptor, list, false);
        return;
    }
    std::byte** holders = holderOf(thread);
    viewRedirectsDispatchStart();
    // ETERNALVR_TEST_VIEW_ONLY=0 or 1 renders that view alone (test only). Async compute on after all: view 0
    // alone (view_async.hpp). View 1's clones are made only for a frame that renders it; once they are off
    // for the process, view 1 would draw into the engine's own targets with view 0: view 0 alone too.
    const bool async = viewAsyncComputeOn();
    const int test = parallelEyesSettings().viewOnly;
    if (!async && test != 0 && viewClonesPrepare()) {
        g_view1Frames.restart(); // new clones: eye 1 waits for a run of frames that wrote them
    }
    const int only = async || viewClonesStopped() ? 0 : test;
    g_view1Frames.frame(only != 0);
    const std::int32_t dispatched = only >= 0 ? 1 : count;
    for (std::int32_t k = 0; k < dispatched; ++k) {
        const std::int32_t i = only >= 0 ? only : k;
        dispatchView(holders, descriptor, list + static_cast<std::size_t>(i) * kRenderEntry,
                     k + 1 < dispatched);
    }
    if (g_twoViewFrames.fetch_add(1, std::memory_order_relaxed) % 10000 == 0) {
        logCounts();
    }
}

// ---- Install ----

bool hookEdit(std::uint32_t rva, MidHookEditCallback callback, const char* what) {
    std::string error;
    if (!installMidHookEdit(at<void*>(rva), callback, error)) {
        EVR_LOG("%s: %s hook at RVA 0x%X failed: %s", kTag, what, rva, error.c_str());
        return false;
    }
    return true;
}

// Each slot site's `imul` destination register, read by checkSlotSites.
std::array<int, std::size(kSlotSites)> g_slotDst{};

bool checkSlotSites() {
    for (std::size_t i = 0; i < std::size(kSlotSites); ++i) {
        const std::byte* p = g_base + kSlotSites[i].rva;
        const auto rex = std::to_integer<std::uint8_t>(p[0]);
        const auto opcode = std::to_integer<std::uint8_t>(p[1]);
        const auto modrm = std::to_integer<std::uint8_t>(p[2]);
        std::uint32_t imm = 0;
        std::memcpy(&imm, p + 3, sizeof(imm));
        if ((rex & 0xF0) != 0x40 || opcode != 0x69 || (modrm & 0xC0) != 0xC0 || imm != kSlotStride) {
            EVR_LOG("%s: RVA 0x%X is not `imul reg, reg, 0xA8`", kTag, kSlotSites[i].rva);
            return false;
        }
        g_slotDst[i] = ((rex >> 2) & 1) << 3 | ((modrm >> 3) & 7);
    }
    return true;
}

bool installSlotHooks() {
    for (std::size_t i = 0; i < std::size(kSlotSites); ++i) {
        const SlotSite& s = kSlotSites[i];
        const auto& table = s.kind == SlotKind::Constructor    ? kCtorCallbacks
                            : s.kind == SlotKind::IndexPlusOne ? kPlusOneCallbacks
                                                               : kIndexCallbacks;
        if (!hookEdit(s.rva + 7, table[static_cast<std::size_t>(g_slotDst[i])], "view slot")) {
            return false;
        }
    }
    return true;
}

// The storage hooks' memory: view 1's device context slot, its occlusion-query state and the render context
// cell.
bool allocateStorage() {
    g_slot1 =
        static_cast<std::byte*>(VirtualAlloc(nullptr, kSlotStride, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    g_occlusion1 = static_cast<std::byte*>(
        VirtualAlloc(nullptr, kOcclusionSize + 16, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    g_contextCell =
        static_cast<std::byte**>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!g_slot1 || !g_occlusion1 || !g_contextCell) {
        EVR_LOG("%s: no memory for view 1's storage", kTag);
        return false;
    }
    const std::int32_t one = 1;
    const std::int64_t oneLong = 1;
    std::memcpy(g_occlusion1 + 0x2BC1F4, &one, sizeof(one)); // as the engine's builder (RVA 0x1C20E32)
    std::memcpy(g_occlusion1 + 0x2BC1F8, &oneLong, sizeof(oneLong));
    g_occlusionCell = g_occlusion1 + kOcclusionSize; // page memory: 8-aligned
    std::memcpy(g_occlusionCell, &g_occlusion1, sizeof(g_occlusion1));
    return true;
}

bool installOcclusionHooks() {
    for (const std::uint32_t rva : kOcclusionIntoIndex) {
        if (!hookEdit(rva, &onOcclusionIntoIndex, "occlusion state")) {
            return false;
        }
    }
    return hookEdit(kOcclusionIntoR8 + kOcclusionInstruction, &onOcclusionIntoR8, "occlusion state");
}

bool installRenderThreadHooks() {
    for (const std::uint32_t rva : kHolderLeas) {
        if (!hookEdit(rva + kLeaSize, &onHolderLea, "render context holder")) {
            return false;
        }
    }
    if (!hookEdit(kHolderIndexed, &onHolderIndexed, "render context by view") ||
        !hookEdit(kSetupLoopHead, &onSetupLoopHead, "job setup loop")) {
        return false;
    }
    std::string error;
    if (!installInlineHook(at<void*>(kDispatcher), reinterpret_cast<void*>(&dispatcher),
                           reinterpret_cast<void**>(&g_dispatcher), error) ||
        !installInlineHook(at<void*>(kContextResize), reinterpret_cast<void*>(&contextResize),
                           reinterpret_cast<void**>(&g_contextResize), error)) {
        EVR_LOG("%s: inline hook failed: %s", kTag, error.c_str());
        return false;
    }
    return true;
}

// r_maxRenderViews' data: checked before anything changes, written last.
std::byte* maxRenderViewsCvar() {
    const std::byte* load = g_base + kMaxRenderViewsLoad;
    if (std::to_integer<std::uint8_t>(load[0]) != 0x48 || std::to_integer<std::uint8_t>(load[1]) != 0x8B) {
        return nullptr;
    }
    return *reinterpret_cast<std::byte* const*>(ripTarget(load + 3, load + 7));
}

void raiseMaxRenderViews(std::byte* cvar) {
    std::int32_t value = 0;
    std::memcpy(&value, cvar + 8, sizeof(value));
    const std::int32_t two = 2;
    std::memcpy(cvar + 8, &two, sizeof(two));
    EVR_LOG("%s: r_maxRenderViews %d -> 2", kTag, value);
}

} // namespace

namespace view_slots {

bool prepareStorage(const std::byte* base) {
    g_base = base;
    g_maxViews = maxRenderViewsCvar();
    if (!g_maxViews) {
        EVR_LOG("%s: RVA 0x%X is not the r_maxRenderViews load", kTag, kMaxRenderViewsLoad);
        return false;
    }
    return checkSlotSites() && allocateStorage();
}

bool installStorageHooks() {
    return installSlotHooks() && installOcclusionHooks() && installRenderThreadHooks();
}

void markChanged() {
    g_changed.store(true, std::memory_order_release);
}

void activate() {
    raiseMaxRenderViews(g_maxViews);
    g_active.store(true, std::memory_order_release);
}

std::size_t slotSites() {
    return std::size(kSlotSites);
}

std::size_t occlusionSites() {
    return std::size(kOcclusionIntoIndex) + 1;
}

} // namespace view_slots

bool viewSlotsActive() {
    return g_active.load(std::memory_order_acquire);
}

bool parallelEyesChangedEngine() {
    return g_changed.load(std::memory_order_acquire);
}

bool viewSlotsView1Rendered() {
    return g_view1Frames.rendered();
}

bool parallelEyesTouch() {
    return g_active.load(std::memory_order_acquire) &&
           (mp_guard::allowsGameTouch() || g_lingering.load(std::memory_order_acquire));
}

std::byte* viewSlotsContext1() {
    return g_context1.load(std::memory_order_acquire);
}

} // namespace evr::vkcore
