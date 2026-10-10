// Parallel Eye Rendering with DLSS (view_dlss.hpp): one lock around the engine's DLSS create and evaluate,
// each view's results and resets, the fallback to TAA, view 1's feature released.

#include "vkcore/view_dlss.hpp"

#include "stereo_seq/ngx_twin_retry.hpp"
#include "vkcore/game_code.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/taa_hooks.hpp"
#include "vkcore/view_clones.hpp"
#include "vkcore/view_dlss_plan.hpp"
#include "vkcore/view_slots.hpp"
#include "vkcore/window_timing.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "view-dlss";

// ---- Sites (RVAs in build 25216728) ----

// The post-process pass's DLSS branch (0x1C9B5D0) calls create at 0x1C9B6A7 and evaluate at 0x1C9B88D with
// the pass's command context in rcx; the anti-aliasing pass (0x1C9B560) releases that context's feature at
// 0x1C9B58D when DLSS is switched off (both views' passes at once on a switch to TAA). The render size asks
// NGX for the optimal render size through the same parameter block (0x1CC5D40, called from 0x1CBFAAE and
// 0x1CC0904). These are the functions' only call sites.
constexpr std::uint32_t kCreate = 0x1CC5760;   // (context, render size, output size)
constexpr std::uint32_t kEvaluate = 0x1CC5B30; // (context, inputs, output) -> bool
constexpr std::uint32_t kRelease = 0x1CC5F00;  // (context): releases [context + 0x190], clears +0x190..+0x1AF
constexpr std::uint32_t kRenderSize = 0x1CC5D40; // (size, settings out)
constexpr std::uint32_t kResetSite = 0x1CC5CE0;
// Evaluate's parameters for NGX's helper (0x1CC7AA0) start at rsp + 0x30 (lea r9 at 0x1CC5CE7); their Reset
// (+0x38) is written at 0x1CC5CC4 from r_dlssForceReset, read by the helper at 0x1CC7C89 into "Reset".
constexpr std::size_t kResetFromRsp = 0x68;
constexpr std::size_t kFeature = 0x190; // the context's DLSS feature handle
// The command context table (13 categories of 4 slots, view_contexts.cpp): POST_PROCESS_GUI is category 11.
constexpr std::uint32_t kTable = 0x667F018;
constexpr int kPostProcessSlot0 = 11 * 4;
constexpr std::uint32_t kParameterBlock = 0x66E8B28;
constexpr std::uint32_t kForceReset = 0x66E8E30; // r_dlssForceReset's cvar object
constexpr std::uint32_t kShutdown = 0x2268FC0;   // the NVSDK_NGX_VULKAN_Shutdown export (0x1CC88AA)

struct Bytes {
    std::uint32_t rva;
    const char* what;
    std::uint8_t bytes[16];
    std::size_t size;
};
constexpr Bytes kBytes[] = {
    {kCreate,
     "the DLSS create",
     {0x40, 0x53, 0x55, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x60},
     13},
    {0x1CC57CE, "the create's lea r15, [rbp + 0x190]", {0x4C, 0x8D, 0xBD, 0x90, 0x01, 0x00, 0x00}, 7},
    {kEvaluate,
     "the DLSS evaluate",
     {0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x40, 0xFE, 0xFF, 0xFF},
     16},
    {0x1CC5CC1, "the evaluate's Reset store", {0x0F, 0x95, 0xC0, 0x89, 0x44, 0x24, 0x68}, 7},
    {kResetSite,
     "the evaluate's call setup",
     {0x48, 0x8B, 0x8E, 0x18, 0x01, 0x00, 0x00, 0x4C, 0x8D, 0x4C, 0x24, 0x30},
     12},
    {0x1CC5CF3, "the evaluate's handle load", {0x48, 0x8B, 0x96, 0x90, 0x01, 0x00, 0x00}, 7},
    {0x1CC7C89, "the helper's Reset read", {0x44, 0x8B, 0x43, 0x38, 0x48, 0x8D, 0x15}, 7},
    {kRelease,
     "the DLSS release",
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x89, 0x90, 0x01, 0x00, 0x00},
     16},
    {kShutdown, "NGX's shutdown", {0x33, 0xC9, 0xE9}, 3},
    {kRenderSize,
     "the optimal render size query",
     {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57},
     16},
};
struct Call {
    std::uint32_t at;
    std::uint32_t target;
};
constexpr Call kCalls[] = {{0x1C9B6A7, kCreate},
                           {0x1C9B88D, kEvaluate},
                           {0x1C9B58D, kRelease},
                           {0x1CBFAAE, kRenderSize},
                           {0x1CC0904, kRenderSize}};
struct RipLoad {
    std::uint32_t at; // the instruction; its disp32 at +3, 7 bytes long
    std::uint32_t target;
};
constexpr RipLoad kLoads[] = {{0x1CC5CEC, kParameterBlock},
                              {0x1CC58EC, kParameterBlock},
                              {0x1CC5D9B, kParameterBlock},
                              {0x1CC5BFB, kForceReset}};

using CreateFn = void (*)(void* context, const std::int32_t* renderSize, const std::int32_t* outputSize);
using EvaluateFn = bool (*)(void* context, const void* inputs, void* output);
using ReleaseFn = void (*)(void* context);
using RenderSizeFn = void (*)(const std::int32_t* size, void* settings);
using ShutdownFn = int (*)();

constexpr std::uint64_t kLogEveryMs = 10000;
constexpr std::uint64_t kReleaseAfter = 8; // view 0's evaluations without view 1 before its feature goes
constexpr int kLoggedCreates = 16;

const std::byte* g_base = nullptr;
CreateFn g_create = nullptr;
EvaluateFn g_evaluate = nullptr;
ReleaseFn g_release = nullptr; // the engine's release (the hook's trampoline): under g_engineMutex only
RenderSizeFn g_renderSize = nullptr;
ShutdownFn g_shutdown = nullptr;
std::atomic<bool> g_installed{false};
std::atomic<bool> g_shared{false}; // a DLSS pass on another context: TAA for the session

// One of the engine's NGX calls at a time: create, evaluate, release and the render size query (the parameter
// block and r_dlssForceReset are the engine's one, and NGX is called from both views' threads).
std::mutex g_engineMutex;
thread_local int t_view = -1; // the view whose evaluate this thread runs, for the Reset site

// Under g_engineMutex.
view_dlss::ResetPlan g_plan;
std::uint64_t g_view0SinceView1 = 0;
std::uint64_t g_lastLogMs = 0;

std::mutex g_stateMutex; // g_health, g_retry, g_automaticTries
view_dlss::Health g_health;
stereo_seq::NgxTwinRetry g_retry;
int g_automaticTries = 0;            // tries started after a wait (view_dlss::kAutomaticTries a session)
std::atomic<bool> g_fallback{false}; // g_retry.fallback(), read without the lock

struct Counters {
    std::atomic<std::uint64_t> evaluates[2]{};
    std::atomic<std::uint64_t> failed[2]{};
    std::atomic<std::uint64_t> creates[2]{};
    std::atomic<std::uint64_t> layerResets[2]{};
    std::atomic<std::uint64_t> engineResets[2]{};
    std::atomic<std::uint64_t> waits{0}; // calls that found the lock taken
    std::atomic<std::uint64_t> waitMicros{0};
    std::atomic<std::uint64_t> switches{0}; // DLSS to TAA and back (each one makes view 1's clones again)
    std::atomic<int> loggedCreates{0};
} g_counters;

std::uint64_t nowMs() {
    return window_timing::nowMicros() / 1000;
}

void* tableCell(int slot) {
    void* context = nullptr;
    std::memcpy(&context,
                g_base + kTable + static_cast<std::size_t>(kPostProcessSlot0 + slot) * sizeof(void*),
                sizeof(context));
    return context;
}

// 0 or 1 for a view's post-process context, -1 for any other.
int viewOf(const void* context) {
    if (context && context == tableCell(0)) {
        return 0;
    }
    return context && context == tableCell(1) ? 1 : -1;
}

void* featureOf(const void* context) {
    void* feature = nullptr;
    std::memcpy(&feature, static_cast<const std::byte*>(context) + kFeature, sizeof(feature));
    return feature;
}

unsigned featureId(const void* feature) {
    return feature ? *static_cast<const unsigned*>(feature) : 0u;
}

std::unique_lock<std::mutex> lockEngine() {
    std::unique_lock lock(g_engineMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        const std::uint64_t from = window_timing::nowMicros();
        lock.lock();
        g_counters.waits.fetch_add(1, std::memory_order_relaxed);
        g_counters.waitMicros.fetch_add(window_timing::nowMicros() - from, std::memory_order_relaxed);
    }
    return lock;
}

// A DLSS pass on neither view's post-process context: both views would share its one feature.
void sharedContext(const void* context) {
    if (!g_shared.exchange(true)) {
        EVR_LOG(
            "%s: a DLSS pass on context %p, neither view's post-process context: TAA in both eyes for the "
            "session (r_antialiasing 1)",
            kTag, context);
    }
}

std::string nextTry() {
    return g_retry.exhausted() || g_automaticTries >= view_dlss::kAutomaticTries
               ? std::string("no try left until DLSS is chosen in the game's video menu")
               : "next try in " + std::to_string(stereo_seq::ngxTwinRetryMs(g_retry.failures()) / 1000) +
                     " s";
}

void noteResult(int view, bool ok) {
    g_counters.evaluates[view].fetch_add(1, std::memory_order_relaxed);
    if (!ok) {
        g_counters.failed[view].fetch_add(1, std::memory_order_relaxed);
    }
    std::lock_guard lock(g_stateMutex);
    switch (g_health.result(view, ok)) {
    case view_dlss::Verdict::Failed:
        if (g_retry.fallback()) {
            g_retry.failed(nowMs()); // the other view's run, the same fallback: counted and logged once
            break;
        }
        g_counters.switches.fetch_add(1, std::memory_order_relaxed);
        g_retry.failed(nowMs());
        g_fallback.store(true);
        EVR_LOG(
            "%s: view %d's DLSS evaluation failed %d times in a row: TAA in both eyes (r_antialiasing 1), "
            "failure %u in a row, %s",
            kTag, view, view_dlss::kFailuresInARow, g_retry.failures(), nextTry().c_str());
        break;
    case view_dlss::Verdict::Recovered:
        if (g_retry.created()) {
            EVR_LOG("%s: DLSS in both eyes again after a fallback (each view %d evaluations in a row)", kTag,
                    view_dlss::kRecoveredEvaluates);
        }
        break;
    case view_dlss::Verdict::None:
        break;
    }
}

void logCounts(std::uint64_t now) {
    if (now - g_lastLogMs < kLogEveryMs) {
        return;
    }
    g_lastLogMs = now;
    const auto n = [](const std::atomic<std::uint64_t>& c) {
        return static_cast<unsigned long long>(c.load(std::memory_order_relaxed));
    };
    EVR_LOG(
        "%s: evaluates view 0 %llu (failed %llu), view 1 %llu (failed %llu); features made %llu, %llu; Reset "
        "raised by the layer %llu, %llu, by the engine %llu, %llu; lock waits %llu (%llu us); DLSS and TAA "
        "switches %llu; %s",
        kTag, n(g_counters.evaluates[0]), n(g_counters.failed[0]), n(g_counters.evaluates[1]),
        n(g_counters.failed[1]), n(g_counters.creates[0]), n(g_counters.creates[1]),
        n(g_counters.layerResets[0]), n(g_counters.layerResets[1]), n(g_counters.engineResets[0]),
        n(g_counters.engineResets[1]), n(g_counters.waits), n(g_counters.waitMicros), n(g_counters.switches),
        g_fallback.load() ? "TAA (fallback)" : "DLSS in both eyes");
}

// View 1 stopped for good (a guard trip: view 0 alone from then on; the clones off for the process): its
// feature is released once view 0 evaluated a few times without it, so no frame in flight still records it.
// Under g_engineMutex, on view 0's evaluation.
void releaseLostView1() {
    void* context1 = tableCell(1);
    if (g_view0SinceView1 < kReleaseAfter || !context1 || !featureOf(context1) ||
        (parallelEyesTouch() && !viewClonesStopped())) {
        return;
    }
    const unsigned id = featureId(featureOf(context1));
    g_release(context1);
    EVR_LOG("%s: view 1's DLSS feature %u released (%s: view 0 alone from now on)", kTag, id,
            parallelEyesTouch() ? "the clones are off" : "the multiplayer guard tripped");
}

void createHook(void* context, const std::int32_t* renderSize, const std::int32_t* outputSize) {
    const auto lock = lockEngine();
    const int view = viewOf(context);
    if (view < 0) {
        sharedContext(context);
    }
    void* before = featureOf(context);
    g_create(context, renderSize, outputSize);
    void* after = featureOf(context);
    if (view < 0 || after == before) {
        return;
    }
    if (after) {
        g_counters.creates[view].fetch_add(1, std::memory_order_relaxed);
    }
    if (g_counters.loggedCreates.fetch_add(1) < kLoggedCreates || !after) {
        const std::string made =
            after ? "created: " + std::to_string(featureId(after)) : "could not be created";
        EVR_LOG("%s: view %d's DLSS feature %s (%dx%d -> %dx%d)", kTag, view, made.c_str(),
                renderSize ? renderSize[0] : 0, renderSize ? renderSize[1] : 0,
                outputSize ? outputSize[0] : 0, outputSize ? outputSize[1] : 0);
    }
}

bool evaluateHook(void* context, const void* inputs, void* output) {
    const auto lock = lockEngine();
    const int view = viewOf(context);
    if (view < 0) {
        sharedContext(context);
        return g_evaluate(context, inputs, output);
    }
    t_view = view;
    const bool ok = g_evaluate(context, inputs, output);
    t_view = -1;
    noteResult(view, ok);
    if (view == 1) {
        g_view0SinceView1 = 0;
    } else {
        ++g_view0SinceView1;
        releaseLostView1();
    }
    logCounts(nowMs());
    return ok;
}

// In the evaluate (rsp is its frame), after the engine set Reset from r_dlssForceReset, before NGX's helper.
// Under g_engineMutex (the evaluate hook holds it on this thread).
void onResetSite(HookRegisters& r) {
    const int view = t_view;
    if (view < 0) {
        return;
    }
    auto* reset = reinterpret_cast<std::int32_t*>(r.rsp + kResetFromRsp);
    const bool engine = *reset != 0;
    if (engine) {
        g_counters.engineResets[view].fetch_add(1, std::memory_order_relaxed);
    }
    if (!parallelEyesTouch() || !g_plan.evaluate(view, engine, viewSlotsView1Rendered())) {
        return;
    }
    *reset = 1;
    g_counters.layerResets[view].fetch_add(1, std::memory_order_relaxed);
}

// The anti-aliasing pass releases its context's feature (DLSS switched off): both views' passes at once.
void releaseHook(void* context) {
    const auto lock = lockEngine();
    g_release(context);
}

// The render size asks NGX for the optimal size, through the parameter block a create reads.
void renderSizeHook(const std::int32_t* size, void* settings) {
    const auto lock = lockEngine();
    g_renderSize(size, settings);
}

// The engine shuts NGX down (0x1CC8890): view 1's feature, which the engine never releases itself, first.
int shutdownHook() {
    {
        const auto lock = lockEngine();
        void* context1 = tableCell(1);
        if (context1 && featureOf(context1)) {
            const unsigned id = featureId(featureOf(context1));
            g_release(context1);
            EVR_LOG("%s: view 1's DLSS feature %u released before NGX shuts down", kTag, id);
        }
    }
    return g_shutdown();
}

bool matches(const Bytes& b) {
    return std::memcmp(g_base + b.rva, b.bytes, b.size) == 0;
}

bool hookFailed(const char* what, std::uint32_t rva, const std::string& error) {
    EVR_LOG("%s: %s hook at RVA 0x%X failed: %s; TAA in both eyes for the session", kTag, what, rva,
            error.c_str());
    return false;
}

bool installHooks() {
    std::string error;
    auto* base = const_cast<std::byte*>(g_base);
    // Release before evaluate and create: every feature they make for view 1 can be released. The layer's own
    // releases (under the lock) call the trampoline.
    if (!installInlineHook(base + kRelease, reinterpret_cast<void*>(&releaseHook),
                           reinterpret_cast<void**>(&g_release), error)) {
        return hookFailed("release", kRelease, error);
    }
    if (!installInlineHook(base + kRenderSize, reinterpret_cast<void*>(&renderSizeHook),
                           reinterpret_cast<void**>(&g_renderSize), error)) {
        return hookFailed("render size query", kRenderSize, error);
    }
    if (!installInlineHook(base + kShutdown, reinterpret_cast<void*>(&shutdownHook),
                           reinterpret_cast<void**>(&g_shutdown), error)) {
        return hookFailed("NGX shutdown", kShutdown, error);
    }
    if (!installMidHookEdit(base + kResetSite, &onResetSite, error)) {
        return hookFailed("Reset", kResetSite, error);
    }
    if (!installInlineHook(base + kEvaluate, reinterpret_cast<void*>(&evaluateHook),
                           reinterpret_cast<void**>(&g_evaluate), error)) {
        return hookFailed("evaluate", kEvaluate, error);
    }
    if (!installInlineHook(base + kCreate, reinterpret_cast<void*>(&createHook),
                           reinterpret_cast<void**>(&g_create), error)) {
        return hookFailed("create", kCreate, error);
    }
    return true;
}

} // namespace

bool prepareViewDlss(const std::byte* base) {
    if (!parallelEyesSettings().dlss) {
        return true;
    }
    g_base = base;
    const char* off = "Parallel Eye Rendering stays off with DLSS (the standard renderer runs DLSS per eye)";
    for (const Bytes& b : kBytes) {
        if (!matches(b)) {
            EVR_LOG("%s: RVA 0x%X is not %s as known; %s", kTag, b.rva, b.what, off);
            return false;
        }
    }
    for (const Call& c : kCalls) {
        if (relativeTarget(base + c.at) != base + c.target) {
            EVR_LOG("%s: RVA 0x%X does not call RVA 0x%X; %s", kTag, c.at, c.target, off);
            return false;
        }
    }
    for (const RipLoad& l : kLoads) {
        if (ripTarget(base + l.at + 3, base + l.at + 7) != base + l.target) {
            EVR_LOG("%s: RVA 0x%X does not read RVA 0x%X; %s", kTag, l.at, l.target, off);
            return false;
        }
    }
    const auto* reset = ripTarget(base + 0x1CC7C8D + 3, base + 0x1CC7C8D + 7);
    const auto* shutdown = reinterpret_cast<const std::byte*>(
        GetProcAddress(GetModuleHandleW(nullptr), "NVSDK_NGX_VULKAN_Shutdown"));
    if (std::memcmp(reset, "Reset", sizeof("Reset")) != 0 || shutdown != base + kShutdown || !tableCell(0)) {
        EVR_LOG(
            "%s: the helper's \"Reset\", the NVSDK_NGX_VULKAN_Shutdown export or the post-process context "
            "is not as known; %s",
            kTag, off);
        return false;
    }
    return true;
}

void installViewDlss() {
    if (!parallelEyesSettings().dlss || !g_base) {
        return;
    }
    if (!tableCell(1)) {
        EVR_LOG("%s: view 1 has no post-process context; TAA in both eyes for the session", kTag);
        return;
    }
    if (!installHooks()) {
        return;
    }
    g_installed.store(true);
    const int quality = taaDlssQuality();
    const std::string held = quality >= 0 ? std::to_string(quality) : std::string("the game's own");
    EVR_LOG(
        "%s: DLSS in both views, each with its own feature and history on its post-process context (view 1: "
        "%p); create (RVA 0x%X), evaluate (RVA 0x%X), release (RVA 0x%X) and the render size query (RVA "
        "0x%X) take one lock; Reset raised at RVA 0x%X; r_antialiasing 2 held, r_dlssQuality %s",
        kTag, tableCell(1), kCreate, kEvaluate, kRelease, kRenderSize, kResetSite, held.c_str());
}

bool viewDlssHolds(std::string_view name, std::string& value) {
    bool dlss = g_installed.load() && !g_shared.load();
    if (dlss && g_fallback.load()) {
        std::lock_guard lock(g_stateMutex);
        const std::uint32_t failures = g_retry.failures();
        if (g_automaticTries < view_dlss::kAutomaticTries && g_retry.due(nowMs())) {
            ++g_automaticTries;
            g_fallback.store(false);
            g_health.tryAgain();
            g_counters.switches.fetch_add(1, std::memory_order_relaxed);
            EVR_LOG("%s: trying DLSS again (%u failure(s) in a row; automatic try %d of %d this session, "
                    "then only "
                    "when DLSS is chosen in the game's video menu): r_antialiasing 2 held again, each view's "
                    "feature "
                    "is made at its next evaluation",
                    kTag, failures, g_automaticTries, view_dlss::kAutomaticTries);
        }
        dlss = !g_retry.fallback();
    }
    if (name == "r_antialiasing") {
        value = dlss ? "2" : "1";
        return true;
    }
    return dlss; // r_dlssQuality: only while DLSS runs
}

bool viewDlssRunning() {
    return g_installed.load() && !g_shared.load() && !g_fallback.load();
}

bool viewDlssFallback() {
    return g_installed.load() && !g_shared.load() && g_fallback.load();
}

bool viewDlssRetry() {
    std::lock_guard lock(g_stateMutex);
    if (!g_installed.load() || g_shared.load() || !g_retry.requested() || !g_retry.due(nowMs())) {
        return false;
    }
    g_fallback.store(false);
    g_health.tryAgain();
    g_counters.switches.fetch_add(1, std::memory_order_relaxed);
    EVR_LOG("%s: DLSS chosen in the game's video menu: trying DLSS in both views again", kTag);
    return true;
}

} // namespace evr::vkcore
