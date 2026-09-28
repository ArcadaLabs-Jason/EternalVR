#include "vkcore/stereo_hooks.hpp"

#include "vkcore/game_code.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

namespace evr::vkcore {

namespace {

// ---- Signatures (docs/rig-findings/stereo-reentry.md sections 3 and 10; each unique in build 25216728) ----

// Screen-views loop: the copy of the screen view's renderView_t into its idRenderView (call at RVA
// 0x1C754B7), then the VR check. The per-eye hook goes on the VR check (RVA 0x1C754BC): r12 = render-list
// entry (+0x30 idRenderView*), r13 = idScreenView (+0x20 viewIndex, +0x28 world), r14 = loop index.
constexpr const char* kEyeViewSignature =
    "E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? 49 8B 44 24 30 4C 8D 85 10 02 00 00 33 FF 41 39 "
    "7D 20 "
    "4C 8D B0 A0 00 00 00 40 0F 95 C7 48 8D B0 94 00 00 00 49 8B D6 48 8B CE E8 ?? ?? ?? ??";
constexpr std::size_t kEyeViewPatch = 5;

// The latch call in the same loop (call at RVA 0x1C7576D); the post-latch hook goes on the instruction
// after it (RVA 0x1C75772), where r12 and r13 still hold the entry and the screen view.
constexpr const char* kLatchCallSignature =
    "48 89 44 24 48 41 8B 45 18 41 2B 45 10 89 44 24 50 41 8B 45 1C 41 2B 45 14 89 44 24 54 E8 ?? ?? ?? ?? "
    "48 8B 05 ?? ?? ?? ?? 83 78 08 00 75 0A";
constexpr std::size_t kLatchCallPatch = 0x22;

// Screen-view build (RVA 0x17E8740): the current world from the game system's vtable slot 0x60, stored
// at [rbp - 0x70] (RVA 0x17E879D) ...
constexpr const char* kBuildWorldSignature = "49 8B 45 00 FF 50 60 45 33 FF 48 89 45 90 0F B6 87 60 2A 00 00";
constexpr std::size_t kBuildWorldDisp8 = 13;
// ... and the read of the layout pointer, frameBuilder (rdi) + 0x2A70 (RVA 0x17E88A1). The layout hook
// goes on that read. Also gives the render system object (lea rcx at RVA 0x17E8837) for E7.
constexpr const char* kLayoutReadSignature = "48 8B CF E8 ?? ?? ?? ?? 4C 8B B7 70 2A 00 00";
constexpr std::size_t kLayoutReadPatch = 8;
constexpr const char* kRenderSystemSignature =
    "41 8B D4 48 8B CF E8 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ??";

// World constructor (RVA 0x18E29F0): operator new (0x29950 bytes) then the idRenderView constructor.
constexpr const char* kViewAllocSignature = "4C 8B C6 BA 44 00 00 00 B9 50 99 02 00 E8 ?? ?? ?? ?? 48 85 C0 "
                                            "74 0D 48 8B C8 E8 ?? ?? ?? ?? 48 8B C8 EB 03";
constexpr std::size_t kViewAllocNewCall = 0x0D;
constexpr std::size_t kViewAllocCtorCall = 0x1A;

// World destructor (RVA 0x18E3237): the render-view list resized to one, then view 0 destroyed and
// deleted. The destructor hook goes on its first instruction (rbx = world).
constexpr const char* kWorldDtorSignature = "4C 8D B3 C0 71 5E 00 45 39 6E 0C 7D 0F 41 8B D5 49 8B CE E8 ?? "
                                            "?? ?? ?? 84 C0 74 0F 41 8B 46 0C 41 3B C5 "
                                            "41 0F 4F C5 41 89 46 08 49 8B 06 48 8B 38 48 85 FF 74 18 48 8B "
                                            "CF E8 ?? ?? ?? ?? BA 50 99 02 00 48 8B CF "
                                            "E8 ?? ?? ?? ??";
constexpr std::size_t kWorldDtorViewDtorCall = 0x39;
constexpr std::size_t kWorldDtorDeleteCall = 0x46;

// idRenderWorldLocal::RenderViewForIndex (RVA 0x18E6C00, world vtable slot 0x118): renderViews[i] for
// i < count, else an error print and null.
constexpr const char* kRenderViewForIndexSignature = "48 83 EC 28 85 D2 78 1B 3B 91 C8 71 5E 00 7D 13 48 8B "
                                                     "81 C0 71 5E 00 48 63 D2 48 8B 04 D0 48 83 C4 28 C3";
constexpr std::size_t kRenderViewForIndexSlot = 0x118;

// Per-world visibility (Umbra) contexts: the world's visibility object (world + 0x71DAE8) allocates one
// per r_maxRenderViews when a map's visibility data loads (RVA 0x1D16F90). At RVA 0x1D17144 the count is
// read into ebx; the hook on the next instruction (RVA 0x1D17151) raises it to two, so each eye has its
// own context (the queries of two views on one context run at once and crash in Umbra).
constexpr const char* kVisibilityCountSignature = "48 8B 05 ?? ?? ?? ?? 48 8B CE 8B 58 08 E8";
constexpr std::size_t kVisibilityCountPatch = 0x0D;
constexpr std::size_t kWorldVisibility = 0x71DAE8;
constexpr std::size_t kVisibilityContextCount = 0xD0;

// A render view's visibility context (RVA 0x1CE2320): edx = viewIndex, then owningWorld->vtbl[0x2D8](world,
// edx). The hook after the first instruction (RVA 0x1CE2326; rcx = the render view) sends the second
// eye's view to context 1.
constexpr const char* kVisibilityOfViewSignature =
    "8B 91 90 89 02 00 48 8B 89 18 99 02 00 48 8B 01 48 FF A0 D8 02 00 00";
constexpr std::size_t kVisibilityOfViewPatch = 6;

// Render backend pointer global (mov r8, [rip + ...] at RVA 0x18E7C70), for E7.
constexpr const char* kBackendSignature = "4C 8B 05 ?? ?? ?? ?? 4D 85 C0 74 4F 4C 63 89 C8 71 5E 00";

// ---- Engine layouts ----

constexpr std::size_t kScreenViewStride = 0xA80;
constexpr std::size_t kScreenViewG = 0x30;
constexpr std::size_t kScreenViewWorld = 0x28;
constexpr std::size_t kEntryRenderView = 0x30;
constexpr int kMaxWorlds = 16;

// A screen layout entry (0x28 bytes, table at RVA 0x2E50950): views have an empty name; the next entry
// with a name ends the layout.
struct LayoutEntry {
    const char* name;
    std::int32_t slot;
    float x, y, width, height;
    std::uint8_t enabled; // +0x1C, 1 in every built-in view
    std::uint8_t pad0[3];
    float eye;           // +0x20: 0 keeps the engine's own stereo offset code out
    std::uint8_t stereo; // +0x24, 1 in the built-in leftRightStereo views
    std::uint8_t pad1[3];
};
static_assert(sizeof(LayoutEntry) == 0x28);

// The built-in leftRightStereo views with eye 0: the per-eye hook moves each view to its eye. Slot 1
// makes the build ask for render view 1 (the world's second view, below); the per-eye hook then files
// that view under view slot 0 like the first (see onEyeViewHook).
const LayoutEntry g_sideBySide[] = {
    {"", 0, 0.0f, 0.0f, 0.5f, 1.0f, 1, {}, 0.0f, 1, {}},
    {"", 1, 0.5f, 0.0f, 0.5f, 1.0f, 1, {}, 0.0f, 1, {}},
    {"end of list", 0, 0.0f, 0.0f, 0.0f, 0.0f, 0, {}, 0.0f, 0, {}},
};

using NewFn = void* (*)(std::size_t size, int tag, void* owner);
using ViewCtorFn = void* (*)(void* view);
using ViewDtorFn = void (*)(void* view);
using DeleteFn = void (*)(void* pointer, std::size_t size);
using RenderViewForIndexFn = std::byte* (*)(std::byte* world, int index);

std::shared_mutex g_sinkMutex;
StereoHookSink* g_sink = nullptr;
std::once_flag g_installOnce;
StereoHookStatus g_status;

std::atomic<bool> g_wantTwoViews{false};
std::intptr_t g_worldSlot = 0; // world = *(rbp + g_worldSlot) at the layout hook
NewFn g_new = nullptr;
ViewCtorFn g_viewCtor = nullptr;
ViewDtorFn g_viewDtor = nullptr;
DeleteFn g_delete = nullptr;
RenderViewForIndexFn g_renderViewForIndex = nullptr; // the engine's own
const std::byte* g_renderSystem = nullptr;
std::byte* const* g_backendPointer = nullptr;

std::atomic<const void*> g_originalLayout{nullptr};
std::atomic<int> g_layoutChanges{0};
std::atomic<int> g_created{0};
std::atomic<bool> g_vtablePatched{false};
std::atomic<int> g_visibilityRaised{0};
std::atomic<std::uint64_t> g_secondEyeVisibility{0};

// Each world's second render view: read lock-free by RenderViewForIndex on render job threads, written
// under g_viewMutex (the screen-view build adds, the world destructor removes).
struct SecondView {
    std::atomic<std::byte*> world{nullptr};
    std::atomic<std::byte*> view{nullptr};
};
SecondView g_second[kMaxWorlds];
std::mutex g_viewMutex;
void freeRetiredViews(bool all);

std::byte* secondViewOf(const std::byte* world) {
    for (const SecondView& s : g_second) {
        if (s.world.load(std::memory_order_acquire) == world) {
            return s.view.load(std::memory_order_acquire);
        }
    }
    return nullptr;
}

// The world vtable's RenderViewForIndex: index 1 is the world's second view; the rest is the engine's.
std::byte* renderViewForIndex(std::byte* world, int index) {
    if (index == 1) {
        if (std::byte* view = secondViewOf(world)) {
            return view;
        }
    }
    return g_renderViewForIndex(world, index);
}

// Points the world vtable's RenderViewForIndex slot at renderViewForIndex (once, at the first world).
bool patchWorldVtable(std::byte* world) {
    if (g_vtablePatched.load()) {
        return true;
    }
    auto** vtable = *reinterpret_cast<void***>(world);
    void** slot = vtable + kRenderViewForIndexSlot / sizeof(void*);
    if (*slot != reinterpret_cast<void*>(g_renderViewForIndex)) {
        EVR_LOG("stereo: world %p vtable slot 0x%zX is not RenderViewForIndex; two views off",
                static_cast<void*>(world), kRenderViewForIndexSlot);
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
        EVR_LOG("stereo: cannot write the world vtable; two views off");
        return false;
    }
    *slot = reinterpret_cast<void*>(&renderViewForIndex);
    VirtualProtect(slot, sizeof(void*), old, &old);
    g_vtablePatched.store(true);
    EVR_LOG("stereo: world vtable %p: RenderViewForIndex(1) now returns each world's second render view",
            static_cast<void*>(vtable));
    return true;
}

// Gives the world a second idRenderView (the engine's allocator and constructor, as for the first). The
// world's own list of render views is left alone: loops over it index per-view backend state that this
// build has once.
std::byte* ensureSecondView(std::byte* world) {
    if (std::byte* view = secondViewOf(world)) {
        return view;
    }
    std::lock_guard lock(g_viewMutex);
    freeRetiredViews(false);
    if (!patchWorldVtable(world)) {
        return nullptr;
    }
    const int contexts =
        *reinterpret_cast<const std::int32_t*>(world + kWorldVisibility + kVisibilityContextCount);
    if (contexts < 2) {
        static std::atomic<int> logged{0};
        if (logged.fetch_add(1) < 5) {
            EVR_LOG("stereo: world %p has %d visibility context(s) (its map loaded before the stereo hooks); "
                    "one view",
                    static_cast<void*>(world), contexts);
        }
        return nullptr;
    }
    SecondView* freeSlot = nullptr;
    for (SecondView& s : g_second) {
        if (!s.world.load()) {
            freeSlot = &s;
            break;
        }
    }
    if (!freeSlot) {
        EVR_LOG("stereo: more than %d worlds with a second view; world %p keeps one view", kMaxWorlds,
                static_cast<void*>(world));
        return nullptr;
    }
    void* memory = g_new(render_view_object::kSize, 0x44, world);
    if (!memory) {
        EVR_LOG("stereo: allocating a render view failed");
        return nullptr;
    }
    auto* view = static_cast<std::byte*>(g_viewCtor(memory));
    *reinterpret_cast<std::int32_t*>(view + render_view_object::kViewIndex) = 0;
    *reinterpret_cast<std::byte**>(view + render_view_object::kOwningWorld) = world;
    freeSlot->view.store(view, std::memory_order_release);
    freeSlot->world.store(world, std::memory_order_release);
    if (g_created.fetch_add(1) < 20) {
        EVR_LOG("stereo: world %p got a second render view %p", static_cast<void*>(world),
                static_cast<void*>(view));
    }
    return view;
}

// A destroyed world's second view is freed a few seconds later, not in the destructor: the render thread
// can still be drawing a frame that refers to it.
struct Retired {
    std::byte* view;
    ULONGLONG since;
};
std::vector<Retired> g_retired; // under g_viewMutex
constexpr ULONGLONG kRetireMs = 5000;

void freeRetiredViews(bool all) {
    const ULONGLONG now = GetTickCount64();
    for (auto it = g_retired.begin(); it != g_retired.end();) {
        if (all || now - it->since >= kRetireMs) {
            g_viewDtor(it->view);
            g_delete(it->view, render_view_object::kSize);
            it = g_retired.erase(it);
        } else {
            ++it;
        }
    }
}

void removeSecondView(std::byte* world) {
    std::lock_guard lock(g_viewMutex);
    for (SecondView& s : g_second) {
        if (s.world.load() == world) {
            std::byte* view = s.view.load();
            s.world.store(nullptr, std::memory_order_release);
            s.view.store(nullptr, std::memory_order_release);
            if (view) {
                g_retired.push_back({view, GetTickCount64()});
            }
            EVR_LOG("stereo: world %p destroyed; its second render view %p is freed in %llu ms",
                    static_cast<void*>(world), static_cast<void*>(view),
                    static_cast<unsigned long long>(kRetireMs));
            return;
        }
    }
}

// ---- Hook callbacks ----

void onLayoutHook(const HookRegisters& regs) {
    auto* builder = reinterpret_cast<std::byte*>(regs.rdi);
    auto** layout = reinterpret_cast<const void**>(builder + 0x2A70);
    const void* current = *layout;
    if (current != g_sideBySide && g_originalLayout.load() != current) {
        g_originalLayout.store(current);
    }
    auto* world = *reinterpret_cast<std::byte**>(static_cast<std::intptr_t>(regs.rbp) + g_worldSlot);
    bool two = false;
    // After a multiplayer guard trip only the game's own layout is put back.
    if (g_wantTwoViews.load(std::memory_order_relaxed) && world && mp_guard::allowsGameTouch()) {
        two = ensureSecondView(world) != nullptr;
    }
    const void* wanted = two ? static_cast<const void*>(g_sideBySide) : g_originalLayout.load();
    if (world && wanted && wanted != current) {
        *layout = wanted;
        if (g_layoutChanges.fetch_add(1) < 20) {
            EVR_LOG("stereo: screen layout %s for world %p",
                    two ? "two views side by side" : "back to the game's", static_cast<void*>(world));
        }
    }
}

void onVisibilityCountHook(HookRegisters& regs) {
    if (g_wantTwoViews.load(std::memory_order_relaxed) && static_cast<std::uint32_t>(regs.rbx) < 2 &&
        mp_guard::allowsGameTouch()) {
        regs.rbx = 2;
        if (g_visibilityRaised.fetch_add(1) < 10) {
            EVR_LOG("stereo: a world's visibility data loads with two per-view contexts");
        }
    }
}

void onVisibilityOfViewHook(HookRegisters& regs) {
    auto* renderView = reinterpret_cast<std::byte*>(regs.rcx);
    if (static_cast<std::uint32_t>(regs.rdx) != 0 || !mp_guard::allowsGameTouch()) {
        return;
    }
    auto* world = *reinterpret_cast<std::byte**>(renderView + render_view_object::kOwningWorld);
    if (world && renderView == secondViewOf(world) &&
        *reinterpret_cast<const std::int32_t*>(world + kWorldVisibility + kVisibilityContextCount) >= 2) {
        regs.rdx = 1;
        g_secondEyeVisibility.fetch_add(1, std::memory_order_relaxed);
    }
}

void onWorldDtorHook(const HookRegisters& regs) {
    removeSecondView(reinterpret_cast<std::byte*>(regs.rbx));
}

void onEyeViewHook(const HookRegisters& regs) {
    auto* entry = reinterpret_cast<std::byte*>(regs.r12);
    auto* screenView = reinterpret_cast<std::byte*>(regs.r13);
    auto* renderView = *reinterpret_cast<std::byte**>(entry + kEntryRenderView);
    const std::uint64_t eye = regs.r14; // the loop index: 0 left, 1 right
    if (!renderView || eye > 1) {
        return;
    }
    const std::byte* firstViewG = screenView - eye * kScreenViewStride + kScreenViewG;
    std::shared_lock lock(g_sinkMutex);
    if (g_sink) {
        g_sink->onEyeView(renderView, static_cast<int>(eye), firstViewG);
    }
}

void onEyeLatchedHook(const HookRegisters& regs) {
    auto* entry = reinterpret_cast<const std::byte*>(regs.r12);
    auto* screenView = reinterpret_cast<const std::byte*>(regs.r13);
    auto* renderView = *reinterpret_cast<std::byte* const*>(entry + kEntryRenderView);
    const std::uint64_t eye = regs.r14;
    if (!renderView || eye > 1) {
        return;
    }
    if (eye == 1 && mp_guard::allowsGameTouch() &&
        renderView == secondViewOf(*reinterpret_cast<std::byte* const*>(screenView + kScreenViewWorld))) {
        // The latch filed the second eye's render view under viewIndex 1. The screen view and its
        // render-list entry keep 1 (the frame's per-screen-view arrays), but the render view's own index
        // goes back to 0: the Umbra request, the render-view jobs and the backend index per-view state by
        // it (the device context's view slot, the renderer's per-view block), and this build has one of
        // each. The world's visibility context is the exception: the visibility hook sends this view to
        // context 1.
        *reinterpret_cast<std::int32_t*>(renderView + render_view_object::kViewIndex) = 0;
    }
    std::shared_lock lock(g_sinkMutex);
    if (g_sink) {
        g_sink->onEyeLatched(renderView, static_cast<int>(eye));
    }
}

bool installEdit(const GameText& text,
                 const std::byte* site,
                 std::size_t offset,
                 MidHookEditCallback callback,
                 const char* name) {
    std::string error;
    void* patch = const_cast<std::byte*>(site + offset);
    if (!installMidHookEdit(patch, callback, error)) {
        EVR_LOG("stereo: %s hook failed: %s", name, error.c_str());
        return false;
    }
    EVR_LOG("stereo: %s hook at RVA 0x%X", name, rvaOf(text, patch));
    return true;
}

bool install(const GameText& text,
             const std::byte* site,
             std::size_t offset,
             MidHookCallback callback,
             const char* name) {
    std::string error;
    void* patch = const_cast<std::byte*>(site + offset);
    if (!installMidHook(patch, callback, error)) {
        EVR_LOG("stereo: %s hook failed: %s", name, error.c_str());
        return false;
    }
    EVR_LOG("stereo: %s hook at RVA 0x%X", name, rvaOf(text, patch));
    return true;
}

bool installEyeHooks(const GameText& text) {
    const std::byte* eye = findUniqueInText(text, "per-eye view", kEyeViewSignature);
    const std::byte* latch = findUniqueInText(text, "screen-view latch call", kLatchCallSignature);
    if (!eye || !latch || latch <= eye || latch - eye > 0x400) {
        return false;
    }
    return install(text, eye, kEyeViewPatch, &onEyeViewHook, "per-eye view") &&
           install(text, latch, kLatchCallPatch, &onEyeLatchedHook, "post-latch");
}

bool installTwoViewHooks(const GameText& text) {
    const std::byte* world = findUniqueInText(text, "screen-view build world", kBuildWorldSignature);
    const std::byte* layout = findUniqueInText(text, "screen-view layout read", kLayoutReadSignature);
    const std::byte* alloc = findUniqueInText(text, "render view allocation", kViewAllocSignature);
    const std::byte* dtor = findUniqueInText(text, "world destructor views", kWorldDtorSignature);
    const std::byte* forIndex = findUniqueInText(text, "RenderViewForIndex", kRenderViewForIndexSignature);
    const std::byte* visCount = findUniqueInText(text, "visibility context count", kVisibilityCountSignature);
    const std::byte* visOfView =
        findUniqueInText(text, "visibility context of a view", kVisibilityOfViewSignature);
    if (!world || !layout || !alloc || !dtor || !forIndex || !visCount || !visOfView || layout <= world ||
        layout - world > 0x200) {
        return false;
    }
    g_worldSlot = static_cast<std::int8_t>(std::to_integer<std::uint8_t>(world[kBuildWorldDisp8]));
    g_new = reinterpret_cast<NewFn>(const_cast<std::byte*>(relativeTarget(alloc + kViewAllocNewCall)));
    g_viewCtor =
        reinterpret_cast<ViewCtorFn>(const_cast<std::byte*>(relativeTarget(alloc + kViewAllocCtorCall)));
    g_viewDtor =
        reinterpret_cast<ViewDtorFn>(const_cast<std::byte*>(relativeTarget(dtor + kWorldDtorViewDtorCall)));
    g_delete =
        reinterpret_cast<DeleteFn>(const_cast<std::byte*>(relativeTarget(dtor + kWorldDtorDeleteCall)));
    g_renderViewForIndex = reinterpret_cast<RenderViewForIndexFn>(const_cast<std::byte*>(forIndex));
    if (!g_new || !g_viewCtor || !g_viewDtor || !g_delete || g_worldSlot >= 0) {
        EVR_LOG("stereo: engine functions for the second render view not found; two views off");
        return false;
    }
    EVR_LOG("stereo: world at rbp%+d; new RVA 0x%X, view ctor 0x%X, view dtor 0x%X, delete 0x%X",
            static_cast<int>(g_worldSlot), rvaOf(text, reinterpret_cast<const void*>(g_new)),
            rvaOf(text, reinterpret_cast<const void*>(g_viewCtor)),
            rvaOf(text, reinterpret_cast<const void*>(g_viewDtor)),
            rvaOf(text, reinterpret_cast<const void*>(g_delete)));
    if (const std::byte* rs = findUniqueInText(text, "render system", kRenderSystemSignature)) {
        g_renderSystem = ripTarget(rs + 14, rs + 18);
    }
    if (const std::byte* be = findUniqueInText(text, "render backend pointer", kBackendSignature)) {
        g_backendPointer = reinterpret_cast<std::byte* const*>(ripTarget(be + 3, be + 7));
    }
    // The destructor hook first: a second view must never outlive its world.
    return install(text, dtor, 0, &onWorldDtorHook, "world destructor") &&
           installEdit(text, visCount, kVisibilityCountPatch, &onVisibilityCountHook,
                       "visibility context count") &&
           installEdit(text, visOfView, kVisibilityOfViewPatch, &onVisibilityOfViewHook,
                       "visibility context of a view") &&
           install(text, layout, kLayoutReadPatch, &onLayoutHook, "screen layout");
}

} // namespace

StereoHookStatus installStereoHooks(StereoHookSink* sink, bool twoViews) {
    setStereoHookSink(sink);
    std::call_once(g_installOnce, [twoViews] {
        GameText text;
        if (!findGameText(text)) {
            EVR_LOG("stereo: the game module has no readable .text; stereo hooks off");
            return;
        }
        g_status.eyeView = installEyeHooks(text);
        g_status.twoViews = g_status.eyeView && twoViews && installTwoViewHooks(text);
        EVR_LOG("stereo: per-eye hooks %s, two views %s", g_status.eyeView ? "on" : "OFF",
                g_status.twoViews ? "on" : (twoViews ? "OFF (hooks missing)" : "not asked for"));
    });
    return g_status;
}

StereoExperiment stereoExperimentFromEnv() {
    std::wstring mode;
    std::wstring experiment;
    if (!readEnv(L"ETERNALVR_MODE", mode) || _wcsicmp(mode.c_str(), L"stereo") != 0 ||
        !readEnv(L"ETERNALVR_STEREO_EXPERIMENT", experiment)) {
        return StereoExperiment::None;
    }
    if (_wcsicmp(experiment.c_str(), L"left-eye") == 0) {
        return StereoExperiment::LeftEye;
    }
    if (_wcsicmp(experiment.c_str(), L"two-views") == 0) {
        return StereoExperiment::TwoViews;
    }
    return StereoExperiment::None;
}

void startStereoHooksEarly() {
    if (stereoExperimentFromEnv() != StereoExperiment::TwoViews) {
        return;
    }
    constexpr bool two = true;
    // The thread's code must outlive any unload of the layer.
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&startStereoHooksEarly), &self);
    std::thread([two] {
        if (!mp_guard::allowsGameTouch()) {
            EVR_LOG("stereo: the multiplayer guard is not armed; the two-view experiment stays off");
            return;
        }
        const StereoHookStatus status = installStereoHooks(nullptr, two);
        requestTwoViews(two && status.twoViews);
    }).detach();
}

void setStereoHookSink(StereoHookSink* sink) {
    std::unique_lock lock(g_sinkMutex);
    g_sink = sink;
}

void requestTwoViews(bool enabled) {
    g_wantTwoViews.store(enabled && g_status.twoViews);
}

EngineFrameCounters readEngineFrameCounters() {
    EngineFrameCounters counters;
    if (g_renderSystem) {
        std::memcpy(&counters.renderFrames, g_renderSystem + 0x10, sizeof(std::uint32_t));
    }
    if (g_backendPointer && *g_backendPointer) {
        std::memcpy(&counters.backendFrames, *g_backendPointer + 0xB0, sizeof(std::uint32_t));
    }
    return counters;
}

} // namespace evr::vkcore
