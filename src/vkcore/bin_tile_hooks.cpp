#include "vkcore/bin_tile_hooks.hpp"

#include "stereo_seq/bin_tiles.hpp"
#include "stereo_seq/stereo_taa.hpp"
#include "vkcore/game_code.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/stereo_hooks.hpp"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-bins";

// The render-view job (RVA 0x1C5794B): the binning setup's arguments (rcx = [r12 + 0x18], the idRenderView;
// rdx = r15, the render context) and `call 0x1CFC050` at +0x4E; the hook goes on the next instruction
// (+0x53), where r12 and r15 still hold them.
constexpr const char* kCallSiteSignature =
    "48 8B 05 ?? ?? ?? ?? 4C 8D 4C 24 50 49 8B 4C 24 18 4C 8D 44 24 30 48 89 44 24 30 49 8B D7 48 8B "
    "05 ?? ?? ?? ?? 48 89 44 24 38 48 8B 05 ?? ?? ?? ?? 48 89 44 24 50 48 8B 05 ?? ?? ?? ?? 48 89 44 "
    "24 58 48 8B 05 ?? ?? ?? ?? 48 89 44 24 60 E8 ?? ?? ?? ?? 49 8B 4C 24 18 49 8B D7 E8";
constexpr std::size_t kSetupCall = 0x4E;
constexpr std::size_t kHookSite = 0x53;
constexpr std::size_t kViewInArgs = 0x18; // [r12 + 0x18]

// In the binning setup (RVA 0x1CFC31C): tan(fov_x / 2) and tan(fov_y / 2) from the idRenderView (r15), then
// binTileWidth (`mov rdx, [rip + param]` at +0x37, the setter call at +0x5C), binTileHeight (+0x61),
// binTileLeft (+0x8C) and binTileTop (+0xAE), each a `mov rdx, [rip + param]`.
constexpr const char* kParamSignature =
    "F3 45 0F 10 87 F8 89 02 00 F3 44 0F 59 05 ?? ?? ?? ?? F3 0F 10 3D ?? ?? ?? ?? 0F 5B C9 0F 28 C7 "
    "F3 45 0F 59 C7 F3 0F 5E C1 F3 44 0F 59 C8 41 0F 28 C0 E8 ?? ?? ?? ?? 48 8B 15 ?? ?? ?? ?? 0F 28 "
    "D0 66 41 0F 6E 87 D8 98 02 00 49 8B CD 0F 5B C0 F3 0F 5E F8 F3 0F 59 D7 F3 0F 58 D2 E8 ?? ?? ?? "
    "?? 48 8B 15 ?? ?? ?? ?? F3 45 0F 58 C9 49 8B CD 41 0F 28 D1 E8 ?? ?? ?? ?? 41 0F 28 C0 E8 ?? ?? "
    "?? ?? 0F 57 05 ?? ?? ?? ?? 49 8B CD 48 8B 15 ?? ?? ?? ?? 0F 28 D0 E8 ?? ?? ?? ?? 41 0F 28 C2 E8 "
    "?? ?? ?? ?? 0F 57 05 ?? ?? ?? ?? 49 8B CD 48 8B 15 ?? ?? ?? ?? 0F 28 D0 E8";
constexpr std::size_t kParamLoads[4] = {0x37, 0x61, 0x8C, 0xAE}; // width, height, left, top
constexpr std::size_t kSetterCall = 0x5C;

// idRenderView: fov_x / fov_y (renderView_t + 0x28 at +0x289D0) and the render size the setup divides by.
constexpr std::size_t kFovX = 0x289F8;
constexpr std::size_t kFovY = 0x289FC;
constexpr std::size_t kRenderWidth = 0x298D8;
constexpr std::size_t kRenderHeight = 0x298DC;
// A parameter object's index into the context's table (the setter reads it); a value sits at
// context + (index + 1) * 16.
constexpr std::size_t kParamIndex = 0x88;

using SetParamFn = void (*)(void* context, void* param, float value);

std::once_flag g_once;
bool g_installed = false;
SetParamFn g_setParam = nullptr;
void* const* g_params[4] = {}; // the globals holding binTileWidth, binTileHeight, binTileLeft, binTileTop

struct Counters {
    std::atomic<std::uint64_t> views{0};     // parameters set from the projection
    std::atomic<std::uint64_t> changed{0};   // ... where they differ from the engine's
    std::atomic<std::uint64_t> unchanged{0}; // ... where they match (symmetric frustum)
    std::atomic<std::uint64_t> skipped{0};   // no usable projection or parameter
    std::atomic<int> logged{0};
    std::atomic<std::uint64_t> lastReport{0}; // GetTickCount64 of the last summary
} g_counters;

float engineValue(const std::byte* context, const std::byte* param) {
    std::int32_t index = 0;
    std::memcpy(&index, param + kParamIndex, sizeof(index));
    float value = 0.0f;
    std::memcpy(&value, context + (static_cast<std::size_t>(index) + 1) * 16, sizeof(value));
    return value;
}

void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counters.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_counters.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    EVR_LOG("%s: bin tiles set from the projection for %llu view render(s): %llu changed (asymmetric), %llu "
            "as the engine's, %llu skipped",
            kTag, static_cast<unsigned long long>(g_counters.views.exchange(0)),
            static_cast<unsigned long long>(g_counters.changed.exchange(0)),
            static_cast<unsigned long long>(g_counters.unchanged.exchange(0)),
            static_cast<unsigned long long>(g_counters.skipped.exchange(0)));
}

void onBinningSetup(const HookRegisters& r) {
    report();
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    const auto* args = reinterpret_cast<const std::byte*>(r.r12);
    auto* context = reinterpret_cast<std::byte*>(r.r15);
    if (!args || !context) {
        ++g_counters.skipped;
        return;
    }
    const std::byte* view = nullptr;
    std::memcpy(&view, args + kViewInArgs, sizeof(view));
    if (!view) {
        ++g_counters.skipped;
        return;
    }
    stereo_seq::Matrix4 projection{};
    std::memcpy(projection.data(), view + render_view_object::kProjection, sizeof(projection));
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::memcpy(&width, view + kRenderWidth, sizeof(width));
    std::memcpy(&height, view + kRenderHeight, sizeof(height));
    const auto p = stereo_seq::binTileParams(projection, width, height);
    std::byte* params[4] = {};
    for (int i = 0; i < 4; ++i) {
        params[i] = static_cast<std::byte*>(*g_params[i]);
    }
    if (!p || !params[0] || !params[1] || !params[2] || !params[3]) {
        ++g_counters.skipped;
        return;
    }
    const float ours[4] = {p->width, p->height, p->left, p->top};
    float engine[4] = {};
    bool differs = false;
    for (int i = 0; i < 4; ++i) {
        engine[i] = engineValue(context, params[i]);
        differs = differs || std::fabs(engine[i] - ours[i]) > 1e-4f * std::fmax(1.0f, std::fabs(ours[i]));
    }
    if (g_counters.logged.fetch_add(1) < 4) {
        float fovX = 0.0f;
        float fovY = 0.0f;
        std::memcpy(&fovX, view + kFovX, sizeof(fovX));
        std::memcpy(&fovY, view + kFovY, sizeof(fovY));
        EVR_LOG(
            "%s: view %ux%u, fov %.2f x %.2f, projection [0] %.4f [2] %.4f [5] %.4f [6] %.4f: engine width "
            "%.6f height %.6f left %.4f top %.4f -> width %.6f height %.6f left %.4f top %.4f",
            kTag, static_cast<unsigned>(width), static_cast<unsigned>(height), fovX, fovY, projection[0],
            projection[2], projection[5], projection[6], engine[0], engine[1], engine[2], engine[3], ours[0],
            ours[1], ours[2], ours[3]);
    }
    for (int i = 0; i < 4; ++i) {
        g_setParam(context, params[i], ours[i]);
    }
    ++g_counters.views;
    ++(differs ? g_counters.changed : g_counters.unchanged);
}

bool enabled() {
    std::wstring value;
    std::string narrow;
    if (readEnv(L"ETERNALVR_STEREO_BIN_TILES", value)) {
        for (const wchar_t c : value) {
            narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
    }
    return stereo_seq::switchValue(narrow, true);
}

} // namespace

bool installBinTileHook() {
    std::call_once(g_once, [] {
        if (!enabled()) {
            EVR_LOG("%s: off (ETERNALVR_STEREO_BIN_TILES=0): the binning keeps the symmetric tile grid",
                    kTag);
            return;
        }
        GameImage image;
        if (!locateGameImage(image, kTag)) {
            return;
        }
        const std::byte* site = findUnique(image, kTag, "binning setup call", kCallSiteSignature);
        const std::byte* loads = findUnique(image, kTag, "bin tile parameters", kParamSignature);
        if (!site || !loads) {
            EVR_LOG("%s: not installed; the binning keeps the symmetric tile grid", kTag);
            return;
        }
        const std::byte* setup = relativeTarget(site + kSetupCall);
        const std::byte* setter = relativeTarget(loads + kSetterCall);
        if (!setup || !image.inText(setup) || functionStart(image, setup) != setup ||
            functionStart(image, loads) != setup || !setter || !image.inText(setter) ||
            functionStart(image, setter) != setter) {
            EVR_LOG(
                "%s: the call does not reach the function that sets the bin tiles, or the setter is not a "
                "function; not installed",
                kTag);
            return;
        }
        for (int i = 0; i < 4; ++i) {
            const std::byte* at = loads + kParamLoads[i];
            const std::byte* global = ripTarget(image, at + 3, at + 7);
            if (!global || !image.contains(global, sizeof(void*))) {
                EVR_LOG("%s: bin tile parameter %d is outside the module; not installed", kTag, i);
                return;
            }
            g_params[i] = reinterpret_cast<void* const*>(global);
        }
        g_setParam = reinterpret_cast<SetParamFn>(const_cast<std::byte*>(setter));
        std::string error;
        if (!installMidHook(const_cast<std::byte*>(site + kHookSite), &onBinningSetup, error)) {
            EVR_LOG("%s: hook failed: %s; the binning keeps the symmetric tile grid", kTag, error.c_str());
            return;
        }
        g_installed = true;
        EVR_LOG("%s: binning setup (RVA 0x%X) hooked at RVA 0x%X: bin tiles follow each view's projection "
                "(setter RVA 0x%X)",
                kTag, image.rva(setup), image.rva(site + kHookSite), image.rva(setter));
    });
    return g_installed;
}

} // namespace evr::vkcore
