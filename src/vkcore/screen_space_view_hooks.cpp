#include "vkcore/screen_space_view_hooks.hpp"

#include "stereo_seq/screen_space_view.hpp"
#include "vkcore/game_code.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/ssr_fade_hooks.hpp"
#include "vkcore/stereo_hooks.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-ssr-view";

// The render-view job (RVA 0x1C54C53): inverseViewMatrixX, Y and Z set from the idRenderView (r13 + 0x29400,
// + 0x29410, + 0x29420) into the parameter context (r15), each `mov rdx, [rip + param]; lea r8, [r13 + row];
// mov rcx, r15; call setter`, then the same `mov rdx` / `lea` for inverseViewMatrixW (+0x42, the hook).
constexpr const char* kSiteSignature = "48 8B 15 ?? ?? ?? ?? 4D 8D 85 00 94 02 00 49 8B CF E8 ?? ?? ?? ?? "
                                       "48 8B 15 ?? ?? ?? ?? 4D 8D 85 10 94 02 00 49 8B CF E8 ?? ?? ?? ?? "
                                       "48 8B 15 ?? ?? ?? ?? 4D 8D 85 20 94 02 00 49 8B CF E8 ?? ?? ?? ?? "
                                       "48 8B 15 ?? ?? ?? ?? 4D 8D 85 30 94 02 00";
constexpr std::size_t kParamLoads[3] = {0x0, 0x16, 0x2C}; // the `mov rdx, [rip + param]` of X, Y, Z
constexpr std::size_t kSetterCall = 0x11;
constexpr std::size_t kHookSite = 0x42;
constexpr std::size_t kRows = 0x29400; // inverseViewMatrix, row-major, in the idRenderView
constexpr std::string_view kNames[3] = {"inverseViewMatrixX", "inverseViewMatrixY", "inverseViewMatrixZ"};
// A parameter's declaration in .data: its name 0x10 before the handle the `mov rdx` reads.
constexpr std::size_t kNameBefore = 0x10;

using SetVec4Fn = void (*)(void* context, void* param, const float* value);

std::once_flag g_once;
bool g_installed = false;
SetVec4Fn g_setVec4 = nullptr;
void* const* g_params[3] = {};

struct Counters {
    std::atomic<std::uint64_t> sheared{0}; // views with an asymmetric projection: rows set sheared
    std::atomic<std::uint64_t> centred{0}; // symmetric projection: left as the engine set them
    std::atomic<std::uint64_t> skipped{0}; // no usable projection or parameter
    // Off-centre but left unsheared: not every SSR shader fades in the shared frame (ssr_fade_hooks.hpp).
    std::atomic<std::uint64_t> held{0};
    std::atomic<int> logged{0};
    std::atomic<std::uint64_t> lastReport{0}; // GetTickCount64 of the last summary
} g_counters;

void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counters.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_counters.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    const std::uint64_t sheared = g_counters.sheared.exchange(0);
    const std::uint64_t centred = g_counters.centred.exchange(0);
    const std::uint64_t skipped = g_counters.skipped.exchange(0);
    const std::uint64_t held = g_counters.held.exchange(0);
    if (sheared + centred + skipped + held == 0) {
        return;
    }
    EVR_LOG("%s: SSR view rotation set for %llu view render(s) with an off-centre projection, %llu centred "
            "left as the engine's, %llu skipped, %llu left unsheared (the SSR fade is not patched in every "
            "shader)",
            kTag, static_cast<unsigned long long>(sheared), static_cast<unsigned long long>(centred),
            static_cast<unsigned long long>(skipped), static_cast<unsigned long long>(held));
}

void onViewParams(const HookRegisters& r) {
    report();
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    const auto* view = reinterpret_cast<const std::byte*>(r.r13);
    auto* context = reinterpret_cast<std::byte*>(r.r15);
    void* params[3] = {};
    for (int i = 0; i < 3; ++i) {
        params[i] = *g_params[i];
    }
    if (!view || !context || !params[0] || !params[1] || !params[2]) {
        ++g_counters.skipped;
        return;
    }
    stereo_seq::Matrix4 projection{};
    std::memcpy(projection.data(), view + render_view_object::kProjection, sizeof(projection));
    const auto shear = stereo_seq::screenSpaceShear(projection);
    if (!shear) {
        ++g_counters.skipped;
        return;
    }
    if (std::fabs(shear->x) < 1e-6f && std::fabs(shear->y) < 1e-6f) {
        ++g_counters.centred;
        return;
    }
    if (!ssr_fade::coherent()) {
        ++g_counters.held; // the shear only with the shared-frame fade: never one without the other
        return;
    }
    std::array<stereo_seq::ParamRow, 3> rows{};
    std::memcpy(rows.data(), view + kRows, sizeof(rows));
    const auto sheared = stereo_seq::shearRotationRows(rows, *shear);
    if (g_counters.logged.fetch_add(1) < 2) {
        EVR_LOG("%s: projection [0] %.4f [2] %.4f [5] %.4f [6] %.4f: shear x %.4f y %.4f; row X %.4f %.4f "
                "%.4f -> "
                "%.4f %.4f",
                kTag, projection[0], projection[2], projection[5], projection[6], shear->x, shear->y,
                rows[0][0], rows[0][1], rows[0][2], sheared[0][0], sheared[0][1]);
    }
    for (int i = 0; i < 3; ++i) {
        g_setVec4(context, params[i], sheared[static_cast<std::size_t>(i)].data());
    }
    ++g_counters.sheared;
}

std::string_view declaredName(const GameImage& image, const std::byte* handle) {
    if (!handle || handle - image.base < static_cast<std::ptrdiff_t>(kNameBefore) ||
        !image.contains(handle - kNameBefore, sizeof(std::uintptr_t))) {
        return {};
    }
    std::uintptr_t namePointer = 0;
    std::memcpy(&namePointer, handle - kNameBefore, sizeof(namePointer));
    return stringAt(image, reinterpret_cast<const std::byte*>(namePointer), 32);
}

} // namespace

bool installScreenSpaceViewHook() {
    std::call_once(g_once, [] {
        if (!mp_guard::allowsGameTouch()) {
            EVR_LOG("%s: the multiplayer guard is not armed; not installed", kTag);
            return;
        }
        GameImage image;
        if (!locateGameImage(image, kTag)) {
            return;
        }
        const std::byte* site = findUnique(image, kTag, "inverse view matrix parameters", kSiteSignature);
        if (!site) {
            EVR_LOG("%s: not installed; SSR keeps the centred view rotation", kTag);
            return;
        }
        for (int i = 0; i < 3; ++i) {
            const std::byte* at = site + kParamLoads[i];
            const std::byte* handle = ripTarget(image, at + 3, at + 7);
            if (!handle || !image.contains(handle, sizeof(void*)) ||
                declaredName(image, handle) != kNames[i]) {
                EVR_LOG("%s: parameter %d is not %.*s; not installed", kTag, i,
                        static_cast<int>(kNames[i].size()), kNames[i].data());
                return;
            }
            g_params[i] = reinterpret_cast<void* const*>(handle);
        }
        const std::byte* setter = relativeTarget(site + kSetterCall);
        if (!setter || !image.inText(setter) || functionStart(image, setter) != setter) {
            EVR_LOG("%s: the vec4 setter is not a function; not installed", kTag);
            return;
        }
        g_setVec4 = reinterpret_cast<SetVec4Fn>(const_cast<std::byte*>(setter));
        std::string error;
        if (!installMidHook(const_cast<std::byte*>(site + kHookSite), &onViewParams, error)) {
            EVR_LOG("%s: hook failed: %s; SSR keeps the centred view rotation", kTag, error.c_str());
            return;
        }
        g_installed = true;
        EVR_LOG(
            "%s: inverse view matrix parameters hooked at RVA 0x%X: SSR's view rotation follows each view's "
            "projection (setter RVA 0x%X)",
            kTag, image.rva(site + kHookSite), image.rva(setter));
    });
    return g_installed;
}

} // namespace evr::vkcore
