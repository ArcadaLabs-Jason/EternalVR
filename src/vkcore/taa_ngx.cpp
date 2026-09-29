#include "vkcore/taa_ngx.hpp"

#include "stereo_seq/alternate_eyes.hpp"
#include "stereo_seq/stereo_taa.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/seq_hooks.hpp"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-taa";

// NGX (nvsdk_ngx.h, SDK 1.x as linked into the game): results, the super-sampling feature, a handle's id.
constexpr int kNgxSuccess = 0x1;
constexpr int kFeatureSuperSampling = 1;
constexpr const char* kResetParameter = "Reset";

using CreateFn = int (*)(void* commandBuffer, int feature, void* parameters, void** handle);
using EvaluateFn = int (*)(void* commandBuffer, const void* handle, const void* parameters, void* callback);
using ReleaseFn = int (*)(void* handle);
using SetIntFn = void (*)(void* parameters, const char* name, int value);
using GetIntFn = int (*)(void* parameters, const char* name, int* value);

CreateFn g_create = nullptr;
EvaluateFn g_evaluate = nullptr;
ReleaseFn g_release = nullptr;
SetIntFn g_setInt = nullptr;
GetIntFn g_getInt = nullptr;

std::once_flag g_installOnce;
bool g_installed = false;
std::atomic<bool> g_active{false};
std::atomic<bool> g_twinFailed{false};

std::mutex g_mutex; // g_twins
stereo_seq::NgxTwins g_twins;

struct Counters {
    std::atomic<std::uint64_t> twinCreates{0};
    std::atomic<std::uint64_t> twinFailures{0};
    std::atomic<std::uint64_t> evaluates[2]{};
    std::atomic<std::uint64_t> evaluatesNoTwin{0};
    std::atomic<std::uint64_t> twinResets{0};
    std::atomic<std::uint64_t> releases{0};
    std::atomic<int> logged{0};
} g_counters;

std::uintptr_t key(const void* handle) {
    return reinterpret_cast<std::uintptr_t>(handle);
}

unsigned handleId(const void* handle) {
    return handle ? *static_cast<const unsigned*>(handle) : 0u;
}

int evaluateHook(void* commandBuffer, const void* handle, const void* parameters, void* callback) {
    if (!g_active.load(std::memory_order_acquire) || !mp_guard::allowsGameTouch()) {
        return g_evaluate(commandBuffer, handle, parameters, callback);
    }
    const std::optional<stereo_seq::RenderTag> tag = seqTagInFlight();
    if (!tag || tag->eye != stereo_seq::Eye::Right) {
        ++g_counters.evaluates[0];
        return g_evaluate(commandBuffer, handle, parameters, callback);
    }
    std::uintptr_t twin = 0;
    bool reset = false;
    {
        std::lock_guard lock(g_mutex);
        if (!g_twins.known(key(handle))) {
            // Eye R's first evaluation of this game feature: its twin, made from the same parameter block
            // (the game keeps one for its create and evaluate calls, so the create keys it set for this
            // feature are still in it), recorded into this command buffer ahead of its evaluation.
            void* made = nullptr;
            const int r =
                g_create(commandBuffer, kFeatureSuperSampling, const_cast<void*>(parameters), &made);
            const bool ok = r == kNgxSuccess && made;
            g_twins.created(key(handle), ok ? key(made) : 0);
            (ok ? g_counters.twinCreates : g_counters.twinFailures)++;
            if (!ok) {
                g_twinFailed.store(true);
            }
            if (g_counters.logged.fetch_add(1) < 8) {
                EVR_LOG("%s: eye R's DLSS feature for the game's feature %u: %s (%u, result 0x%X)", kTag,
                        handleId(handle), ok ? "created" : "FAILED", handleId(made),
                        static_cast<unsigned>(r));
            }
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
    if (!twin) {
        ++g_counters.evaluatesNoTwin;
        return g_evaluate(commandBuffer, handle, parameters, callback);
    }
    ++g_counters.evaluates[1];
    auto* params = const_cast<void*>(parameters);
    int gameReset = 0;
    if (reset) {
        ++g_counters.twinResets;
        if (g_getInt(params, kResetParameter, &gameReset) != kNgxSuccess) {
            gameReset = 0;
        }
        g_setInt(params, kResetParameter, 1);
    }
    const int result = g_evaluate(commandBuffer, reinterpret_cast<const void*>(twin), parameters, callback);
    if (reset) {
        g_setInt(params, kResetParameter, gameReset);
    }
    return result;
}

int releaseHook(void* handle) {
    std::uintptr_t twin = 0;
    {
        std::lock_guard lock(g_mutex);
        twin = g_twins.released(key(handle));
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
        g_installed = true;
        EVR_LOG("%s: NGX exports hooked (evaluate RVA 0x%X, release RVA 0x%X; create RVA 0x%X)", kTag,
                image.rva(static_cast<std::byte*>(evaluate)), image.rva(static_cast<std::byte*>(release)),
                image.rva(reinterpret_cast<std::byte*>(g_create)));
    });
    return g_installed;
}

void setNgxTwinsActive(bool active) {
    g_active.store(active && g_installed, std::memory_order_release);
}

bool ngxTwinFailed() {
    return g_twinFailed.load();
}

NgxCounters ngxCounters() {
    NgxCounters c;
    c.twinCreates = g_counters.twinCreates.load();
    c.twinFailures = g_counters.twinFailures.load();
    c.evaluates[0] = g_counters.evaluates[0].load();
    c.evaluates[1] = g_counters.evaluates[1].load();
    c.evaluatesNoTwin = g_counters.evaluatesNoTwin.load();
    c.twinResets = g_counters.twinResets.load();
    c.releases = g_counters.releases.load();
    return c;
}

} // namespace evr::vkcore
