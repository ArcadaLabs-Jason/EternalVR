#include "vkcore/seq_hooks.hpp"

#include "stereo_seq/drain_backoff.hpp"
#include "stereo_seq/render_idle.hpp"
#include "stereo_seq/stack_budget.hpp"
#include "vkcore/bin_tile_hooks.hpp"
#include "vkcore/cpu_timing.hpp"
#include "vkcore/keep_prev_hooks.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/moved_flag_hooks.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/object_prev_hooks.hpp"
#include "vkcore/seq_alternate.hpp"
#include "vkcore/seq_locate.hpp"
#include "vkcore/seq_prev.hpp"
#include "vkcore/seq_stack.hpp"
#include "vkcore/stall_watch.hpp"
#include "vkcore/status_file.hpp"
#include "vkcore/test_cpu_load.hpp"
#include "vkcore/window_timing.hpp"
#include "vkcore/world_gui_hooks.hpp"

#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

namespace evr::vkcore {

using stereo_seq::Eye;

namespace {

// ---- Engine layouts ----

constexpr std::size_t kPacketRenderSystem = 0x00;
constexpr std::size_t kPacketFrameInfo = 0x08;
constexpr std::size_t kPacketArg = 0x10;
constexpr std::size_t kPacketFlag = 0x18;
constexpr std::size_t kRenderSystemGuard = 0x08;
constexpr std::size_t kRenderSystemFrame = 0x10;
constexpr std::size_t kRenderSystemSkipBackend = 0x14; // non-zero: the frame-end job kicks no backend frame
constexpr std::size_t kBackendFrame = 0xB0;
constexpr std::size_t kFrameInfoScreenshot = 0x2A44;
constexpr std::size_t kCvarIntValue = 0x08;

// Holding the frontend for a new tag base gives up after this long (stereo_seq/render_idle.hpp decides
// when the render thread is idle); how long stereo then waits before the next try is
// stereo_seq/drain_backoff.hpp's.
constexpr ULONGLONG kDrainTimeoutMs = 250;
constexpr ULONGLONG kDrainSpacingMs = 1000;
// Stereo resuming after this many mono frames takes a fresh tag base.
constexpr std::uint32_t kRebaseAfterMono = 30;

// g_marks bits, set by the per-eye hook for the chain that is running.
constexpr std::uint32_t kMarkLeft = 1;   // eye L's view written
constexpr std::uint32_t kMarkRight = 2;  // eye R's view written
constexpr std::uint32_t kMarkWanted = 4; // eye L had a stereo view, but the tags need a base first

using JobFn = void (*)(void*, void*, void*, void*);
using RenderOneFn = void (*)(void* renderSystem, void* arg, void* frameInfo, std::uint8_t flag);

std::once_flag g_installOnce;
bool g_installed = false;
std::atomic<bool> g_active{false};
std::atomic<bool> g_allowed{true};

void** g_slot = nullptr;
JobFn g_frameEnd = nullptr;
RenderOneFn g_renderOne = nullptr;
std::byte* g_renderSystem = nullptr;
std::byte* const* g_backend = nullptr;
const std::byte* const* g_swapIntervalCvar = nullptr;

std::atomic<bool> g_inRight{false};
std::atomic<std::uint64_t> g_rightTick{0};
std::atomic<std::uint32_t> g_marks{0};
std::atomic<std::uint64_t> g_markTick{0};
std::atomic<std::uint32_t> g_monoStreak{kRebaseAfterMono};
std::atomic<ULONGLONG> g_nextDrainTicks{0}; // no drain before this (after a failure or a recent drain)
stereo_seq::DrainBackoff g_backoff;         // drainAndRebase only (the frontend, one frame-end job at a time)

std::mutex g_tagMutex; // g_tags and g_idle
stereo_seq::EyeTagQueue g_tags;
stereo_seq::RenderIdle g_idle;
bool g_alternate = false; // ETERNALVR_ALTERNATE_EYES on or auto, set once at install before g_active
bool g_adaptive = false;  // auto: a tick may render both eyes (seq_alternate::pairInTick)

struct Counters {
    std::atomic<std::uint64_t> frameEnds{0};
    std::atomic<std::uint64_t> stereoTicks{0};
    std::atomic<std::uint64_t> rightFrameEnds{0};
    std::atomic<std::uint64_t> guardBusy{0};
    std::atomic<std::uint64_t> stackSkips{0};
    std::atomic<std::uint64_t> guardTrips{0};
    std::atomic<std::uint64_t> noBackendFrames{0};
    std::atomic<std::uint64_t> drains{0};
    std::atomic<std::uint64_t> drainFailures{0};
    std::atomic<std::uint64_t> unverifiedBases{0};
    std::atomic<std::uint64_t> drainMicros{0};    // the frontend held by drains, in total
    std::atomic<std::uint64_t> drainMaxMicros{0}; // the longest drain since seqTakeDrainMaxMicros
    std::atomic<int> loggedDesyncs{0};
    std::atomic<int> loggedTicks{0};
    std::atomic<int> loggedStack{0};
} g_counters;

std::uint32_t readU32(const std::byte* at) {
    std::uint32_t v = 0;
    std::memcpy(&v, at, sizeof(v));
    return v;
}

std::uint32_t backendFrame() {
    const std::byte* backend = g_backend ? *g_backend : nullptr;
    return backend ? readU32(backend + kBackendFrame) : 0;
}

stereo_seq::StackPosition currentStack() {
    ULONG_PTR low = 0;
    ULONG_PTR high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    return {low, high, reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress())};
}

// Holds the frontend (this frame-end job, before its frame is handed over) until the render thread has
// presented every frame kicked so far, then takes the backend counter as the tag base.
bool drainAndRebase() {
    const ULONGLONG start = GetTickCount64();
    ++g_counters.drains;
    std::uint32_t last = backendFrame();
    ULONGLONG lastChange = start;
    bool counted = false;
    for (;;) {
        const ULONGLONG now = GetTickCount64();
        const std::uint32_t b = backendFrame();
        if (b != last) {
            last = b;
            lastChange = now;
        }
        stereo_seq::RenderIdle::Verdict verdict = stereo_seq::RenderIdle::Verdict::Wait;
        {
            std::lock_guard lock(g_tagMutex);
            verdict = g_idle.check(last, now - lastChange);
        }
        if (verdict != stereo_seq::RenderIdle::Verdict::Wait) {
            counted = verdict == stereo_seq::RenderIdle::Verdict::Idle;
            break;
        }
        if (now - start >= kDrainTimeoutMs) {
            ++g_counters.drainFailures;
            {
                std::lock_guard lock(g_tagMutex);
                g_idle.forget(); // the next base comes from a quiet period
            }
            const std::uint64_t wait = g_backoff.failed();
            g_nextDrainTicks.store(now + wait);
            EVR_LOG("seq: the render thread did not present every frame within %llu ms; mono for %llu ms (%u "
                    "failed drain(s) in a row)",
                    static_cast<unsigned long long>(kDrainTimeoutMs), static_cast<unsigned long long>(wait),
                    g_backoff.failures());
            return false;
        }
        Sleep(1);
    }
    {
        std::lock_guard lock(g_tagMutex);
        g_tags.rebase(last);
        g_idle.based(last);
    }
    if (!counted) {
        ++g_counters.unverifiedBases;
    }
    g_backoff.succeeded();
    // At most one drain per kDrainSpacingMs, so tags that keep falling out of sync cost a hitch a second at
    // worst (stereo is off until the next drain).
    g_nextDrainTicks.store(GetTickCount64() + kDrainSpacingMs);
    if (g_counters.drains.load() <= 20) {
        EVR_LOG("seq: render thread idle at backend frame %u after %llu ms (%s); eye tags start at %u", last,
                static_cast<unsigned long long>(GetTickCount64() - start),
                counted ? "every frame handed over presented" : "quiet, frames not counted", last + 1);
    }
    return true;
}

// A drain's wall time, for the 10 s summary and the stall line (stall_watch.hpp).
void noteDrainTime(std::uint64_t micros) {
    g_counters.drainMicros.fetch_add(micros, std::memory_order_relaxed);
    std::uint64_t longest = g_counters.drainMaxMicros.load(std::memory_order_relaxed);
    while (micros > longest &&
           !g_counters.drainMaxMicros.compare_exchange_weak(longest, micros, std::memory_order_relaxed)) {
    }
    stall_watch::addDrain(micros);
}

bool tagsSynced() {
    std::lock_guard lock(g_tagMutex);
    return g_tags.synced();
}

// Tags a frame about to be handed to the render thread (and counts it for the drains). False when the
// tags are out of step (nothing queued).
bool handOver(Eye eye, std::uint64_t tick, bool applied, std::uint32_t renderFrame, bool pairInTick = false) {
    stereo_seq::RenderTag tag;
    tag.eye = eye;
    tag.tick = tick;
    tag.viewApplied = applied;
    tag.pairInTick = pairInTick;
    tag.renderFrame = renderFrame;
    std::lock_guard lock(g_tagMutex);
    g_idle.kicked();
    return g_tags.push(tag);
}

// Eye R after eye L's frame-end job handed eye L over: the same frame again, as the loading screens
// render one, with the screenshot request cleared so that a screenshot is taken once. False when eye R
// was not rendered (eye L's half is then dropped by the pairing).
bool renderRightEye(std::byte* rs, void* arg, void* frameInfo, std::uint8_t flag, std::uint64_t tick) {
    if (rs[kRenderSystemGuard] != std::byte{0}) {
        // 0x1CBA3AA did not clear the guard (not expected): eye R would render nothing.
        ++g_counters.guardBusy;
        return false;
    }
    if (!mp_guard::allowsGameTouch()) {
        ++g_counters.guardTrips; // tripped since eye L: its half is dropped, no eye R
        return false;
    }
    const stereo_seq::StackPosition stack = currentStack();
    const std::size_t deepest = seq_stack::deepestNested();
    if (stereo_seq::stackKnown(stack)) {
        seq_stack::noteHeadroom(stereo_seq::stackHeadroom(stack));
    }
    if (g_counters.loggedStack.fetch_add(1) == 0) {
        EVR_LOG(
            "seq: eye L's frame end runs with %zu KiB of a %zu KiB stack left%s",
            stereo_seq::stackHeadroom(stack) / 1024, static_cast<std::size_t>(stack.high - stack.low) / 1024,
            stereo_seq::stackKnown(stack) ? ""
                                          : " (the stack pointer is outside the thread's stack: no check)");
    }
    if (!stereo_seq::nestedRenderFits(stack, deepest)) {
        if (g_counters.stackSkips.fetch_add(1) < 5) {
            EVR_LOG("seq: eye R skipped: %zu KiB of stack left, its chain went %zu KiB deep so far",
                    stereo_seq::stackHeadroom(stack) / 1024, deepest / 1024);
        }
        return false;
    }
    seq_stack::enter(stack.sp);
    auto* screenshot = static_cast<std::byte*>(frameInfo) + kFrameInfoScreenshot;
    std::int32_t savedScreenshot = 0;
    std::memcpy(&savedScreenshot, screenshot, sizeof(savedScreenshot));
    const std::int32_t zero = 0;
    std::memcpy(screenshot, &zero, sizeof(zero));
    g_rightTick.store(tick);
    g_inRight.store(true, std::memory_order_release);
    g_renderOne(rs, arg, frameInfo, flag);
    g_inRight.store(false, std::memory_order_release);
    seq_stack::leave();
    std::memcpy(screenshot, &savedScreenshot, sizeof(savedScreenshot));
    ++g_counters.stereoTicks;
    return true;
}

// The frame-end job's replacement. Every render frame of the engine ends here.
void frameEndWrapper(void* packet, void* a2, void* a3, void* a4) {
    seq_alternate::countRender();
    test_cpu_load::atFrameEnd(); // ETERNALVR_TEST_CPU_LOAD_MS, a test knob
    auto* p = static_cast<std::byte*>(packet);
    std::byte* rs = nullptr;
    std::memcpy(&rs, p + kPacketRenderSystem, sizeof(rs));
    // After a multiplayer guard trip (or before it arms) every frame is the engine's own, untouched.
    if (!g_active.load(std::memory_order_acquire) || rs != g_renderSystem || !mp_guard::allowsGameTouch()) {
        if (rs != g_renderSystem && g_active.exchange(false)) {
            EVR_LOG("seq: a frame-end job for render system %p, not %p; stereo off", static_cast<void*>(rs),
                    static_cast<void*>(g_renderSystem));
        }
        g_frameEnd(packet, a2, a3, a4);
        return;
    }
    const bool presents = readU32(rs + kRenderSystemSkipBackend) == 0;
    const std::uint32_t renderFrame = readU32(rs + kRenderSystemFrame);
    if (!presents) {
        ++g_counters.noBackendFrames;
    }
    const std::uint32_t marks = g_marks.exchange(0);
    const std::uint64_t markTick = g_markTick.load();

    if (g_inRight.load(std::memory_order_acquire)) {
        // Eye R's own chain (inside renderRightEye).
        ++g_counters.rightFrameEnds;
        seqNoteNestedStack();
        const std::uint64_t tick = g_rightTick.load();
        if (presents) {
            handOver(Eye::Right, tick, (marks & kMarkRight) != 0 && markTick == tick, renderFrame,
                     g_alternate);
        }
        g_frameEnd(packet, a2, a3, a4);
        return;
    }

    ++g_counters.frameEnds;
    cpu_timing::onFrontendTick(); // ETERNALVR_CPU_TIMING
    // The engine's own chain draws eye L, or with alternate eyes the eye of this render frame.
    const Eye eye = g_alternate ? seq_alternate::eyeFor(renderFrame) : Eye::Left;
    stereo_seq::FrameEndInput in;
    in.presents = presents;
    in.leftApplied = (marks & (eye == Eye::Right ? kMarkRight : kMarkLeft)) != 0;
    in.wanted = (marks & kMarkWanted) != 0;
    in.synced = tagsSynced();
    in.rebaseDue = g_monoStreak.load() >= kRebaseAfterMono;
    in.drainAllowed = GetTickCount64() >= g_nextDrainTicks.load();
    const stereo_seq::FrameEndPlan plan = stereo_seq::planFrameEnd(in);
    bool stereo = plan.stereo;
    if (plan.drain) {
        if (in.synced) {
            std::lock_guard lock(g_tagMutex);
            g_tags.desync(stereo_seq::DesyncReason::Requested);
        }
        const std::uint64_t drainStart = window_timing::nowMicros();
        const bool based = [] {
            cpu_timing::Scope timed(cpu_timing::Stage::Drain);
            return drainAndRebase();
        }();
        noteDrainTime(window_timing::nowMicros() - drainStart);
        if (based) {
            g_monoStreak.store(0);
        }
        stereo = stereo && based;
    }
    // Auto: eye L of a tick that renders both eyes (Route S; decided at the render's first ask).
    const bool pairTick = g_adaptive && eye == Eye::Left && seq_alternate::pairInTick(renderFrame);
    if (presents) {
        const bool pushed =
            handOver(stereo ? eye : Eye::Mono, markTick, in.leftApplied, renderFrame, stereo && pairTick);
        stereo = stereo && pushed;
    }
    // Noted before the original clears the render-frame guard, so the next render frame is decided after it.
    if (g_alternate && stereo && pairTick) {
        seq_alternate::rendered(renderFrame, eye, true, true); // eye R nested below: the next pair starts
    } else if (g_alternate) {                                  // one eye per tick: no eye R render
        seq_alternate::rendered(renderFrame, eye, stereo);
        g_monoStreak.store(stereo ? 0 : g_monoStreak.load() + 1);
        cpu_timing::Scope timed(cpu_timing::Stage::FrameEndLeft);
        g_frameEnd(packet, a2, a3, a4);
        return;
    }
    // The packet belongs to the calling job list and stays valid, but read it before the original runs.
    void* frameInfo = nullptr;
    void* arg = nullptr;
    std::memcpy(&frameInfo, p + kPacketFrameInfo, sizeof(frameInfo));
    std::memcpy(&arg, p + kPacketArg, sizeof(arg));
    const auto flag = std::to_integer<std::uint8_t>(p[kPacketFlag]);

    {
        cpu_timing::Scope timed(cpu_timing::Stage::FrameEndLeft);
        g_frameEnd(packet, a2, a3, a4);
    }

    if (!stereo) {
        g_monoStreak.fetch_add(1);
        return;
    }
    g_monoStreak.store(0);
    const std::uint64_t rightStart = g_adaptive ? window_timing::nowMicros() : 0;
    cpu_timing::Scope timed(cpu_timing::Stage::RightEye);
    const bool right = renderRightEye(rs, arg, frameInfo, flag, markTick);
    if (right && g_counters.loggedTicks.fetch_add(1) < 3) {
        EVR_LOG("seq: stereo tick for game frame %llu (render frame %u): eye L then eye R rendered",
                static_cast<unsigned long long>(markTick), renderFrame);
    }
    if (g_adaptive) {
        seq_alternate::rightRendered(right, window_timing::nowMicros() - rightStart);
    }
}

void onPrevStoredHook(const HookRegisters& regs) {
    if (!g_active.load(std::memory_order_acquire) || !mp_guard::allowsGameTouch()) {
        return;
    }
    auto* view = reinterpret_cast<std::byte*>(regs.rdi);
    if (!view) {
        return;
    }
    const Eye eye = seqRenderEye();
    if (eye == Eye::Right) {
        seqNoteNestedStack();
    }
    // The render-frame job raised the counter before this frame's world-views pass.
    const std::uint32_t renderFrame = readU32(g_renderSystem + kRenderSystemFrame);
    seq_prev::afterStore(view, eye, renderFrame);
}

// The wrapper and the hooks' code must outlive any unload of the layer: the slot keeps pointing at it.
bool pinSelf() {
    HMODULE self = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&frameEndWrapper), &self) != FALSE;
}

} // namespace

bool installSeqHooks(const SeqHookSettings& settings) {
    std::call_once(g_installOnce, [&settings] {
        GameText text;
        if (!findGameText(text)) {
            EVR_LOG("seq: the game module has no readable .text; stereo off");
            return;
        }
        SeqEngine engine;
        if (!locateSeqEngine(text, engine)) {
            return;
        }
        if (!pinSelf()) {
            EVR_LOG("seq: the layer cannot be pinned in memory; stereo off");
            return;
        }
        g_slot = engine.slot;
        g_frameEnd = reinterpret_cast<JobFn>(const_cast<std::byte*>(engine.frameEnd));
        g_renderOne = reinterpret_cast<RenderOneFn>(const_cast<std::byte*>(engine.renderOne));
        g_renderSystem = engine.renderSystem;
        g_backend = engine.backend;
        g_swapIntervalCvar = engine.swapIntervalCvar;
        if (settings.prevMatrices) {
            std::string error;
            if (!installMidHook(const_cast<std::byte*>(engine.prevHookSite), &onPrevStoredHook, error)) {
                EVR_LOG("seq: previous-matrix hook failed: %s; stereo off", error.c_str());
                return;
            }
            EVR_LOG("seq: previous-matrix hook at RVA 0x%X", rvaOf(text, engine.prevHookSite));
        }
        // Last: the swap itself. The hooks above do nothing until g_active is set.
        void* expected = reinterpret_cast<void*>(g_frameEnd);
        void* swapped =
            InterlockedCompareExchangePointer(g_slot, reinterpret_cast<void*>(&frameEndWrapper), expected);
        if (swapped != expected) {
            EVR_LOG("seq: the frame-end slot changed to %p while installing; stereo off", swapped);
            return;
        }
        g_alternate = settings.alternateEyes; // before g_active: the wrapper and the hooks read it
        g_adaptive = g_alternate && settings.adaptiveEyes;
        seq_alternate::configure(g_adaptive ? stereo_seq::AlternateMode::Auto
                                            : stereo_seq::AlternateMode::On);
        g_installed = true;
        g_active.store(true, std::memory_order_release);
        installBinTileHook(); // lights and decals binned in each eye's own frustum; a missing piece only logs
        installObjectPrevHooks(); // the object ring, a slot per render: eye R's objects keep their motion
        installWorldGuiHook();    // world GUIs (holograms, screens) in eye R too
        installMovedFlagHooks();  // moving objects keep their motion vectors in eye R
        installKeepPrevHooks();   // and their previous model matrix from eye L
        EVR_LOG("seq: frame-end job wrapped; per-eye previous matrices %s%s",
                settings.prevMatrices ? "on" : "off",
                g_adaptive    ? "; adaptive eyes (eye R nested while the processor keeps up)"
                : g_alternate ? "; alternate eyes (no eye R render)"
                              : "");
    });
    return g_installed;
}

bool seqHooksActive() {
    return g_active.load(std::memory_order_acquire);
}

void seqSetStereoAllowed(bool allowed) {
    g_allowed.store(allowed);
    if (!allowed) {
        status::stereo(false, "the anti-aliasing settings stereo needs could not be held");
    }
}

SeqReadiness seqStereoReadiness() {
    if (!g_active.load(std::memory_order_acquire) || !g_allowed.load(std::memory_order_relaxed) ||
        !mp_guard::allowsGameTouch()) {
        return SeqReadiness::Off;
    }
    return tagsSynced() && g_monoStreak.load() < kRebaseAfterMono ? SeqReadiness::Ready
                                                                  : SeqReadiness::NeedsBase;
}

void seqMarkWanted() {
    g_marks.fetch_or(kMarkWanted);
}

Eye seqChainEye() {
    return g_inRight.load(std::memory_order_acquire) ? Eye::Right : Eye::Left;
}

Eye seqRenderEye() {
    if (!g_alternate || !g_renderSystem || g_inRight.load(std::memory_order_acquire)) {
        return seqChainEye();
    }
    return seq_alternate::eyeFor(readU32(g_renderSystem + kRenderSystemFrame));
}

bool seqAlternateEyes() {
    return g_alternate && g_installed;
}

bool seqAdaptiveEyes() {
    return g_adaptive && g_installed;
}

bool seqPairInTick() {
    if (!g_alternate || !g_renderSystem || g_inRight.load(std::memory_order_acquire)) {
        return true; // Route S, or eye R nested in its eye L's tick
    }
    return seq_alternate::pairInTick(readU32(g_renderSystem + kRenderSystemFrame));
}

void seqMarkEyeView(Eye eye, std::uint64_t tick) {
    g_markTick.store(tick);
    g_marks.fetch_or(eye == Eye::Right ? kMarkRight : kMarkLeft);
}

std::uint64_t seqRightTick() {
    return g_rightTick.load();
}

stereo_seq::PresentMatch seqTakePresent() {
    if (!g_active.load(std::memory_order_acquire)) {
        return {};
    }
    const std::uint32_t backend = backendFrame();
    std::lock_guard lock(g_tagMutex);
    const bool wasSynced = g_tags.synced();
    const stereo_seq::PresentMatch match = g_tags.pop(backend);
    if (wasSynced && !g_tags.synced() && g_counters.loggedDesyncs.fetch_add(1) < 20) {
        EVR_LOG("seq: eye tags out of sync at backend frame %u: %s; mono until the next base", backend,
                stereo_seq::desyncReasonName(g_tags.lastDesync()));
    }
    return match;
}

std::optional<std::uint32_t> seqBackendCounter() {
    return g_active.load(std::memory_order_acquire) ? std::optional<std::uint32_t>(backendFrame())
                                                    : std::nullopt;
}

std::optional<stereo_seq::RenderTag> seqTagInFlight() {
    const std::optional<std::uint32_t> counter = seqBackendCounter();
    return counter ? seqTagForBackendFrame(*counter + 1) : std::nullopt;
}

std::optional<stereo_seq::RenderTag> seqTagForBackendFrame(std::uint32_t frame) {
    if (!g_active.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    std::lock_guard lock(g_tagMutex);
    const stereo_seq::RenderTag* tag = g_tags.peek(frame);
    if (!tag) {
        return std::nullopt;
    }
    return *tag;
}

SeqCounters seqCounters() {
    SeqCounters c;
    c.frameEnds = g_counters.frameEnds.load();
    c.stereoTicks = g_counters.stereoTicks.load();
    c.rightFrameEnds = g_counters.rightFrameEnds.load();
    c.guardBusy = g_counters.guardBusy.load();
    c.stackSkips = g_counters.stackSkips.load();
    c.guardTrips = g_counters.guardTrips.load();
    c.noBackendFrames = g_counters.noBackendFrames.load();
    c.drains = g_counters.drains.load();
    c.drainFailures = g_counters.drainFailures.load();
    c.unverifiedBases = g_counters.unverifiedBases.load();
    c.drainMicros = g_counters.drainMicros.load();
    c.deepestNested = seq_stack::deepestNested();
    c.leastHeadroom = seq_stack::leastHeadroom();
    const stereo_seq::PrevMatrixBook::Stats prev = seq_prev::stats();
    c.prevRewrites = prev.rewrites;
    c.prevKept = prev.kept;
    c.prevUndone = prev.undone;
    const stereo_seq::EyeAlternator::Stats alt = seq_alternate::stats();
    c.altRenders[0] = alt.renders[0];
    c.altRenders[1] = alt.renders[1];
    {
        std::lock_guard lock(g_tagMutex);
        c.tags = g_tags.stats();
        c.tagsSynced = g_tags.synced();
    }
    if (g_renderSystem) {
        c.renderFrames = readU32(g_renderSystem + kRenderSystemFrame);
    }
    c.backendFrames = backendFrame();
    if (g_swapIntervalCvar && *g_swapIntervalCvar) {
        c.swapInterval = static_cast<std::int32_t>(readU32(*g_swapIntervalCvar + kCvarIntValue));
    }
    return c;
}

std::uint64_t seqTakeDrainMaxMicros() {
    return g_counters.drainMaxMicros.exchange(0, std::memory_order_relaxed);
}

} // namespace evr::vkcore
