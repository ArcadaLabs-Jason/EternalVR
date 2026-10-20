#include "vkcore/decal_frustum_hooks.hpp"

#include "stereo_seq/decal_frustum.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/stereo_hooks.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-decals";

// The decal gather (RVA 0x1C77C90, a job: gather(params)): params[0] is the view's culling context, params[1]
// the Umbra context (its visible-decal bit set at +0x4B8, 0x7800 bits, one per decal record, filled by the
// query finish, RVA 0x1D1A5A0). With Umbra on (the cull settings, [viewContext + 0x18], byte +0x24) it lists
// the records (0x50 bytes each, at [[viewContext + 0x28] + 0x10]) whose bit is set; with Umbra off every
// active record whose bounds pass the view's frustum test.
constexpr const char* kGatherSignature = "40 57 41 57 B8 48 C0 03 00 E8 ?? ?? ?? ?? 48 2B E0 48 8B 05 ?? ?? "
                                         "?? ?? 48 33 C4 48 89 84 24 30 C0 03 00 "
                                         "4C 8B 51 08 33 FF 48 89 9C 24 68 C0 03 00 49 81 C2 B8 04 00 00 4C "
                                         "89 B4 24 40 C0 03 00 4C 8B F9 4C 8B 31 "
                                         "49 8B 46 28 48 8B 58 10 49 8B 46 18 40 38 78 24 74 57";
// The Umbra-off block (+0xAF): the view at [viewContext + 0x20], records walked from +0x3C in 0x50 steps for
// 0x7800 records, active when the word at +0x38 and the dword at +0x3C are not 0, bounds at +0x20 (`lea rdx,
// [rbx - 0x1C]`) tested against the view-projection at +0x29440.
constexpr std::size_t kOffBlock = 0xAF;
constexpr const char* kOffBlockCode =
    "4D 8B 76 20 48 83 C3 3C 48 89 AC 24 70 C0 03 00 BD 00 78 00 00 48 89 B4 24 "
    "78 C0 03 00 48 8D 74 24 30 66 83 7B FC 00 74 2E 83 3B 00 74 29 48 8D 53 E4 "
    "45 33 C9 4C 8D 44 24 20 49 8D 8E 40 94 02 00 E8 ?? ?? ?? ?? 84 C0 75 0D";
constexpr std::size_t kVisibleDecals = 0x4B8;
constexpr std::size_t kDecalCount = 0x7800;
constexpr std::size_t kRecordSize = 0x50;
constexpr std::size_t kRecordBounds = 0x20;
constexpr std::size_t kRecordActiveWord = 0x38;
constexpr std::size_t kRecordActiveDword = 0x3C;
// renderView_t fields in the view's latched copy.
constexpr std::size_t kFovX = render_view_object::kLatched + 0x28;
constexpr std::size_t kFovY = render_view_object::kLatched + 0x2C;
constexpr std::size_t kOrigin = render_view_object::kLatched + 0x94;
constexpr std::size_t kAxis = render_view_object::kLatched + 0xA0;

using GatherFn = void (*)(std::byte* params, void* arg);

std::once_flag g_once;
bool g_installed = false;
GatherFn g_gather = nullptr;

struct Counters {
    std::atomic<std::uint64_t> pairs{0};     // gathers given the decals around the eye pair
    std::atomic<std::uint64_t> unchanged{0}; // a view already that wide (mono): the engine's list
    std::atomic<std::uint64_t> notEye{0};    // not an eye view the layer wrote: the engine's list
    std::atomic<std::uint64_t> added{0};     // decals added
    std::atomic<std::uint64_t> lastReport{0};
} g_counters;

void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counters.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_counters.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    const std::uint64_t pairs = g_counters.pairs.exchange(0);
    const std::uint64_t added = g_counters.added.exchange(0);
    const std::uint64_t unchanged = g_counters.unchanged.exchange(0);
    const std::uint64_t notEye = g_counters.notEye.exchange(0);
    if (pairs == 0 && added == 0 && unchanged == 0 && notEye == 0) {
        return;
    }
    EVR_LOG("%s: decal gathers: %llu with the decals around the eye pair (%llu added), %llu as the engine's "
            "(%llu already that wide, %llu not an eye view)",
            kTag, static_cast<unsigned long long>(pairs), static_cast<unsigned long long>(added),
            static_cast<unsigned long long>(unchanged + notEye), static_cast<unsigned long long>(unchanged),
            static_cast<unsigned long long>(notEye));
}

template <class T>
T readAt(const std::byte* at) {
    T v{};
    std::memcpy(&v, at, sizeof(v));
    return v;
}

// The eye views the layer wrote last: the idRenderView and the projection terms it was given.
struct EyeViewNote {
    const std::byte* view = nullptr;
    std::array<float, 4> terms{}; // [0] [2] [5] [6]
};
std::mutex g_eyeMutex;
std::array<EyeViewNote, 4> g_eyeViews{};
std::size_t g_eyeNext = 0;

bool isNotedEyeView(const std::byte* view, const stereo_seq::Matrix4& latched) {
    std::lock_guard lock(g_eyeMutex);
    for (const auto& n : g_eyeViews) {
        if (n.view == view) {
            return n.terms == std::array<float, 4>{latched[0], latched[2], latched[5], latched[6]};
        }
    }
    return false;
}

void addPairDecals(std::byte* params) {
    const auto* viewContext = readAt<const std::byte*>(params);
    auto* context = readAt<std::byte*>(params + 8);
    if (!viewContext || !context) {
        return;
    }
    const auto* settings = readAt<const std::byte*>(viewContext + 0x18);
    const auto* view = readAt<const std::byte*>(viewContext + 0x20);
    const auto* manager = readAt<const std::byte*>(viewContext + 0x28);
    if (!settings || settings[0x24] == std::byte{0} || !view || !manager) {
        return; // Umbra off: the engine tests every decal against the view itself
    }
    const auto* records = readAt<const std::byte*>(manager + 0x10);
    if (!records) {
        return;
    }
    stereo_seq::Matrix4 latched{};
    std::memcpy(latched.data(), view + render_view_object::kProjection, sizeof(latched));
    if (!isNotedEyeView(view, latched)) {
        ++g_counters.notEye;
        return;
    }
    const stereo_seq::Matrix4 pair =
        stereo_seq::pairDecalProjection(latched, readAt<float>(view + kFovX), readAt<float>(view + kFovY));
    if (pair == latched) {
        ++g_counters.unchanged;
        return;
    }
    const auto origin = readAt<std::array<float, 3>>(view + kOrigin);
    const auto axis = readAt<std::array<float, 9>>(view + kAxis);
    const auto frustum = stereo_seq::sideFrustum(origin, axis, pair);
    if (!frustum) {
        return;
    }
    ++g_counters.pairs;
    g_counters.added += stereo_seq::addDecals(
        std::span(reinterpret_cast<std::uint64_t*>(context + kVisibleDecals), kDecalCount / 64), kDecalCount,
        [records](std::size_t i) {
            const std::byte* r = records + i * kRecordSize;
            return readAt<std::uint16_t>(r + kRecordActiveWord) != 0 &&
                   readAt<std::uint32_t>(r + kRecordActiveDword) != 0;
        },
        [records, &frustum](std::size_t i) {
            const auto box = readAt<std::array<float, 6>>(records + i * kRecordSize + kRecordBounds);
            return !stereo_seq::boxOutside(*frustum, box.data(), box.data() + 3);
        });
}

void gatherHook(std::byte* params, void* arg) {
    report();
    if (params && mp_guard::allowsGameTouch()) {
        addPairDecals(params);
    }
    g_gather(params, arg);
}

} // namespace

void noteEyeViewProjection(const std::byte* renderView, const float* projection) {
    if (!renderView || !projection) {
        return;
    }
    const std::array<float, 4> terms{projection[0], projection[2], projection[5], projection[6]};
    std::lock_guard lock(g_eyeMutex);
    for (auto& n : g_eyeViews) {
        if (n.view == renderView) {
            n.terms = terms;
            return;
        }
    }
    g_eyeViews[g_eyeNext] = {renderView, terms};
    g_eyeNext = (g_eyeNext + 1) % g_eyeViews.size();
}

bool installDecalFrustumHook() {
    std::call_once(g_once, [] {
        GameImage image;
        if (!locateGameImage(image, kTag)) {
            return;
        }
        const std::byte* gather = findUnique(image, kTag, "decal gather", kGatherSignature);
        if (!gather || functionStart(image, gather) != gather ||
            !matchesAt(image, gather + kOffBlock, kOffBlockCode)) {
            EVR_LOG("%s: the decal gather was not found; not installed", kTag);
            return;
        }
        std::string error;
        if (!installInlineHook(const_cast<std::byte*>(gather), reinterpret_cast<void*>(&gatherHook),
                               reinterpret_cast<void**>(&g_gather), error)) {
            EVR_LOG("%s: hook failed: %s", kTag, error.c_str());
            return;
        }
        g_installed = true;
        EVR_LOG(
            "%s: decal gather (RVA 0x%X) hooked: the eyes' decals are those Umbra reports plus every decal "
            "in the frustum around the eye pair",
            kTag, image.rva(gather));
    });
    return g_installed;
}

} // namespace evr::vkcore
