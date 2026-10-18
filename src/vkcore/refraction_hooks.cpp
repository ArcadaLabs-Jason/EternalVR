#include "vkcore/refraction_hooks.hpp"

#include "stereo_seq/refraction_history.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/refraction_locate.hpp"
#include "vkcore/refraction_resize.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/stereo_hooks.hpp"
#include "vkcore/taa_locate.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-refract";

// The argument block's images and the blend, from rsp.
constexpr std::size_t kArgMaskNow = 0x50;
constexpr std::size_t kArgMaskLast = 0x58;
constexpr std::size_t kArgWrite = 0x60;
constexpr std::size_t kArgRead = 0x68;
constexpr std::size_t kArgBlend = 0x90;

// The device context's pairs: the mask tiles [parity], the accumulation images and targets [parity * 2 +
// update]. A render target's colour image at +0x10.
constexpr std::size_t kDcMaskTiles = 0x488;
constexpr std::size_t kDcAccImages = 0x440;
constexpr std::size_t kDcAccTargets = 0x5F0;
constexpr std::size_t kTargetImage = 0x10;
// The transparency context (render context + 0x706CB0): its render view (+0x10, stored at RVA 0x1C601AA) and
// the backend counter the render-view job read for the render (+0xF8, stored at RVA 0x1C60253).
constexpr std::size_t kContextRenderView = 0x10;
constexpr std::size_t kContextCounter = 0xF8;
constexpr int kUpdates = stereo_seq::kRefractionUpdates;
// planFor's update for the mask pass.
constexpr int kMaskPass = -1;

using CreateImageFn = void* (*)(void* manager, const char* name, const void* description);

constexpr const char* kMaskNames[2] = {"_refractionMaskTiles32x0EyeR", "_refractionMaskTiles32x1EyeR"};
// [pair * 2 + update], as the engine's.
constexpr const char* kAccNames[4] = {
    "_refractionAccumulationBuffer00EyeR", "_refractionAccumulationBuffer01EyeR",
    "_refractionAccumulationBuffer10EyeR", "_refractionAccumulationBuffer11EyeR"};

// One eye's images by the slot of its own pairs (eye L: the engine's, slot = the engine's parity).
struct EyeSet {
    std::array<void*, 2> mask{};
    std::array<std::array<void*, 2>, kUpdates> acc{}; // [update][slot]
};

std::once_flag g_once;
std::atomic<bool> g_installed{false};
void* const* g_manager = nullptr;
CreateImageFn g_create = nullptr;
std::byte* const* g_dcGlobal = nullptr;
const std::byte* g_taaCvar = nullptr;  // r_refractionTAA, for the log
const std::byte* g_maskCvar = nullptr; // r_transparencyEnableRefractionMask
// rs_enable. Dynamic resolution scales the device context's refraction images of the counter's parity at
// each frame's start (RVA 0x1CDE950: the accumulation targets at RVA 0x1CDEDD2, the mask tiles at RVA
// 0x1CDEE78), which with the history per eye are not always the ones written, and never eye R's: the history
// is held off in stereo while it reads non-zero (the per-eye TAA set holds it at 0; Anti-aliasing Off and a
// failed-closed per-eye TAA do not), and both eyes start over when it is 0 again.
const std::byte* g_dynamicCvar = nullptr;

std::mutex g_mutex;
stereo_seq::RefractionHistory g_history;
std::byte* g_dc = nullptr; // the device context the sets belong to
std::byte* g_pendingDc = nullptr;
std::array<void*, 2> g_pendingMask{}; // eye R's mask tiles, until the accumulation images are made
// [eye]: written only while the device context is built; the mask pass's r15 points into .mask.
std::array<EyeSet, 2> g_sets{};
std::atomic<bool> g_ready{false};
std::atomic<bool> g_failedClosed{false};
std::atomic<bool> g_firstLogged{false};
std::atomic<bool> g_dynamicHeld{false}; // changed under g_mutex

// A pass's plan, from its entry hook to its later hooks on the same thread.
struct PassPlan {
    bool own = false;   // the eye's own images
    bool fresh = false; // ... and its history starts over
    bool held = false;  // a stereo render whose history is held off
    int eye = 0;
    int write = 0;
    unsigned parity = 0; // the engine's: counter & 1
    int update = 0;
};
thread_local PassPlan t_mask;
thread_local PassPlan t_pass;

struct Counters {
    std::atomic<std::uint64_t> renders[2]{};
    // By stereo_seq::RefractionRestart.
    std::atomic<std::uint64_t> restarts[stereo_seq::kRefractionRestarts]{};
    std::atomic<std::uint64_t> masks{0};
    std::atomic<std::uint64_t> passes[2]{}; // with the mask, without
    std::atomic<std::uint64_t> held{0};     // passes whose history was held off
    std::atomic<std::uint64_t> kept{0};     // engine resets dropped (another render's count)
    std::atomic<std::uint64_t> others{0};   // another view slot: left to the engine
    std::atomic<std::uint64_t> inFlightDiffers{0};
    std::atomic<int> logged{0};
    std::atomic<int> updatesLogged{0};
    std::atomic<std::uint64_t> lastReport{0};
} g_counters;

template <typename T>
T readAt(const void* at) {
    T v{};
    std::memcpy(&v, at, sizeof(v));
    return v;
}

template <typename T>
void writeAt(std::uintptr_t at, T v) {
    std::memcpy(reinterpret_cast<void*>(at), &v, sizeof(v));
}

using refraction_sizes::extentOf;

stereo_seq::RefractionReadiness readiness() {
    stereo_seq::RefractionReadiness r;
    r.installed = g_installed.load(std::memory_order_acquire);
    r.gameTouch = mp_guard::allowsGameTouch();
    r.failedClosed = g_failedClosed.load(std::memory_order_acquire);
    r.imagesMade = g_ready.load(std::memory_order_acquire);
    r.dynamicResolution = cvarInt(g_dynamicCvar) != 0;
    return r;
}

void failClosed(const char* why) {
    if (!g_failedClosed.exchange(true, std::memory_order_acq_rel)) {
        EVR_LOG(
            "%s: %s; the refraction history is held off in stereo until the device context is built again",
            kTag, why);
    }
}

// ---- Eye R's images, made with the device context (the renderer's start-up thread) ----

void* makeImage(const char* name, const std::byte* description) {
    void* manager = *g_manager;
    return manager ? g_create(manager, name, description) : nullptr;
}

void onMaskImagesBuilt(const HookRegisters& r) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    auto* dc = reinterpret_cast<std::byte*>(r.rdi);
    const auto* description = reinterpret_cast<const std::byte*>(r.rbp) + kRefractionMaskDescription;
    std::lock_guard lock(g_mutex);
    g_ready.store(false, std::memory_order_release);
    trackRefractionImages(nullptr, {}); // the old set's images are not followed any more
    g_pendingDc = dc;
    for (int i = 0; i < 2; ++i) {
        g_pendingMask[i] = makeImage(kMaskNames[i], description); // never freed, as the engine's own
    }
}

void onAccImagesBuilt(const HookRegisters& r) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    auto* dc = reinterpret_cast<std::byte*>(r.rdi);
    const auto* description = reinterpret_cast<const std::byte*>(r.rbp) + kRefractionAccDescription;
    std::lock_guard lock(g_mutex);
    g_ready.store(false, std::memory_order_release);
    void* made[4] = {};
    for (int k = 0; k < 4; ++k) {
        made[k] = makeImage(kAccNames[k], description);
    }
    EyeSet engine;
    EyeSet eyeR;
    bool ok = g_pendingDc == dc;
    std::vector<void*> all;
    for (int slot = 0; slot < 2; ++slot) {
        engine.mask[slot] = readAt<void*>(dc + kDcMaskTiles + 8 * slot);
        eyeR.mask[slot] = g_pendingMask[slot];
        ok = ok && extentOf(eyeR.mask[slot]) == extentOf(engine.mask[slot]);
        all.push_back(engine.mask[slot]);
        all.push_back(eyeR.mask[slot]);
        for (int u = 0; u < kUpdates; ++u) {
            const auto k = static_cast<std::size_t>(stereo_seq::refractionAccIndex(slot, u));
            engine.acc[u][slot] = readAt<void*>(dc + kDcAccImages + 8 * k);
            eyeR.acc[u][slot] = made[k];
            const auto* target = readAt<const std::byte*>(dc + kDcAccTargets + 8 * k);
            // The pass reads the targets' colour images: they must be the images the constructor made.
            ok = ok && target && readAt<void*>(target + kTargetImage) == engine.acc[u][slot] &&
                 extentOf(made[k]) == extentOf(engine.acc[u][slot]);
            all.push_back(engine.acc[u][slot]);
            all.push_back(made[k]);
        }
    }
    for (std::size_t i = 0; i < all.size(); ++i) {
        ok = ok && all[i] != nullptr && extentOf(all[i]).width > 0 && extentOf(all[i]).height > 0;
        for (std::size_t j = i + 1; j < all.size(); ++j) {
            ok = ok && all[i] != all[j];
        }
    }
    const refraction_sizes::Extent maskSize = extentOf(engine.mask[0]);
    const refraction_sizes::Extent accSize = extentOf(engine.acc[0][0]);
    EVR_LOG(
        "%s: eye R's refraction images made with device context %p: mask tiles %p %p (the engine's %p %p, "
        "%dx%d), accumulation %p %p %p %p (the engine's %p %p %p %p, %dx%d)",
        kTag, static_cast<void*>(dc), eyeR.mask[0], eyeR.mask[1], engine.mask[0], engine.mask[1],
        maskSize.width, maskSize.height, made[0], made[1], made[2], made[3], engine.acc[0][0],
        engine.acc[1][0], engine.acc[0][1], engine.acc[1][1], accSize.width, accSize.height);
    g_pendingDc = nullptr;
    if (!ok) {
        EVR_LOG(
            "%s: eye R's images are missing, the engine's own or of another size, or the engine's targets "
            "hold other images; the refraction history is held off in stereo",
            kTag);
        g_dc = nullptr;
        trackRefractionImages(nullptr, {});
        return;
    }
    g_sets[0] = engine;
    g_sets[1] = eyeR;
    g_dc = dc;
    trackRefractionImages(dc, refraction_sizes::pairsOf(engine.mask, engine.acc, eyeR.mask, eyeR.acc));
    g_history.reset();
    g_failedClosed.store(false, std::memory_order_release); // a new device context: its checks start over
    g_ready.store(true, std::memory_order_release);
}

// Eye R's images followed the engine's resize (refraction_resize.hpp, the renderer's thread).
void restartAfterResize() {
    std::lock_guard lock(g_mutex);
    g_history.invalidate(stereo_seq::RefractionRestart::Resized);
}

// ---- Each render's passes (job threads) ----

void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counters.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_counters.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    const auto take = [](std::atomic<std::uint64_t>& c) {
        return static_cast<unsigned long long>(c.exchange(0));
    };
    using R = stereo_seq::RefractionRestart;
    const auto restarts = [&](R why) {
        return take(g_counters.restarts[static_cast<int>(why)]);
    };
    EVR_LOG(
        "%s: refraction history %s: %llu eye L / mono and %llu eye R render(s); %llu mask pass(es), %llu "
        "refraction update(s) with the mask and %llu without; restarts %llu first, %llu after a gap, %llu "
        "untagged after eye L, %llu eye L after it, %llu after dynamic resolution, %llu after a resize; "
        "update restarts %llu count changed, %llu not in the eye's last render; %llu engine reset(s) dropped "
        "(another render's count); %llu update(s) held off; %llu other view(s) left to the engine; %llu "
        "render(s) whose tag in flight names another eye; r_transparencyEnableRefractionMask %d, "
        "r_refractionTAA %d, rs_enable %d",
        kTag, stereo_seq::refractionNotReady(readiness()) ? "not per eye" : "per eye",
        take(g_counters.renders[0]), take(g_counters.renders[1]), take(g_counters.masks),
        take(g_counters.passes[0]), take(g_counters.passes[1]), restarts(R::First), restarts(R::Gap),
        restarts(R::Untagged), restarts(R::AfterUntagged), restarts(R::Resumed), restarts(R::Resized),
        restarts(R::CountChanged), restarts(R::NotInLast), take(g_counters.kept), take(g_counters.held),
        take(g_counters.others), take(g_counters.inFlightDiffers), cvarInt(g_maskCvar), cvarInt(g_taaCvar),
        cvarInt(g_dynamicCvar));
}

void logFirst(stereo_seq::Eye eye, const stereo_seq::RefractionReadiness& r) {
    if (eye == stereo_seq::Eye::Mono || g_firstLogged.exchange(true)) {
        return;
    }
    if (const char* why = stereo_seq::refractionNotReady(r)) {
        EVR_LOG("%s: the refraction history is not per eye at the first stereo render: %s%s", kTag, why,
                stereo_seq::refractionHeldOff(r, eye) ? "; held off in stereo" : "");
    } else {
        EVR_LOG("%s: the refraction history is per eye at the first stereo render "
                "(r_transparencyEnableRefractionMask %d, r_refractionTAA %d)",
                kTag, cvarInt(g_maskCvar), cvarInt(g_taaCvar));
    }
}

// rs_enable non-zero holds the history off in stereo (readiness); back at 0 both eyes start over.
void followDynamicResolution() {
    const int dynamic = cvarInt(g_dynamicCvar);
    if ((dynamic != 0) == g_dynamicHeld.load(std::memory_order_acquire)) {
        return;
    }
    std::lock_guard lock(g_mutex);
    if ((dynamic != 0) == g_dynamicHeld.load(std::memory_order_relaxed)) {
        return;
    }
    g_dynamicHeld.store(dynamic != 0, std::memory_order_release);
    if (dynamic != 0) {
        EVR_LOG("%s: rs_enable %d: dynamic resolution scales the engine's refraction images by the counter's "
                "parity; the refraction history is held off in stereo until rs_enable is 0 again",
                kTag, dynamic);
    } else {
        // The engine wrote its own images meanwhile.
        g_history.invalidate(stereo_seq::RefractionRestart::Resumed);
        EVR_LOG("%s: rs_enable 0 again: the refraction history per eye resumes, both eyes start over", kTag);
    }
}

// The plan of the mask pass (kMaskPass) or of refraction update `update` of the render whose transparency
// context is `context`; nothing (the engine's) for another view slot, without the hooks or the guard, for
// mono renders while the history is not per eye, and for an update past the engine's images.
PassPlan planFor(const std::byte* context, int update) {
    PassPlan out;
    out.update = update;
    if (!g_installed.load(std::memory_order_acquire) || !context || !mp_guard::allowsGameTouch()) {
        return out;
    }
    report();
    const auto* renderView = readAt<const std::byte*>(context + kContextRenderView);
    const std::int32_t view =
        renderView ? readAt<std::int32_t>(renderView + render_view_object::kViewIndex) : -1;
    if (view != 0) {
        ++g_counters.others; // another view slot reads the engine's pairs
        return out;
    }
    const auto counter = readAt<std::uint32_t>(context + kContextCounter);
    // The render's own tag, by the counter the render-view job read for it (the backend counter, RVA
    // 0x1CBB2D0), the one the engine picks the pairs' parity with. The tag in flight reads the counter again
    // now; the count of renders where it names another eye is the self-check.
    const std::optional<stereo_seq::RenderTag> tag = seqTagForBackendFrame(counter + 1u);
    const stereo_seq::Eye eye = tag ? tag->eye : stereo_seq::Eye::Mono;
    const std::optional<stereo_seq::RenderTag> inFlight = seqTagInFlight();
    out.parity = counter & 1u;
    followDynamicResolution();
    stereo_seq::RefractionReadiness r = readiness();
    // rs_enable read again: a flip to 0 since the follow above waits for the next render's follow (and its
    // start-over), so the history stays held off until then.
    r.dynamicResolution = r.dynamicResolution || g_dynamicHeld.load(std::memory_order_acquire);
    logFirst(eye, r);
    if (stereo_seq::refractionHeldOff(r, eye)) {
        out.held = true;
        return out;
    }
    if (stereo_seq::refractionNotReady(r) || (update != kMaskPass && (update < 0 || update >= kUpdates))) {
        return out; // the engine's arrays hold two updates
    }
    // The render's update count (set before the transparency job runs), which the engine compares with the
    // last render's for the reset flag: per eye against the eye's own last render.
    const std::int32_t updates =
        update == kMaskPass ? 0 : readAt<std::int32_t>(context + kRefractionUpdateCount);
    std::lock_guard lock(g_mutex);
    if (*g_dcGlobal != g_dc) {
        failClosed(
            "a refraction pass found another device context than the one eye R's images were made with");
        out.held = eye != stereo_seq::Eye::Mono;
        return out;
    }
    if (update != kMaskPass && update >= updates) {
        failClosed("a refraction update is not below the render's update count");
        out.held = eye != stereo_seq::Eye::Mono;
        return out;
    }
    using refraction_sizes::SizeCheck;
    if (const SizeCheck size = eye == stereo_seq::Eye::Right  ? checkRefractionImageSizes(counter)
                               : refractionImagesMismatched() ? SizeCheck::HoldOff
                                                              : SizeCheck::Same;
        size != SizeCheck::Same) {
        if (size == SizeCheck::HoldOffFirst) {
            g_history.invalidate(stereo_seq::RefractionRestart::Resized); // no eye reads a held render's slot
        } else if (size == SizeCheck::FailClosed) {
            failClosed("eye R's refraction images stayed another size than the engine's");
        }
        out.held = true; // blends with no history while eye R's images are of another size
        return out;
    }
    const stereo_seq::RefractionPlan plan =
        update == kMaskPass ? g_history.plan(eye, counter) : g_history.plan(eye, counter, update, updates);
    if (!plan.repeated) {
        if ((inFlight ? inFlight->eye : stereo_seq::Eye::Mono) != eye) {
            ++g_counters.inFlightDiffers;
        }
        ++g_counters.renders[plan.eye];
        if (plan.restart != stereo_seq::RefractionRestart::None) {
            ++g_counters.restarts[static_cast<int>(plan.restart)];
        }
        if (g_counters.logged.fetch_add(1) < 4) {
            const EyeSet& s = g_sets[plan.eye];
            EVR_LOG(
                "%s: render %u for eye %s: writes mask %p, updates %p %p; reads mask %p, updates %p %p%s%s",
                kTag, counter, stereo_seq::eyeName(eye), s.mask[plan.write], s.acc[0][plan.write],
                s.acc[1][plan.write], s.mask[plan.write ^ 1], s.acc[0][plan.write ^ 1],
                s.acc[1][plan.write ^ 1],
                plan.restart != stereo_seq::RefractionRestart::None ? "; history reset: " : "",
                plan.restart != stereo_seq::RefractionRestart::None
                    ? stereo_seq::refractionRestartName(plan.restart)
                    : "");
        }
    }
    if (!plan.updateRepeated && plan.updateRestart != stereo_seq::RefractionRestart::None) {
        ++g_counters.restarts[static_cast<int>(plan.updateRestart)];
        if (g_counters.updatesLogged.fetch_add(1) < 8) {
            EVR_LOG("%s: render %u for eye %s: update %d of %d starts over: %s", kTag, counter,
                    stereo_seq::eyeName(eye), update, updates,
                    stereo_seq::refractionRestartName(plan.updateRestart));
        }
    }
    out.own = true;
    out.fresh = plan.fresh();
    out.eye = plan.eye;
    out.write = plan.write;
    return out;
}

// The mask pass's entry: rdx the transparency context.
void onMaskEntry(const HookRegisters& r) {
    t_mask = planFor(reinterpret_cast<const std::byte*>(r.rdx), kMaskPass);
}

// Its output: r15 = the parity * 8 + 0x488, rcx the device context. r15 then points at the eye's own image.
void onMaskSlots(HookRegisters& r) {
    if (!t_mask.own) {
        return;
    }
    if (r.rcx != reinterpret_cast<std::uintptr_t>(g_dc) || r.r15 != kDcMaskTiles + 8u * t_mask.parity) {
        t_mask.own = false;
        failClosed("the mask pass's output slot is not the engine's");
        return;
    }
    r.r15 = reinterpret_cast<std::uintptr_t>(&g_sets[t_mask.eye].mask[t_mask.write]) - r.rcx;
    ++g_counters.masks;
}

// Its last render's tiles: r8 = the engine's [parity ^ 1].
void onMaskLast(HookRegisters& r) {
    if (!t_mask.own) {
        return;
    }
    t_mask.own = false;
    if (r.r8 != reinterpret_cast<std::uintptr_t>(g_sets[0].mask[t_mask.parity ^ 1u])) {
        failClosed("the mask pass's last tiles are not the engine's");
        return;
    }
    r.r8 = reinterpret_cast<std::uintptr_t>(g_sets[t_mask.eye].mask[t_mask.write ^ 1]);
}

// The refraction pass's entry: rdx the transparency context, r8d the update, r9b the reset flag (read only
// for the blend with the mask: 1.0 when set, no history).
void onPassEntry(HookRegisters& r) {
    t_pass =
        planFor(reinterpret_cast<const std::byte*>(r.rdx), static_cast<std::int32_t>(r.r8 & 0xFFFFFFFFu));
    if (t_pass.held || (t_pass.own && t_pass.fresh)) {
        r.r9 = (r.r9 & ~std::uintptr_t{0xFF}) | 1u;
    } else if (t_pass.own && (r.r9 & 0xFFu) != 0) {
        // The eye's update count is its last render's, which ran this update: the engine's flag came from
        // another render's count (the other eye's, or another view's), and the eye's own history is good.
        r.r9 &= ~std::uintptr_t{0xFF};
        ++g_counters.kept;
    }
}

// The pass's argument block with the mask: the four images become the eye's own.
void onPassMask(HookRegisters& r) {
    if (t_pass.held) {
        ++g_counters.held;
    }
    if (!t_pass.own) {
        return;
    }
    t_pass.own = false;
    const EyeSet& engine = g_sets[0];
    const EyeSet& own = g_sets[t_pass.eye];
    const unsigned p = t_pass.parity;
    const int u = t_pass.update;
    if (readAt<void*>(reinterpret_cast<const void*>(r.rsp + kArgWrite)) != engine.acc[u][p] ||
        readAt<void*>(reinterpret_cast<const void*>(r.rsp + kArgMaskNow)) != engine.mask[p]) {
        failClosed("the refraction pass's images are not the engine's");
        r.rbx = (r.rbx & ~std::uintptr_t{0xFF}) | 1u; // this update blends with no history
        return;
    }
    const int w = t_pass.write;
    writeAt(r.rsp + kArgMaskNow, own.mask[w]);
    writeAt(r.rsp + kArgMaskLast, own.mask[w ^ 1]);
    writeAt(r.rsp + kArgWrite, own.acc[u][w]);
    writeAt(r.rsp + kArgRead, own.acc[u][w ^ 1]);
    ++g_counters.passes[0];
}

// Without the mask: the two accumulation images, and the blend 1.0 for a restart.
void onPassPlain(HookRegisters& r) {
    if (t_pass.held) {
        writeAt(r.rsp + kArgBlend, 1.0f);
        ++g_counters.held;
    }
    if (!t_pass.own) {
        return;
    }
    t_pass.own = false;
    const unsigned p = t_pass.parity;
    const int u = t_pass.update;
    if (readAt<void*>(reinterpret_cast<const void*>(r.rsp + kArgWrite)) != g_sets[0].acc[u][p]) {
        failClosed("the refraction pass's images are not the engine's");
        writeAt(r.rsp + kArgBlend, 1.0f);
        return;
    }
    const EyeSet& own = g_sets[t_pass.eye];
    const int w = t_pass.write;
    writeAt(r.rsp + kArgWrite, own.acc[u][w]);
    writeAt(r.rsp + kArgRead, own.acc[u][w ^ 1]);
    if (t_pass.fresh) {
        writeAt(r.rsp + kArgBlend, 1.0f);
    }
    ++g_counters.passes[1];
}

} // namespace

bool installRefractionHooksEarly() {
    std::call_once(g_once, [] {
        if (!mp_guard::allowsGameTouch()) {
            EVR_LOG("%s: the multiplayer guard is not armed; the refraction history is the engine's", kTag);
            return;
        }
        GameImage image;
        RefractionSites s;
        if (!locateGameImage(image, kTag) || !locateRefractionSites(image, kTag, s)) {
            EVR_LOG("%s: not installed; the refraction history is the engine's, shared between the eyes",
                    kTag);
            return;
        }
        g_manager = reinterpret_cast<void* const*>(s.imageManager);
        g_create = reinterpret_cast<CreateImageFn>(const_cast<std::byte*>(s.createImage));
        g_dcGlobal = reinterpret_cast<std::byte* const*>(s.deviceContext);
        const std::vector<std::byte*> cvars =
            findCvarObjects(image, {"r_refractionTAA", "r_transparencyEnableRefractionMask", "rs_enable"});
        if (!cvars[2]) {
            EVR_LOG("%s: rs_enable not found; not installed, the refraction history is the engine's", kTag);
            return;
        }
        g_taaCvar = cvars[0];
        g_maskCvar = cvars[1];
        g_dynamicCvar = cvars[2];
        std::string error;
        // The pass hooks act only once g_installed is set; the image hooks last, so a failure leaves no hook
        // that acts.
        if (!installMidHook(s.maskEntry, &onMaskEntry, error) ||
            !installMidHookEdit(s.maskSlots, &onMaskSlots, error) ||
            !installMidHookEdit(s.maskLast, &onMaskLast, error) ||
            !installMidHookEdit(s.passEntry, &onPassEntry, error) ||
            !installMidHookEdit(s.passMask, &onPassMask, error) ||
            !installMidHookEdit(s.passPlain, &onPassPlain, error) ||
            !installRefractionResizeHook(s, &restartAfterResize, error) ||
            !installMidHook(s.maskImagesBuilt, &onMaskImagesBuilt, error) ||
            !installMidHook(s.accImagesBuilt, &onAccImagesBuilt, error)) {
            EVR_LOG("%s: hook failed: %s; the refraction history is the engine's", kTag, error.c_str());
            return;
        }
        g_installed.store(true, std::memory_order_release);
        EVR_LOG(
            "%s: refraction images (RVA 0x%X, 0x%X), their resize (RVA 0x%X), mask pass (RVA 0x%X) and "
            "refraction pass (RVA 0x%X) hooked: each eye keeps its own refraction history (device context "
            "global RVA 0x%X, image manager RVA 0x%X, create RVA 0x%X, resize RVA 0x%X)",
            kTag, image.rva(s.maskImagesBuilt), image.rva(s.accImagesBuilt), image.rva(s.imagesResized),
            image.rva(s.maskEntry), image.rva(s.passEntry), image.rva(s.deviceContext),
            image.rva(s.imageManager), image.rva(s.createImage), image.rva(s.resizeImage));
    });
    return g_installed.load(std::memory_order_acquire);
}

const char* refractionHooksText() {
    return g_installed.load(std::memory_order_acquire) ? "per eye" : "NOT hooked";
}

bool refractionPerEyeReady() {
    return stereo_seq::refractionNotReady(readiness()) == nullptr;
}

} // namespace evr::vkcore
