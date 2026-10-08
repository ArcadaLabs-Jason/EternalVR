#include "vkcore/ssdo_hooks.hpp"

#include "stereo_seq/ssdo_history.hpp"
#include "stereo_seq/stereo_taa.hpp"
#include "vkcore/game_code.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/stereo_hooks.hpp"
#include "vkcore/taa_locate.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-ssdo";

// The device context constructor's unfiltered SSDO image (RVA 0x1C1EA3E): `lea rdx, ["ambientOcclusion-
// Unfiltered"]`, the name formatted, `mov rcx, [rip + imageManager]` (+0x16), `lea r8, [rbp + 0x10]` (the
// image description, the same for the three SSDO images), `call create` (+0x25), then `mov rcx, [rdi +
// 0x298]`.
constexpr const char* kImagesSignature =
    "48 8D 15 ?? ?? ?? ?? 45 8B CD 48 8D 8D D0 00 00 00 E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 4C 8D 45 10 "
    "48 8B 50 08 E8 ?? ?? ?? ?? 48 8B 8F 98 02 00 00";
constexpr std::size_t kImagesName = 0x0;
constexpr std::size_t kImagesManager = 0x16;
constexpr std::size_t kImagesCreate = 0x25;
constexpr std::size_t kDescriptionInFrame = 0x10; // rbp + 0x10

// Its render target, the last of the three (RVA 0x1C1EBAF): a 0x230-byte object allocated, `call construct`
// (+0x17), stored at [rdi + 0x560], then `call attach(target, image, 0, 0, 0)` (+0x69); the hook goes on the
// next instruction (+0x6E), where rdi is the device context and rbp + 0x10 still holds the description.
constexpr const char* kTargetsSignature =
    "BA 45 00 00 00 B9 30 02 00 00 E8 ?? ?? ?? ?? 48 85 C0 74 0D 48 8B C8 E8 ?? ?? ?? ?? 48 8B F0 EB 03 49 "
    "8B F4 48 8B 9F 60 05 00 00 48 3B DE 74 24 48 85 DB 74 15 48 8B CB E8 ?? ?? ?? ?? BA 30 02 00 00 48 8B "
    "CB E8 ?? ?? ?? ?? 48 89 B7 60 05 00 00 48 8B DE 48 8B 97 98 02 00 00 45 33 C9 45 33 C0 44 89 64 24 20 "
    "48 8B CB E8 ?? ?? ?? ??";
constexpr std::size_t kTargetsConstruct = 0x17;
constexpr std::size_t kTargetsAttach = 0x69;
constexpr std::size_t kTargetsHook = 0x6E;
constexpr std::size_t kTargetBytes = 0x230;

// SSDO's parameter setup (RVA 0x1C71630, from the render-view job at RVA 0x1C56B19): rcx is the job's
// context.
constexpr const char* kSetupSignature =
    "40 53 56 57 41 54 48 81 EC 88 00 00 00 48 8B 05 ?? ?? ?? ?? 48 8B F2 0F 29 74 24 60 48 8B F9 44 0F 29 "
    "44 24 40 44 0F 29 4C 24 30 83 78 08 00";
// In it, the filter's state from the SSDO pass context (RVA 0x1C71813: `mov rax, [rdi + 0x90]` .. `mov rbx,
// [rax + 0x10]`) ...
constexpr const char* kStateSignature =
    "48 8B 87 90 00 00 00 F3 44 0F 10 0D ?? ?? ?? ?? 4C 8B 7C 24 70 4C 8B 74 24 78 48 8B 58 10";
// ... and the pick of the target (RVA 0x1C718BD): a new state takes the job's counter (+0xB0) as its last
// frame (state + 4) and the size of array[0] (array = [context + 0x58]), r_SSDOTemporalAA (+0x42) as its
// filter flag; then index = counter & 1 (state + 8) and output = array[index] (state + 0x18). The pass (RVA
// 0x1C70CE0) reads the unfiltered target and the history from the pass context's copy (+0x20): [2] and
// [index ^ 1]. The engine resets the filter when the counter is more than one past the last frame.
constexpr const char* kSelectSignature =
    "80 3B 00 75 4E C6 03 01 8B 87 B0 00 00 00 89 43 04 C7 43 08 00 00 00 00 C6 43 0C 01 48 8B 47 58 48 8B "
    "08 8B 01 8B 51 04 89 84 24 B0 00 00 00 89 94 24 B4 00 00 00 48 8B 84 24 B0 00 00 00 48 89 43 10 48 8B "
    "05 ?? ?? ?? ?? 83 78 08 00 0F 95 C0 88 43 20 8B 8F B0 00 00 00 83 E1 01 89 4B 08 48 8B 47 58 4C 8B 04 "
    "C8 4C 89 43 18";
constexpr std::size_t kSelectCvar = 0x42;
constexpr std::size_t kJobRenderView = 0x20;
constexpr std::size_t kJobArray = 0x58;
constexpr std::size_t kJobPassContext = 0x90;
constexpr std::size_t kJobCounter = 0xB0;
constexpr std::size_t kPassState = 0x10;
constexpr std::size_t kPassArray = 0x20;
constexpr std::size_t kStateLastFrame = 0x4;

// The render-size change (RVA 0x1C21600) resizes the SSDO targets at half the size (RVA 0x1C21946): `mov rcx,
// [rdi + 0x550]`, the halved size and `call resize(target, width, height, 1)` (RVA 0x1C743C0) for each of
// + 0x550, + 0x558 and + 0x560 (the calls at +0x21, +0x38 and +0x4F), then `mov rcx, [rdi + 0x568]` (+0x54),
// where the hook goes; the skip for a missing + 0x550 jumps there as well. rdi is the device context.
constexpr const char* kResizeSignature =
    "48 8B 8F 50 05 00 00 44 8B F5 8B DE 41 D1 FE D1 FB 48 85 C9 74 3E 41 B9 01 00 00 00 45 8B C6 8B D3 E8 "
    "?? ?? ?? ?? 48 8B 8F 58 05 00 00 41 B9 01 00 00 00 45 8B C6 8B D3 E8 ?? ?? ?? ?? 48 8B 8F 60 05 00 00 "
    "41 B9 01 00 00 00 45 8B C6 8B D3 E8 ?? ?? ?? ?? 48 8B 8F 68 05 00 00";
constexpr std::size_t kResizeCalls[3] = {0x21, 0x38, 0x4F};
constexpr std::size_t kResizeHook = 0x54;

// The device context's SSDO targets: accumulation 0 and 1, then the unfiltered one.
constexpr std::size_t kDeviceContextTargets = 0x550;
// A render target's size (width +0x0, height +0x4) and its colour image (+0x10); an image's size (+0x64,
// +0x68).
constexpr std::size_t kTargetImage = 0x10;
constexpr std::size_t kImageSize = 0x64;

using CreateImageFn = void* (*)(void* manager, const char* name, const void* description);
using ConstructTargetFn = void* (*)(void* target);
using AttachTargetFn = void (*)(void* target, void* color, void* depth, void* stencil, int flags);
using ResizeTargetFn = void (*)(void* target, int width, int height, int flag);

// Eye R's images, named after the engine's.
constexpr const char* kEyeRNames[2] = {"ambientOcclusionAcc0EyeR", "ambientOcclusionAcc1EyeR"};

std::once_flag g_once;
bool g_installed = false;
void* const* g_manager = nullptr;
CreateImageFn g_create = nullptr;
ConstructTargetFn g_construct = nullptr;
AttachTargetFn g_attach = nullptr;
ResizeTargetFn g_resize = nullptr;
const std::byte* g_filterCvar = nullptr; // r_SSDOTemporalAA, read back for the log
// rs_enable. Dynamic resolution sizes the device context's target of the counter's parity at each frame's
// start (RVA 0x1CDEED7), which with the history per eye is not always the one written, and never eye R's: the
// history is held off while it reads non-zero (the per-eye TAA set holds it at 0; Anti-aliasing Off and a
// failed-closed per-eye TAA do not).
const std::byte* g_dynamicCvar = nullptr;
bool g_requested = false;

std::mutex g_mutex;
stereo_seq::SsdoHistory g_history;
std::byte* g_deviceContext = nullptr;    // the device context g_history's targets belong to
std::atomic<bool> g_ready{false};        // eye R's targets made, and not failed closed since
std::atomic<bool> g_failedClosed{false}; // for the rest of the session
bool g_dynamicHeld = false; // dynamic resolution holds the history off (renders go to the engine)
std::atomic<bool> g_tickLogged{false};
void* g_engineTargets[3] = {}; // + 0x550 .. + 0x560 as the constructor left them
void* g_eyeRTargets[2] = {};
// The arrays handed to the engine, one per plan key: a key always holds the same three targets.
std::array<std::array<void*, 3>, stereo_seq::kSsdoArrays> g_arrays{};
std::atomic<std::uint64_t> g_resizes{0};

struct Counters {
    std::atomic<std::uint64_t> renders[2]{};
    std::atomic<std::uint64_t> restarts[stereo_seq::kSsdoRestarts]{}; // by stereo_seq::SsdoRestart
    std::atomic<std::uint64_t> skipped{0};                            // another view slot: left to the engine
    std::atomic<std::uint64_t> dynamic{0}; // held off by dynamic resolution: left to the engine
    std::atomic<std::uint64_t> inFlightDiffers{0};
    std::atomic<int> logged{0};
    std::atomic<std::uint64_t> lastReport{0};
} g_counters;

bool requested() {
    std::wstring value;
    std::string narrow;
    if (readEnv(L"ETERNALVR_STEREO_SSDO_TAA", value)) {
        for (const wchar_t c : value) {
            narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
    }
    return stereo_seq::switchValue(narrow, true);
}

template <typename T>
T readAt(const std::byte* at) {
    T v{};
    std::memcpy(&v, at, sizeof(v));
    return v;
}

struct Size {
    std::int32_t width = 0;
    std::int32_t height = 0;
    friend constexpr bool operator==(Size, Size) = default;
};

Size targetSize(const void* target) {
    return target ? readAt<Size>(static_cast<const std::byte*>(target)) : Size{};
}

Size imageSize(const void* target) {
    const auto* image =
        target ? readAt<const std::byte*>(static_cast<const std::byte*>(target) + kTargetImage) : nullptr;
    return image ? readAt<Size>(image + kImageSize) : Size{};
}

// Under g_mutex: eye R's targets are no longer used, and the cvar holds take the filter off again.
void failClosed(const char* why) {
    g_failedClosed.store(true, std::memory_order_release);
    if (g_ready.exchange(false, std::memory_order_acq_rel)) {
        EVR_LOG("%s: %s; the SSDO filter is held off in stereo for the rest of the session", kTag, why);
    }
}

// ---- Eye R's targets, made with the device context (the renderer's start-up thread) ----

void onTargetsBuilt(const HookRegisters& r) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    auto* deviceContext = reinterpret_cast<std::byte*>(r.rdi);
    const auto* description = reinterpret_cast<const std::byte*>(r.rbp) + kDescriptionInFrame;
    void* manager = *g_manager;
    std::lock_guard lock(g_mutex);
    g_ready.store(false, std::memory_order_release);
    g_deviceContext = nullptr;
    if (!manager) {
        EVR_LOG("%s: no image manager; the SSDO filter stays off in stereo", kTag);
        return;
    }
    void* engine[3] = {};
    std::memcpy(engine, deviceContext + kDeviceContextTargets, sizeof(engine));
    void* images[2] = {};
    void* targets[2] = {};
    for (int i = 0; i < 2; ++i) {
        images[i] = g_create(manager, kEyeRNames[i], description);
        if (images[i]) {
            // Never freed: the engine frees only the targets in its own device context.
            targets[i] = g_construct(new std::byte[kTargetBytes]{});
            g_attach(targets[i], images[i], nullptr, nullptr, 0);
        }
    }
    g_history.reset({engine[0], engine[1]}, {targets[0], targets[1]}, engine[2]);
    const Size size = targetSize(engine[0]);
    const bool sized = size.width > 0 && size.height > 0 && targetSize(targets[0]) == size &&
                       targetSize(targets[1]) == size && imageSize(targets[0]) == size &&
                       imageSize(targets[1]) == size;
    EVR_LOG("%s: eye R's SSDO targets made with device context %p: %p %p (images %p %p), %dx%d / %dx%d (the "
            "engine's %p %p, unfiltered %p, %dx%d)",
            kTag, static_cast<void*>(deviceContext), targets[0], targets[1], images[0], images[1],
            targetSize(targets[0]).width, targetSize(targets[0]).height, targetSize(targets[1]).width,
            targetSize(targets[1]).height, engine[0], engine[1], engine[2], size.width, size.height);
    if (!g_history.ready() || !sized) {
        EVR_LOG(
            "%s: eye R's targets are missing, the engine's own or of another size; the SSDO filter stays off "
            "in stereo",
            kTag);
        return;
    }
    std::memcpy(g_engineTargets, engine, sizeof(engine));
    std::memcpy(g_eyeRTargets, targets, sizeof(targets));
    g_deviceContext = deviceContext;
    g_ready.store(!g_failedClosed.load(std::memory_order_acquire), std::memory_order_release);
}

// ---- The render-size change (the renderer's thread, between frames) ----

void onTargetsResized(const HookRegisters& r) {
    if (!g_ready.load(std::memory_order_acquire) || !mp_guard::allowsGameTouch()) {
        return;
    }
    auto* deviceContext = reinterpret_cast<std::byte*>(r.rdi);
    std::lock_guard lock(g_mutex);
    if (!deviceContext || deviceContext != g_deviceContext) {
        return;
    }
    const Size size = targetSize(readAt<void*>(deviceContext + kDeviceContextTargets));
    if (size.width <= 0 || size.height <= 0) {
        return; // no target at + 0x550: the engine resized none
    }
    int followed = 0;
    for (void* target : g_eyeRTargets) {
        if (targetSize(target) != size || imageSize(target) != size) {
            g_resize(target, size.width, size.height, 1);
            ++followed;
        }
    }
    const std::uint64_t n = ++g_resizes;
    if (followed > 0 || n <= 4) {
        EVR_LOG("%s: SSDO targets resized to %dx%d: %d of eye R's followed (%dx%d / %dx%d)", kTag, size.width,
                size.height, followed, targetSize(g_eyeRTargets[0]).width,
                targetSize(g_eyeRTargets[0]).height, targetSize(g_eyeRTargets[1]).width,
                targetSize(g_eyeRTargets[1]).height);
    }
    for (void* target : g_eyeRTargets) {
        if (targetSize(target) != size || imageSize(target) != size) {
            failClosed("eye R's SSDO targets did not take the engine's size");
            return;
        }
    }
    if (followed > 0) {
        g_history.invalidate();
    }
}

// ---- Each render: the eye's targets (render-view job threads; the renders of a tick never overlap) ----

void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counters.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_counters.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    const auto take = [](std::atomic<std::uint64_t>& c) {
        return static_cast<unsigned long long>(c.exchange(0));
    };
    using R = stereo_seq::SsdoRestart;
    const auto restarts = [&](R why) {
        return take(g_counters.restarts[static_cast<int>(why)]);
    };
    EVR_LOG(
        "%s: SSDO history per eye: %llu eye L / mono and %llu eye R render(s); restarts %llu first, %llu "
        "after a resize, %llu after a gap, %llu eye R missed a tick, %llu untagged after eye L, %llu after "
        "dynamic resolution; %llu other view(s) and %llu dynamic resolution render(s) left to the engine; "
        "%llu "
        "render(s) whose tag in flight names another eye; targets %dx%d, eye R's %dx%d / %dx%d; "
        "r_SSDOTemporalAA %d",
        kTag, take(g_counters.renders[0]), take(g_counters.renders[1]), restarts(R::First),
        restarts(R::Resize), restarts(R::Gap), restarts(R::EyeRMissed), restarts(R::Untagged),
        restarts(R::Resumed), take(g_counters.skipped), take(g_counters.dynamic),
        take(g_counters.inFlightDiffers), targetSize(g_engineTargets[0]).width,
        targetSize(g_engineTargets[0]).height, targetSize(g_eyeRTargets[0]).width,
        targetSize(g_eyeRTargets[0]).height, targetSize(g_eyeRTargets[1]).width,
        targetSize(g_eyeRTargets[1]).height, cvarInt(g_filterCvar));
}

void onSetup(const HookRegisters& r) {
    if (!g_ready.load(std::memory_order_acquire) || !mp_guard::allowsGameTouch()) {
        return;
    }
    auto* job = reinterpret_cast<std::byte*>(r.rcx);
    if (!job) {
        return;
    }
    const auto* renderView = readAt<const std::byte*>(job + kJobRenderView);
    const auto* array = readAt<const std::byte*>(job + kJobArray);
    auto* pass = readAt<std::byte*>(job + kJobPassContext);
    const auto counter = readAt<std::uint32_t>(job + kJobCounter);
    const std::int32_t viewIndex =
        renderView ? readAt<std::int32_t>(renderView + render_view_object::kViewIndex) : -1;
    std::byte* state = pass ? readAt<std::byte*>(pass + kPassState) : nullptr;
    std::lock_guard lock(g_mutex);
    if (!g_ready.load(std::memory_order_acquire)) {
        return;
    }
    report();
    if (viewIndex != 0) {
        ++g_counters.skipped; // another view slot has a filter state of its own
        return;
    }
    const int dynamic = cvarInt(g_dynamicCvar);
    if ((dynamic != 0) != g_dynamicHeld) {
        g_dynamicHeld = dynamic != 0;
        if (g_dynamicHeld) {
            EVR_LOG("%s: rs_enable %d: dynamic resolution sizes the engine's SSDO targets by the counter's "
                    "parity; "
                    "the SSDO filter is held off in stereo until rs_enable is 0 again",
                    kTag, dynamic);
        } else {
            // The engine wrote its own targets meanwhile: both eyes start over.
            g_history.invalidate(stereo_seq::SsdoRestart::Resumed);
            EVR_LOG("%s: rs_enable 0 again: the SSDO history per eye resumes, both eyes start over", kTag);
        }
    }
    if (g_dynamicHeld) {
        ++g_counters.dynamic;
        return;
    }
    // The engine points both at its own array on every render; anything else is a context these hooks do not
    // know, and the engine's targets are the ones the constructor made.
    void* engine[3] = {};
    if (g_deviceContext) {
        std::memcpy(engine, g_deviceContext + kDeviceContextTargets, sizeof(engine));
    }
    if (!state || !g_deviceContext || array != g_deviceContext + kDeviceContextTargets ||
        readAt<const std::byte*>(pass + kPassArray) != array ||
        std::memcmp(engine, g_engineTargets, sizeof(engine)) != 0) {
        failClosed("the SSDO setup found another device context, target array or targets than at start-up");
        return;
    }
    // The render's own tag, found by the counter the render-view job read for it (the backend frame counter
    // stored at RVA 0x1C56A19), the one the engine picks the target's parity with. The tag in flight reads
    // the counter again now and would name the next render if the render thread's swap came in between; the
    // count of such renders is the self-check.
    const std::optional<stereo_seq::RenderTag> tag = seqTagForBackendFrame(counter + 1u);
    const stereo_seq::Eye eye = tag ? tag->eye : stereo_seq::Eye::Mono;
    const std::optional<stereo_seq::RenderTag> inFlight = seqTagInFlight();
    if ((inFlight ? inFlight->eye : stereo_seq::Eye::Mono) != eye) {
        ++g_counters.inFlightDiffers;
    }
    const stereo_seq::SsdoPlan plan = g_history.beforeRender(eye, counter);
    std::array<void*, 3>& held = g_arrays[static_cast<std::size_t>(plan.key)];
    held = plan.targets;
    void* const at = held.data();
    std::memcpy(job + kJobArray, &at, sizeof(at));
    std::memcpy(pass + kPassArray, &at, sizeof(at));
    if (plan.restart != stereo_seq::SsdoRestart::None) {
        // Two renders back: the engine finds no last frame and resets the filter (the blend ratio 1, no
        // history).
        const std::uint32_t stale = counter - 2u;
        std::memcpy(state + kStateLastFrame, &stale, sizeof(stale));
        ++g_counters.restarts[static_cast<int>(plan.restart)];
    }
    ++g_counters.renders[eye == stereo_seq::Eye::Right ? 1 : 0];
    if (g_counters.logged.fetch_add(1) < 4) {
        EVR_LOG("%s: render %u for eye %s: writes %p, reads %p%s%s", kTag, counter, stereo_seq::eyeName(eye),
                plan.targets[counter & 1u], plan.targets[(counter & 1u) ^ 1u],
                plan.restart != stereo_seq::SsdoRestart::None ? ", history reset: " : "",
                plan.restart != stereo_seq::SsdoRestart::None ? stereo_seq::ssdoRestartName(plan.restart)
                                                              : "");
    }
}

} // namespace

bool installSsdoHooksEarly() {
    std::call_once(g_once, [] {
        g_requested = requested();
        if (!g_requested) {
            EVR_LOG("%s: off (ETERNALVR_STEREO_SSDO_TAA=0); the SSDO filter stays off in stereo", kTag);
            return;
        }
        if (!mp_guard::allowsGameTouch()) {
            EVR_LOG("%s: the multiplayer guard is not armed; the SSDO filter stays off in stereo", kTag);
            return;
        }
        GameImage image;
        if (!locateGameImage(image, kTag)) {
            EVR_LOG("%s: the game's code was not found; the SSDO filter stays off in stereo", kTag);
            return;
        }
        const std::byte* images = findUnique(image, kTag, "SSDO images", kImagesSignature);
        const std::byte* targets = findUnique(image, kTag, "SSDO targets", kTargetsSignature);
        const std::byte* setup = findUnique(image, kTag, "SSDO setup", kSetupSignature);
        const std::byte* state = findUnique(image, kTag, "SSDO filter state", kStateSignature);
        const std::byte* select = findUnique(image, kTag, "SSDO target pick", kSelectSignature);
        const std::byte* resizeSite = findUnique(image, kTag, "SSDO target resize", kResizeSignature);
        if (!images || !targets || !setup || !state || !select || !resizeSite) {
            EVR_LOG("%s: not installed; the SSDO filter stays off in stereo", kTag);
            return;
        }
        const std::byte* name = ripTarget(image, images + kImagesName + 3, images + kImagesName + 7);
        const std::byte* manager = ripTarget(image, images + kImagesManager + 3, images + kImagesManager + 7);
        const std::byte* create = relativeTarget(images + kImagesCreate);
        const std::byte* construct = relativeTarget(targets + kTargetsConstruct);
        const std::byte* attach = relativeTarget(targets + kTargetsAttach);
        const std::byte* cvar = ripTarget(image, select + kSelectCvar + 3, select + kSelectCvar + 7);
        const std::byte* resize = relativeTarget(resizeSite + kResizeCalls[0]);
        const std::byte* dynamic = findCvarObjects(image, {"rs_enable"}).front();
        const std::byte* constructor = functionStart(image, images);
        bool resizeOk = resize && image.inText(resize) && functionStart(image, resize) == resize;
        for (const std::size_t call : kResizeCalls) {
            resizeOk = resizeOk && relativeTarget(resizeSite + call) == resize;
        }
        // The constructor is a leaf without an unwind entry, so only its place is checked.
        if (!name || stringAt(image, name) != "ambientOcclusionUnfiltered" || !manager ||
            !image.contains(manager, sizeof(void*)) || !create || !image.inText(create) ||
            functionStart(image, create) != create || !construct || !image.inText(construct) || !attach ||
            !image.inText(attach) || functionStart(image, attach) != attach || !constructor ||
            targets < images || functionStart(image, targets + kTargetsHook) != constructor ||
            functionStart(image, setup) != setup || functionStart(image, state) != setup ||
            functionStart(image, select) != setup || state > select || !cvar ||
            !image.contains(cvar, sizeof(void*)) || !resizeOk || !dynamic) {
            EVR_LOG(
                "%s: the image manager, the target functions, the setup's state, the resize or rs_enable did "
                "not check out; not installed, the SSDO filter stays off in stereo",
                kTag);
            return;
        }
        g_manager = reinterpret_cast<void* const*>(manager);
        g_create = reinterpret_cast<CreateImageFn>(const_cast<std::byte*>(create));
        g_construct = reinterpret_cast<ConstructTargetFn>(const_cast<std::byte*>(construct));
        g_attach = reinterpret_cast<AttachTargetFn>(const_cast<std::byte*>(attach));
        g_resize = reinterpret_cast<ResizeTargetFn>(const_cast<std::byte*>(resize));
        g_filterCvar = cvar;
        g_dynamicCvar = dynamic;
        std::string error;
        // The targets hook last: the other two do nothing until eye R's targets exist, so a failure leaves no
        // hook that acts.
        if (!installMidHook(const_cast<std::byte*>(resizeSite + kResizeHook), &onTargetsResized, error) ||
            !installMidHook(const_cast<std::byte*>(setup), &onSetup, error) ||
            !installMidHook(const_cast<std::byte*>(targets + kTargetsHook), &onTargetsBuilt, error)) {
            EVR_LOG("%s: hook failed: %s; the SSDO filter stays off in stereo", kTag, error.c_str());
            return;
        }
        g_installed = true;
        EVR_LOG(
            "%s: SSDO targets (RVA 0x%X), setup (RVA 0x%X) and resize (RVA 0x%X) hooked: each eye keeps its "
            "own SSDO history (image manager RVA 0x%X, create RVA 0x%X, target constructor RVA 0x%X, attach "
            "RVA 0x%X, resize RVA 0x%X, r_SSDOTemporalAA RVA 0x%X)",
            kTag, image.rva(targets + kTargetsHook), image.rva(setup), image.rva(resizeSite + kResizeHook),
            image.rva(manager), image.rva(create), image.rva(construct), image.rva(attach), image.rva(resize),
            image.rva(cvar));
    });
    return g_installed;
}

bool ssdoHooksInstalled() {
    return g_installed;
}

namespace {

stereo_seq::SsdoReadiness readiness() {
    stereo_seq::SsdoReadiness r;
    r.requested = g_requested;
    r.installed = g_installed;
    r.gameTouch = mp_guard::allowsGameTouch();
    r.failedClosed = g_failedClosed.load(std::memory_order_acquire);
    r.targetsMade = g_ready.load(std::memory_order_acquire);
    r.dynamicResolution = g_dynamicCvar && cvarInt(g_dynamicCvar) != 0;
    return r;
}

} // namespace

bool ssdoPerEyeReady() {
    return stereo_seq::ssdoNotReady(readiness()) == nullptr;
}

void ssdoOnStereoTick() {
    if (g_tickLogged.exchange(true)) {
        return;
    }
    if (const char* why = stereo_seq::ssdoNotReady(readiness())) {
        EVR_LOG(
            "%s: the SSDO history per eye is not ready at the first stereo tick: %s; the SSDO filter stays "
            "off in stereo",
            kTag, why);
    } else {
        EVR_LOG("%s: the SSDO history per eye is ready at the first stereo tick (r_SSDOTemporalAA %d)", kTag,
                cvarInt(g_filterCvar));
    }
}

} // namespace evr::vkcore
