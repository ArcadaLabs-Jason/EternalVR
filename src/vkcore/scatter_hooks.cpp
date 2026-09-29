#include "vkcore/scatter_hooks.hpp"

#include "stereo_seq/scatter_history.hpp"
#include "stereo_seq/stereo_taa.hpp"
#include "vkcore/game_code.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/seq_hooks.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-scatter";

// The device context constructor's last scattering image (RVA 0x1C1F00E): `lea rdx, ["lightScattering-
// Packed1Acc1"]`, the name formatted, `mov rcx, [rip + imageManager]` (+0x16), `lea r8, [rbp + 0x10]` (the
// image description), `call create` (+0x25), then the old image released and `mov [rdi + 0x2E8], rbx`; the
// hook goes on the next instruction (+0x4E), where rdi is the device context and rbp + 0x10 still holds the
// description.
constexpr const char* kAllocSignature =
    "48 8D 15 ?? ?? ?? ?? 45 8B CD 48 8D 8D D0 00 00 00 E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 4C 8D 45 10 "
    "48 8B 50 08 E8 ?? ?? ?? ?? 48 8B 8F E8 02 00 00 48 8B D8 48 3B C8 74 15 48 85 C9 74 09 4C 8B 01 41 "
    "8B D4 41 FF 10 48 89 9F E8 02 00 00";
constexpr std::size_t kAllocName = 0x0;
constexpr std::size_t kAllocManager = 0x16;
constexpr std::size_t kAllocCreate = 0x25;
constexpr std::size_t kAllocHook = 0x4E;
constexpr std::size_t kDescriptionInFrame = 0x10; // rbp + 0x10

// The scattering setup's entry (RVA 0x1C71F90): the arguments' context (rcx + 0x80) and `mov r11, [rip +
// deviceContext]` (+0x25). The context holds the render counter (+0x10: the backend frame counter, stored by
// the render-view job through RVA 0x1CBB2D0) and the filter's state (+0x8).
constexpr const char* kSetupSignature =
    "48 8B C4 48 89 58 10 48 89 70 18 57 41 54 41 55 41 56 41 57 48 81 EC A0 00 00 00 4C 8B 91 80 00 00 "
    "00 4C 8B EA 4C 8B 1D ?? ?? ?? ?? 48 8B F1";
constexpr std::size_t kSetupDeviceContext = 0x25;
constexpr std::size_t kArgsContext = 0x80;
constexpr std::size_t kContextCounter = 0x10;
constexpr std::size_t kContextState = 0x8;

// The render-size change (RVA 0x1CDD6D0) resizes the scattering volumes in place, the device context's four
// through `resize(image, width, height, depth, mips)` (RVA 0x1C4AE30). The signature starts at the last of
// them (RVA 0x1CDDD58): `mov rcx, [rip + deviceContext]`, the size from the stack, `mov rcx, [rcx + 0x2E8]`,
// `call resize` (+0x20); the hook goes on the next instruction (+0x25). The slots then hold whichever eye's
// images the last render left there, so the hook brings all eight to the size of the one just resized.
constexpr const char* kResizeSignature =
    "48 8B 0D ?? ?? ?? ?? 44 8B 4C 24 38 44 8B 44 24 34 8B 54 24 30 48 8B 89 E8 02 00 00 89 5C 24 20 "
    "E8 ?? ?? ?? ?? 44 8B 4C 24 38 44 8B 44 24 34 8B 54 24 30 48 8B 0D ?? ?? ?? ?? 89 5C 24 20 E8 ?? ?? "
    "?? ??";
constexpr std::size_t kResizeDeviceContext = 0x0;
constexpr std::size_t kResizeCall = 0x20;
constexpr std::size_t kResizeHook = 0x25;
constexpr std::size_t kLastPairImage = 0x2E8;
// An image's size: width +0x64, height +0x68, depth +0x6C, mip count +0x74.
constexpr std::size_t kImageSize = 0x64;
constexpr std::size_t kImageMips = 0x74;

// The device context's two pairs (the image objects), [parity] at + 0x2D0 + parity * 0x10.
constexpr std::size_t kDeviceContextPairs = 0x2D0;

using CreateImageFn = void* (*)(void* manager, const char* name, const void* description);
using ResizeImageFn = bool (*)(void* image, int width, int height, int depth, int mips);

// Eye R's images, named after the engine's.
constexpr const char* kEyeRNames[4] = {"lightScatteringPacked0Acc0EyeR", "lightScatteringPacked0Acc1EyeR",
                                       "lightScatteringPacked1Acc0EyeR", "lightScatteringPacked1Acc1EyeR"};

std::once_flag g_once;
bool g_installed = false;
void* const* g_manager = nullptr;
CreateImageFn g_create = nullptr;
ResizeImageFn g_resize = nullptr;
std::byte* const* g_deviceContextGlobal = nullptr;

std::mutex g_mutex;
stereo_seq::ScatterHistory g_history;
std::byte* g_deviceContext = nullptr; // the device context g_history's images belong to
std::atomic<bool> g_ready{false};

// The engine's four images (+0x2D0 .. +0x2E8 at start-up) and eye R's, both kept at one size.
void* g_engineImages[4] = {};
void* g_eyeRImages[4] = {};
std::atomic<std::uint64_t> g_resizes{0};

struct Counters {
    std::atomic<std::uint64_t> renders[2]{};
    std::atomic<std::uint64_t> swaps{0};
    std::atomic<std::uint64_t> skipped{0};
    std::atomic<std::uint64_t> inFlightDiffers{0}; // renders whose tag in flight names another eye
    std::atomic<int> logged{0};
    std::atomic<std::uint64_t> lastReport{0};
} g_counters;

bool requested() {
    std::wstring value;
    std::string narrow;
    if (readEnv(L"ETERNALVR_STEREO_SCATTER_TAA", value)) {
        for (const wchar_t c : value) {
            narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
    }
    return stereo_seq::switchValue(narrow, true);
}

stereo_seq::ScatterPair pairAt(const std::byte* deviceContext, int parity) {
    stereo_seq::ScatterPair p;
    const std::byte* at = deviceContext + kDeviceContextPairs + static_cast<std::size_t>(parity) * 0x10;
    std::memcpy(&p.packed0, at, sizeof(void*));
    std::memcpy(&p.packed1, at + sizeof(void*), sizeof(void*));
    return p;
}

void setPair(std::byte* deviceContext, int parity, const stereo_seq::ScatterPair& p) {
    std::byte* at = deviceContext + kDeviceContextPairs + static_cast<std::size_t>(parity) * 0x10;
    std::memcpy(at, &p.packed0, sizeof(void*));
    std::memcpy(at + sizeof(void*), &p.packed1, sizeof(void*));
}

// ---- Eye R's images, made with the device context (the renderer's start-up thread) ----

void onImagesBuilt(const HookRegisters& r) {
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
        EVR_LOG("%s: no image manager; the scattering filter stays off in stereo", kTag);
        return;
    }
    void* made[4] = {};
    for (int i = 0; i < 4; ++i) {
        made[i] = g_create(manager, kEyeRNames[i], description);
    }
    const std::array<stereo_seq::ScatterPair, 2> engine{pairAt(deviceContext, 0), pairAt(deviceContext, 1)};
    const std::array<stereo_seq::ScatterPair, 2> eyeR{stereo_seq::ScatterPair{made[0], made[1]},
                                                      stereo_seq::ScatterPair{made[2], made[3]}};
    g_history.reset(engine, eyeR);
    bool distinct = true;
    for (int i = 0; i < 4; ++i) {
        for (const auto& p : engine) {
            distinct = distinct && made[i] != p.packed0 && made[i] != p.packed1;
        }
    }
    EVR_LOG(
        "%s: eye R's scattering volumes made with device context %p: %p %p %p %p (the engine's %p %p %p %p)",
        kTag, static_cast<void*>(deviceContext), made[0], made[1], made[2], made[3], engine[0].packed0,
        engine[0].packed1, engine[1].packed0, engine[1].packed1);
    if (!g_history.ready() || !distinct) {
        EVR_LOG(
            "%s: eye R's volumes are missing or the engine's own; the scattering filter stays off in stereo",
            kTag);
        return;
    }
    void* const engineImages[4] = {engine[0].packed0, engine[0].packed1, engine[1].packed0,
                                   engine[1].packed1};
    for (int i = 0; i < 4; ++i) {
        g_engineImages[i] = engineImages[i];
        g_eyeRImages[i] = made[i];
    }
    g_deviceContext = deviceContext;
    g_ready.store(true, std::memory_order_release);
}

// ---- The render-size change (the renderer's thread, between frames) ----

void onVolumesResized(const HookRegisters&) {
    if (!g_ready.load(std::memory_order_acquire) || !mp_guard::allowsGameTouch()) {
        return;
    }
    std::byte* deviceContext = *g_deviceContextGlobal;
    std::lock_guard lock(g_mutex);
    if (!deviceContext || deviceContext != g_deviceContext) {
        return;
    }
    const std::byte* resized = nullptr;
    std::memcpy(&resized, deviceContext + kLastPairImage, sizeof(resized));
    if (!resized) {
        return;
    }
    std::int32_t size[3] = {};
    std::int32_t mips = 1;
    std::memcpy(size, resized + kImageSize, sizeof(size));
    std::memcpy(&mips, resized + kImageMips, sizeof(mips));
    int changed = 0;
    for (void* const* set : {g_engineImages, g_eyeRImages}) {
        for (int i = 0; i < 4; ++i) {
            if (set[i] && set[i] != resized && g_resize(set[i], size[0], size[1], size[2], mips)) {
                ++changed;
            }
        }
    }
    if (changed > 0) {
        g_history.invalidate();
    }
    const std::uint64_t n = ++g_resizes;
    if (changed > 0 || n <= 4) {
        EVR_LOG(
            "%s: scattering volumes resized to %dx%dx%d (%d mip(s)): %d of eye R's and the engine's other "
            "images followed",
            kTag, size[0], size[1], size[2], mips, changed);
    }
}

// ---- Each render: the eye's pairs and state (job threads; the renders of a tick never overlap) ----

void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counters.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_counters.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    EVR_LOG("%s: scattering history per eye: %llu eye L / mono and %llu eye R render(s), %llu state swap(s), "
            "%llu skipped; %llu render(s) whose tag in flight names another eye",
            kTag, static_cast<unsigned long long>(g_counters.renders[0].exchange(0)),
            static_cast<unsigned long long>(g_counters.renders[1].exchange(0)),
            static_cast<unsigned long long>(g_counters.swaps.exchange(0)),
            static_cast<unsigned long long>(g_counters.skipped.exchange(0)),
            static_cast<unsigned long long>(g_counters.inFlightDiffers.exchange(0)));
}

void onSetup(const HookRegisters& r) {
    if (!g_ready.load(std::memory_order_acquire) || !mp_guard::allowsGameTouch()) {
        return;
    }
    report();
    const auto* args = reinterpret_cast<const std::byte*>(r.rcx);
    const std::byte* context = nullptr;
    if (args) {
        std::memcpy(&context, args + kArgsContext, sizeof(context));
    }
    std::byte* state = nullptr;
    std::uint32_t counter = 0;
    if (context) {
        std::memcpy(&state, context + kContextState, sizeof(state));
        std::memcpy(&counter, context + kContextCounter, sizeof(counter));
    }
    std::byte* deviceContext = *g_deviceContextGlobal;
    std::lock_guard lock(g_mutex);
    if (!state || !deviceContext || deviceContext != g_deviceContext) {
        ++g_counters.skipped;
        return;
    }
    // The render's own tag, found by the counter the render-view job read for it (the backend frame counter
    // stored at RVA 0x1C568FC), the one the engine picks the pair's parity with. The tag in flight reads the
    // counter again now and would name the next render if the render thread's swap came in between; the
    // count of such renders is the self-check.
    const std::optional<stereo_seq::RenderTag> tag = seqTagForBackendFrame(counter + 1u);
    const stereo_seq::Eye eye = tag ? tag->eye : stereo_seq::Eye::Mono;
    const std::optional<stereo_seq::RenderTag> inFlight = seqTagInFlight();
    if ((inFlight ? inFlight->eye : stereo_seq::Eye::Mono) != eye) {
        ++g_counters.inFlightDiffers;
    }
    stereo_seq::ScatterState current{};
    std::memcpy(current.data(), state + stereo_seq::kScatterStateOffset, current.size());
    const stereo_seq::ScatterPlan plan = g_history.beforeRender(eye, counter, current);
    if (plan.load) {
        std::memcpy(state + stereo_seq::kScatterStateOffset, plan.load->data(), plan.load->size());
        ++g_counters.swaps;
    }
    setPair(deviceContext, 0, plan.slots[0]);
    setPair(deviceContext, 1, plan.slots[1]);
    ++g_counters.renders[eye == stereo_seq::Eye::Right ? 1 : 0];
    if (g_counters.logged.fetch_add(1) < 4) {
        EVR_LOG("%s: render %u for eye %s: writes %p/%p, reads %p/%p%s", kTag, counter,
                stereo_seq::eyeName(eye), plan.slots[counter & 1u].packed0, plan.slots[counter & 1u].packed1,
                plan.slots[(counter & 1u) ^ 1u].packed0, plan.slots[(counter & 1u) ^ 1u].packed1,
                plan.load ? ", its state swapped in" : "");
    }
}

} // namespace

bool installScatterHooksEarly() {
    std::call_once(g_once, [] {
        if (!requested()) {
            return;
        }
        if (!mp_guard::allowsGameTouch()) {
            EVR_LOG("%s: the multiplayer guard is not armed; the scattering filter stays off in stereo",
                    kTag);
            return;
        }
        GameImage image;
        if (!locateGameImage(image, kTag)) {
            return;
        }
        const std::byte* alloc = findUnique(image, kTag, "scattering volume images", kAllocSignature);
        const std::byte* setup = findUnique(image, kTag, "scattering setup", kSetupSignature);
        const std::byte* resizeSite = findUnique(image, kTag, "scattering volume resize", kResizeSignature);
        if (!alloc || !setup || !resizeSite) {
            EVR_LOG("%s: not installed; the scattering filter stays off in stereo", kTag);
            return;
        }
        const std::byte* name = ripTarget(image, alloc + kAllocName + 3, alloc + kAllocName + 7);
        const std::byte* manager = ripTarget(image, alloc + kAllocManager + 3, alloc + kAllocManager + 7);
        const std::byte* create = relativeTarget(alloc + kAllocCreate);
        const std::byte* dcGlobal =
            ripTarget(image, setup + kSetupDeviceContext + 3, setup + kSetupDeviceContext + 7);
        const std::byte* resizeDcGlobal =
            ripTarget(image, resizeSite + kResizeDeviceContext + 3, resizeSite + kResizeDeviceContext + 7);
        const std::byte* resize = relativeTarget(resizeSite + kResizeCall);
        if (!resize || !image.inText(resize) || functionStart(image, resize) != resize ||
            resizeDcGlobal != dcGlobal) {
            EVR_LOG("%s: the volume resize or its device context did not check out; not installed", kTag);
            return;
        }
        if (!name || stringAt(image, name) != "lightScatteringPacked1Acc1" || !manager ||
            !image.contains(manager, sizeof(void*)) || !create || !image.inText(create) ||
            functionStart(image, create) != create || functionStart(image, setup) != setup || !dcGlobal ||
            !image.contains(dcGlobal, sizeof(void*))) {
            EVR_LOG(
                "%s: the image manager, its create function or the device context global did not check out; "
                "not installed",
                kTag);
            return;
        }
        g_manager = reinterpret_cast<void* const*>(manager);
        g_create = reinterpret_cast<CreateImageFn>(const_cast<std::byte*>(create));
        g_resize = reinterpret_cast<ResizeImageFn>(const_cast<std::byte*>(resize));
        g_deviceContextGlobal = reinterpret_cast<std::byte* const*>(dcGlobal);
        std::string error;
        // The resize hook first: it does nothing until the images exist, so its failure leaves no hook that
        // acts.
        if (!installMidHook(const_cast<std::byte*>(resizeSite + kResizeHook), &onVolumesResized, error) ||
            !installMidHook(const_cast<std::byte*>(alloc + kAllocHook), &onImagesBuilt, error) ||
            !installMidHook(const_cast<std::byte*>(setup), &onSetup, error)) {
            EVR_LOG("%s: hook failed: %s; the scattering filter stays off in stereo", kTag, error.c_str());
            return;
        }
        g_installed = true;
        EVR_LOG("%s: scattering volumes (RVA 0x%X), setup (RVA 0x%X) and resize (RVA 0x%X) hooked: each eye "
                "keeps "
                "its own scattering history (image manager RVA 0x%X, create RVA 0x%X, resize RVA 0x%X)",
                kTag, image.rva(alloc + kAllocHook), image.rva(setup), image.rva(resizeSite + kResizeHook),
                image.rva(manager), image.rva(create), image.rva(resize));
    });
    return g_installed;
}

bool scatterPerEyeReady() {
    return g_installed && g_ready.load(std::memory_order_acquire) && mp_guard::allowsGameTouch();
}

} // namespace evr::vkcore
