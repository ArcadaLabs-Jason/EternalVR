#include "vkcore/taa_ngx.hpp"

#include "features/dlss_dll/dlss_dll.hpp"
#include "stereo_seq/alternate_eyes.hpp"
#include "stereo_seq/ngx_eye.hpp"
#include "stereo_seq/ngx_twin_retry.hpp"
#include "stereo_seq/stereo_taa.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/seh_filter.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/ui_engine.hpp"
#include "vkcore/window_timing.hpp"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-taa";

// NGX (nvsdk_ngx.h, SDK 1.x as linked into the game): results, the super-sampling feature, a handle's id.
constexpr int kNgxSuccess = 0x1;
constexpr int kNgxFail = static_cast<int>(0xBAD00000u);
constexpr int kFeatureSuperSampling = 1;
constexpr const char* kResetParameter = "Reset";
constexpr const char* kOutputParameter = "Output";

using CreateFn = int (*)(void* commandBuffer, int feature, void* parameters, void** handle);
using EvaluateFn = int (*)(void* commandBuffer, const void* handle, const void* parameters, void* callback);
using ReleaseFn = int (*)(void* handle);
using SetIntFn = void (*)(void* parameters, const char* name, int value);
using GetIntFn = int (*)(void* parameters, const char* name, int* value);
using GetUIntFn = int (*)(void* parameters, const char* name, unsigned int* value);
using GetVoidPointerFn = int (*)(void* parameters, const char* name, void** value);

// The create keys (nvsdk_ngx_defs.h), read back for the log when eye R's feature fails, and the render preset
// hint for each NVSDK_NGX_PerfQuality_Value (0 MaxPerf, 1 Balanced, 2 MaxQuality, 3 UltraPerformance,
// 4 UltraQuality, 5 DLAA).
constexpr const char* kSizeParameters[] = {"Width", "Height", "OutWidth", "OutHeight"};
constexpr const char* kQualityParameter = "PerfQualityValue";
constexpr const char* kPresetForQuality[] = {
    "DLSS.Hint.Render.Preset.Performance",  "DLSS.Hint.Render.Preset.Balanced",
    "DLSS.Hint.Render.Preset.Quality",      "DLSS.Hint.Render.Preset.UltraPerformance",
    "DLSS.Hint.Render.Preset.UltraQuality", "DLSS.Hint.Render.Preset.DLAA",
};
constexpr int kLoggedFailures = 32;
constexpr int kMaxTestFailures = 100;

CreateFn g_create = nullptr;
EvaluateFn g_evaluate = nullptr;
ReleaseFn g_release = nullptr;
SetIntFn g_setInt = nullptr;
GetIntFn g_getInt = nullptr;
GetUIntFn g_getUInt = nullptr; // optional: only the failure log reads with it
// Optional: without it each evaluation goes by the tag in flight (counted).
GetVoidPointerFn g_getVoidPointer = nullptr;

std::once_flag g_installOnce;
bool g_installed = false;
std::atomic<bool> g_active{false};
std::atomic<bool> g_twinFailed{false}; // g_retry.fallback(), read each tick without the lock
std::atomic<int> g_testFailures{0};    // ETERNALVR_TEST_DLSS_TWIN_FAIL: tries still to fail

std::mutex g_mutex; // g_twins, g_retry
stereo_seq::NgxTwins g_twins;
stereo_seq::NgxTwinRetry g_retry;

std::mutex g_eyeMutex; // g_outputs, g_resets
stereo_seq::NgxOutputBook g_outputs;
stereo_seq::NgxResetBook g_resets;

struct Counters {
    std::atomic<std::uint64_t> twinCreates{0};
    std::atomic<std::uint64_t> twinFailures{0};
    std::atomic<std::uint64_t> evaluates[2]{};
    std::atomic<std::uint64_t> evaluatesNoTwin{0};
    std::atomic<std::uint64_t> twinResets{0};
    std::atomic<std::uint64_t> leftResets{0};
    std::atomic<std::uint64_t> releases{0};
    std::atomic<std::uint64_t> ownTags{0};
    std::atomic<std::uint64_t> inFlightTags{0};
    std::atomic<std::uint64_t> inFlightDiffers{0};
    std::atomic<int> logged{0};
    std::atomic<int> failuresLogged{0};
} g_counters;

std::uintptr_t key(const void* handle) {
    return reinterpret_cast<std::uintptr_t>(handle);
}

unsigned handleId(const void* handle) {
    return handle ? *static_cast<const unsigned*>(handle) : 0u;
}

std::uint64_t nowMs() {
    return window_timing::nowMicros() / 1000;
}

// A size key as the game set it (the SDK's create helper uses SetUI; SetI is read too), 0 when unreadable.
unsigned sizeKey(void* parameters, const char* name) {
    unsigned value = 0;
    if (g_getUInt && g_getUInt(parameters, name, &value) == kNgxSuccess) {
        return value;
    }
    int signedValue = 0;
    return g_getInt(parameters, name, &signedValue) == kNgxSuccess ? static_cast<unsigned>(signedValue) : 0u;
}

// For the failure log: the create keys eye R's feature was made from, as the parameter block holds them.
std::string createKeys(void* parameters) {
    unsigned sizes[std::size(kSizeParameters)] = {};
    for (std::size_t i = 0; i < std::size(kSizeParameters); ++i) {
        sizes[i] = sizeKey(parameters, kSizeParameters[i]);
    }
    int quality = -1;
    if (g_getInt(parameters, kQualityParameter, &quality) != kNgxSuccess) {
        quality = -1;
    }
    unsigned preset = 0;
    const bool hasPreset = g_getUInt && quality >= 0 &&
                           static_cast<std::size_t>(quality) < std::size(kPresetForQuality) &&
                           g_getUInt(parameters, kPresetForQuality[quality], &preset) == kNgxSuccess;
    char text[160];
    std::snprintf(text, sizeof(text), "%ux%u -> %ux%u, PerfQualityValue %d, preset %s", sizes[0], sizes[1],
                  sizes[2], sizes[3], quality, hasPreset ? dlss_dll::presetName(preset).c_str() : "unset");
    return text;
}

// Eye R's first evaluation of a game feature (or the first after its failed twin was forgotten for a new
// try): its twin, made from the same parameter block (the game keeps one for its create and evaluate calls,
// so the create keys it set for this feature are still in it), recorded into this command buffer ahead of
// its evaluation. A failure falls back to TAA until the next try (NgxTwinRetry). Under g_mutex.
void createTwin(void* commandBuffer, const void* handle, void* parameters) {
    void* made = nullptr;
    // The test knob fails a try without creating anything, as NGX's own generic failure.
    const int testLeft = g_testFailures.load();
    if (testLeft > 0) {
        g_testFailures.store(testLeft - 1);
        EVR_LOG("%s: test: eye R's DLSS feature for the game's feature %u fails without a create "
                "(ETERNALVR_TEST_DLSS_TWIN_FAIL, %d more)",
                kTag, handleId(handle), testLeft - 1);
    }
    const int r = testLeft > 0 ? kNgxFail : g_create(commandBuffer, kFeatureSuperSampling, parameters, &made);
    const bool ok = r == kNgxSuccess && made;
    g_twins.created(key(handle), ok ? key(made) : 0);
    (ok ? g_counters.twinCreates : g_counters.twinFailures)++;
    if (!ok) {
        g_retry.failed(nowMs());
        g_twinFailed.store(g_retry.fallback());
        if (g_counters.failuresLogged.fetch_add(1) < kLoggedFailures) {
            const std::string next =
                g_retry.exhausted()
                    ? std::string("no try left until DLSS is chosen in the game's video menu")
                    : "next try in " + std::to_string(stereo_seq::ngxTwinRetryMs(g_retry.failures()) / 1000) +
                          " s";
            EVR_LOG("%s: eye R's DLSS feature for the game's feature %u: FAILED (%u, result 0x%X; %s): "
                    "temporal AA, failure %u in a row, %s",
                    kTag, handleId(handle), handleId(made), static_cast<unsigned>(r),
                    createKeys(parameters).c_str(), g_retry.failures(), next.c_str());
        }
        return;
    }
    // DLSS is per eye again only once every game feature has its twin.
    const bool recovered = !g_twins.anyFailed() && g_retry.created();
    g_twinFailed.store(g_retry.fallback());
    if (g_counters.logged.fetch_add(1) < 8 || recovered) {
        EVR_LOG("%s: eye R's DLSS feature for the game's feature %u: created (%u, result 0x%X)%s", kTag,
                handleId(handle), handleId(made), static_cast<unsigned>(r),
                recovered ? ": DLSS per eye again after a failure" : "");
    }
}

// The first two handles of an NVSDK_NGX_Resource_VK (nvsdk_ngx_defs_vk.h): its image view, then its image.
// Read under a handler: the pointer is the game's, set for this evaluation.
bool readResourceHandles(const void* resource, std::uint64_t (&out)[2]) {
    __try {
        std::memcpy(out, resource, sizeof(out));
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

// The render's own tag: the one its output selector noted for the evaluation's `Output` image. Either
// handle is compared (an image view never has an image's handle).
std::optional<stereo_seq::RenderTag> ownTag(const void* parameters) {
    void* output = nullptr;
    std::uint64_t handles[2] = {};
    if (!g_getVoidPointer ||
        g_getVoidPointer(const_cast<void*>(parameters), kOutputParameter, &output) != kNgxSuccess ||
        !output || !readResourceHandles(output, handles)) {
        return std::nullopt;
    }
    std::lock_guard lock(g_eyeMutex);
    for (const std::uint64_t handle : handles) {
        if (auto tag = g_outputs.find(handle)) {
            return tag;
        }
    }
    return std::nullopt;
}

stereo_seq::NgxEyePick pickEye(const void* parameters) {
    const stereo_seq::NgxEyePick pick = stereo_seq::pickNgxEye(ownTag(parameters), seqTagInFlight());
    if (pick.own) {
        ++g_counters.ownTags;
    }
    if (pick.fallback) {
        ++g_counters.inFlightTags;
    }
    if (pick.inFlightDiffers) {
        ++g_counters.inFlightDiffers;
    }
    return pick;
}

// The per-eye hook reset this render's history.
bool takeReset(const stereo_seq::RenderTag& tag) {
    std::lock_guard lock(g_eyeMutex);
    return g_resets.take(tag.eye, tag.tick);
}

// Evaluates `feature`, with "Reset" raised for this evaluation only when `reset`.
int evaluate(void* commandBuffer, const void* feature, const void* parameters, void* callback, bool reset) {
    if (!reset) {
        return g_evaluate(commandBuffer, feature, parameters, callback);
    }
    auto* params = const_cast<void*>(parameters);
    int gameReset = 0;
    if (g_getInt(params, kResetParameter, &gameReset) != kNgxSuccess) {
        gameReset = 0;
    }
    g_setInt(params, kResetParameter, 1);
    const int result = g_evaluate(commandBuffer, feature, parameters, callback);
    g_setInt(params, kResetParameter, gameReset);
    return result;
}

int evaluateHook(void* commandBuffer, const void* handle, const void* parameters, void* callback) {
    if (!g_active.load(std::memory_order_acquire) || !mp_guard::allowsGameTouch()) {
        return g_evaluate(commandBuffer, handle, parameters, callback);
    }
    const std::optional<stereo_seq::RenderTag> tag = pickEye(parameters).tag;
    if (!tag || tag->eye != stereo_seq::Eye::Right) {
        ++g_counters.evaluates[0];
        // Eye L's history is reset with eye R's (the per-eye hook resets both eyes of a tick).
        const bool reset = tag && tag->eye == stereo_seq::Eye::Left && takeReset(*tag);
        if (reset) {
            ++g_counters.leftResets;
        }
        return evaluate(commandBuffer, handle, parameters, callback, reset);
    }
    std::uintptr_t twin = 0;
    bool reset = false;
    {
        std::lock_guard lock(g_mutex);
        if (!g_twins.known(key(handle))) {
            createTwin(commandBuffer, handle, const_cast<void*>(parameters));
        }
        twin = g_twins.twinOf(key(handle));
        if (twin) {
            // With alternate eyes eye R evaluates every other game frame; with auto, every game frame or
            // every other one.
            reset = g_twins.resetTwin(key(handle), tag->tick,
                                      stereo_seq::eyeFrameMinStep(seqAlternateEyes(), seqAdaptiveEyes()),
                                      stereo_seq::eyeFrameStep(seqAlternateEyes()));
        }
    }
    // Taken either way, so that a reset noted for this render is not left for a later one.
    reset = takeReset(*tag) || reset;
    if (!twin) {
        ++g_counters.evaluatesNoTwin;
        return g_evaluate(commandBuffer, handle, parameters, callback);
    }
    ++g_counters.evaluates[1];
    if (reset) {
        ++g_counters.twinResets;
    }
    return evaluate(commandBuffer, reinterpret_cast<const void*>(twin), parameters, callback, reset);
}

int releaseHook(void* handle) {
    std::uintptr_t twin = 0;
    {
        std::lock_guard lock(g_mutex);
        // A feature whose twin failed: the game makes a new one (another quality or size), worth a try now.
        const bool failedTwin = g_twins.known(key(handle)) && !g_twins.twinOf(key(handle));
        twin = g_twins.released(key(handle));
        if (failedTwin) {
            g_retry.released();
        }
    }
    // The twin is the layer's own feature: released with the game's even after a guard trip.
    if (twin) {
        const int r = g_release(reinterpret_cast<void*>(twin));
        if (r != kNgxSuccess) {
            EVR_LOG("%s: eye R's DLSS feature could not be released (0x%X)", kTag, static_cast<unsigned>(r));
        }
    }
    ++g_counters.releases;
    return g_release(handle);
}

void* exported(const GameImage& image, const char* name) {
    auto* p = reinterpret_cast<std::byte*>(GetProcAddress(GetModuleHandleW(nullptr), name));
    if (!p || !image.inText(p)) {
        EVR_LOG("%s: the game exports no %s in its code", kTag, name);
        return nullptr;
    }
    return p;
}

// ETERNALVR_TEST_DLSS_TWIN_FAIL=<n> (a test knob, never set by the launcher): eye R's first n tries fail.
void readTestFailures() {
    std::wstring value;
    if (!readEnv(L"ETERNALVR_TEST_DLSS_TWIN_FAIL", value) || value.empty()) {
        return;
    }
    int n = 0;
    for (const wchar_t c : value) {
        if (c < L'0' || c > L'9' || n > kMaxTestFailures) {
            n = -1;
            break;
        }
        n = n * 10 + (c - L'0');
    }
    if (n < 1 || n > kMaxTestFailures) {
        EVR_LOG("%s: test: ETERNALVR_TEST_DLSS_TWIN_FAIL is not a count from 1 to %d; no failure", kTag,
                kMaxTestFailures);
        return;
    }
    g_testFailures.store(n);
    EVR_LOG("%s: test: eye R's first %d DLSS feature tries fail (ETERNALVR_TEST_DLSS_TWIN_FAIL, a test knob)",
            kTag, n);
}

bool hook(void* target, void* destination, void** original, const char* name) {
    std::string error;
    if (!installInlineHook(target, destination, original, error)) {
        EVR_LOG("%s: %s hook failed: %s", kTag, name, error.c_str());
        return false;
    }
    return true;
}

} // namespace

bool installNgxTwins() {
    std::call_once(g_installOnce, [] {
        GameImage image;
        if (!locateGameImage(image, kTag)) {
            return;
        }
        g_create = reinterpret_cast<CreateFn>(exported(image, "NVSDK_NGX_VULKAN_CreateFeature"));
        void* evaluate = exported(image, "NVSDK_NGX_VULKAN_EvaluateFeature_C");
        void* release = exported(image, "NVSDK_NGX_VULKAN_ReleaseFeature");
        g_setInt = reinterpret_cast<SetIntFn>(exported(image, "NVSDK_NGX_Parameter_SetI"));
        g_getInt = reinterpret_cast<GetIntFn>(exported(image, "NVSDK_NGX_Parameter_GetI"));
        g_getUInt = reinterpret_cast<GetUIntFn>(exported(image, "NVSDK_NGX_Parameter_GetUI"));
        g_getVoidPointer =
            reinterpret_cast<GetVoidPointerFn>(exported(image, "NVSDK_NGX_Parameter_GetVoidPointer"));
        if (!g_create || !evaluate || !release || !g_setInt || !g_getInt) {
            return;
        }
        // Release first: every twin the evaluate hook makes is released with its game feature. Creating
        // needs no hook (the game's own create is called directly for the twin).
        if (!hook(release, reinterpret_cast<void*>(&releaseHook), reinterpret_cast<void**>(&g_release),
                  "ReleaseFeature") ||
            !hook(evaluate, reinterpret_cast<void*>(&evaluateHook), reinterpret_cast<void**>(&g_evaluate),
                  "EvaluateFeature_C")) {
            return;
        }
        readTestFailures();
        g_installed = true;
        EVR_LOG("%s: NGX exports hooked (evaluate RVA 0x%X, release RVA 0x%X; create RVA 0x%X); each "
                "evaluation's eye by %s",
                kTag, image.rva(static_cast<std::byte*>(evaluate)),
                image.rva(static_cast<std::byte*>(release)),
                image.rva(reinterpret_cast<std::byte*>(g_create)),
                g_getVoidPointer ? "its Output image (the tag in flight without one)" : "the tag in flight");
    });
    return g_installed;
}

void setNgxTwinsActive(bool active) {
    g_active.store(active && g_installed, std::memory_order_release);
}

bool ngxTwinFailed() {
    return g_twinFailed.load();
}

void ngxTwinsTick() {
    if (!g_twinFailed.load()) {
        return;
    }
    std::lock_guard lock(g_mutex);
    const std::uint32_t failures = g_retry.failures();
    if (!g_retry.due(nowMs())) {
        return;
    }
    const std::size_t forgotten = g_twins.forgetFailed();
    g_twinFailed.store(g_retry.fallback());
    EVR_LOG(
        "%s: trying eye R's DLSS feature again (%u failure(s) in a row, %zu game feature(s) without one): "
        "DLSS held again, the feature is made at eye R's next evaluation",
        kTag, failures, forgotten);
}

bool retryNgxTwins() {
    std::lock_guard lock(g_mutex);
    if (!g_retry.requested() || !g_retry.due(nowMs())) {
        return false;
    }
    const std::size_t forgotten = g_twins.forgetFailed();
    g_twinFailed.store(g_retry.fallback());
    EVR_LOG(
        "%s: DLSS chosen in the game's video menu: trying eye R's DLSS feature again (%zu game feature(s) "
        "without one)",
        kTag, forgotten);
    return true;
}

void noteNgxOutput(const void* target, const stereo_seq::RenderTag& tag) {
    if (!g_active.load(std::memory_order_acquire) || !g_getVoidPointer) {
        return;
    }
    // The render target's colour image, or an image set's member in use (read as the motion capture does).
    const auto fields = ui_engine::readImageOrTarget(reinterpret_cast<std::uintptr_t>(target));
    if (!fields) {
        return;
    }
    std::uint64_t image = fields->vkImage;
    if ((fields->flags & ui_layer::engine::kImageSetFlag) != 0) {
        const auto member = ui_engine::readSetMember(fields->vkImage);
        image = member ? member->second : 0;
    }
    std::lock_guard lock(g_eyeMutex);
    g_outputs.note(image, tag);
}

void noteNgxReset(stereo_seq::Eye eye, std::uint64_t gameFrame) {
    std::lock_guard lock(g_eyeMutex);
    g_resets.note(eye, gameFrame);
}

NgxCounters ngxCounters() {
    NgxCounters c;
    c.twinCreates = g_counters.twinCreates.load();
    c.twinFailures = g_counters.twinFailures.load();
    c.evaluates[0] = g_counters.evaluates[0].load();
    c.evaluates[1] = g_counters.evaluates[1].load();
    c.evaluatesNoTwin = g_counters.evaluatesNoTwin.load();
    c.twinResets = g_counters.twinResets.load();
    c.leftResets = g_counters.leftResets.load();
    c.releases = g_counters.releases.load();
    c.ownTags = g_counters.ownTags.load();
    c.inFlightTags = g_counters.inFlightTags.load();
    c.inFlightDiffers = g_counters.inFlightDiffers.load();
    return c;
}

} // namespace evr::vkcore
