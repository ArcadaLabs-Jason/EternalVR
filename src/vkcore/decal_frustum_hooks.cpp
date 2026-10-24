#include "vkcore/decal_frustum_hooks.hpp"

#include "stereo_seq/decal_frustum.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/stereo_hooks.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <string>
#include <vector>

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
// The decal scoring jobs (RVA 0x1CB8950 and 0x1CB8D80), read statically (Steam build; the Game Pass 1.0.56.0
// bodies, RVA 0x1D44EE0 and 0x1D45310, are the same instruction for instruction, their calls shifted with
// them). The engine queues them per view (0x1C5CEA0, which builds their parameter block at +0x522640 of its
// frame context, 0x40 bytes) or calls them inline as `mov rcx, block; call [table]` (0x1C5D0CE, 0x1C5D170):
// one argument. Both read only rcx: the scoring job's rdx, r8 and r9 are written before any read (0x1CB8AD0,
// 0x1CB8AD8, 0x1CB8AE6), as are the sorting job's (0x1CB8D9E, 0x1CB8DF0, 0x1CB8DFC).
// The block: [+8] the idRenderView, [+0x10] the list of record pointers, [+0x18] the keys, [+0x20] the decal
// manager ([+0x10] the records, as in the gather), [+0x28] a pointer to the count; +0x00, +0x30 and +0x38 are
// not read by either. List and keys hold 0x7800 entries each (+0x7B2F8 and +0xB72F8 of the frame context).
// The scoring job reads the view, the list and the manager's tables, writes one key per decal it keeps to
// keys[0..kept) and the kept count; the sorting job sorts keys[0..count) in place (0x1CB8770, 0x1CB9B40,
// 0x1CB75A0: the last a radix sort whose scratch is 1 MB of its own stack frame, never the keys past the
// count), keeps the first 2048 and rewrites list[0..count) from the keys. Nothing past the count of either
// array is read or written. A later stage reads the keys with the same count (a block at +0x6A75D0 of the
// frame context: the manager, the keys, the count), so the decals placed after the engine's list get their
// keys too.
constexpr const char* kScoreSignature =
    "48 8B C4 53 55 56 57 41 56 41 57 48 81 EC E8 00 00 00 0F 29 70 B8 48 8B F9 0F 29 78 A8 44 "
    "0F 29 90 78 FF FF FF 45 0F 57 D2 48 8B 05";
constexpr const char* kSortSignature =
    "40 53 55 41 54 41 57 48 83 EC 38 48 8B 41 20 48 8B E9 4C 8B 79 18 4C 8B 60 10 48 8B 41 28 "
    "4C 63 00 41 81 F8 00 08 00 00 0F 8E";
// The bytes of the job's parameter block the jobs read (+0x08 to +0x2F).
constexpr std::size_t kJobParams = 0x30;
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
using JobFn = void (*)(std::byte* params);

std::once_flag g_once;
bool g_installed = false;
// Both detours in place: until then (and for good when one install failed) both pass straight through.
std::atomic<bool> g_active{false};
GatherFn g_gather = nullptr;
JobFn g_score = nullptr; // the scoring job, called as the engine's
JobFn g_sort = nullptr;  // the sorting job's trampoline (the engine's own)

struct Counters {
    std::atomic<std::uint64_t> pairs{0};     // gathers that chose the decals around the eye pair
    std::atomic<std::uint64_t> unchanged{0}; // a view already that wide (mono): the engine's list
    std::atomic<std::uint64_t> notEye{0};    // not an eye view the layer wrote: the engine's list
    std::atomic<std::uint64_t> sorted{0};    // sorting jobs that had decals chosen for their view
    std::atomic<std::uint64_t> kept{0};      // decals the engine kept for those views, summed
    std::atomic<std::uint64_t> scored{0};    // chosen decals the engine's scoring kept, summed
    std::atomic<std::uint64_t> appended{0};  // chosen decals that followed the engine's list, summed
    std::atomic<std::uint64_t> bound{0};     // views whose scored extras did not all fit
    std::atomic<std::uint64_t> displaced{0}; // decals of the engine's list that gave way, summed
    std::atomic<std::uint64_t> full{0}; // views with no room at all (the engine's list alone is at the limit)
    std::atomic<std::uint64_t> lastReport{0};
} g_counters;

void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counters.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_counters.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    const std::uint64_t pairs = g_counters.pairs.exchange(0);
    const std::uint64_t unchanged = g_counters.unchanged.exchange(0);
    const std::uint64_t notEye = g_counters.notEye.exchange(0);
    const std::uint64_t sorted = g_counters.sorted.exchange(0);
    const std::uint64_t kept = g_counters.kept.exchange(0);
    const std::uint64_t scored = g_counters.scored.exchange(0);
    const std::uint64_t appended = g_counters.appended.exchange(0);
    const std::uint64_t bound = g_counters.bound.exchange(0);
    const std::uint64_t full = g_counters.full.exchange(0);
    const std::uint64_t displaced = g_counters.displaced.exchange(0);
    if (pairs == 0 && unchanged == 0 && notEye == 0) {
        return;
    }
    EVR_LOG("%s: decal gathers: %llu with the decals around the eye pair, %llu as the engine's (%llu already "
            "that wide, %llu not an eye view)",
            kTag, static_cast<unsigned long long>(pairs), static_cast<unsigned long long>(unchanged + notEye),
            static_cast<unsigned long long>(unchanged), static_cast<unsigned long long>(notEye));
    if (sorted > 0) {
        EVR_LOG("%s: decal list after the engine's scoring (limit %zu): %llu eye views, the engine kept %llu "
                "a view, "
                "%llu chosen decals scored a view, %llu appended a view, %llu of the engine's gave way a "
                "view; the limit bound in "
                "%llu views (%llu with no room under it)",
                kTag, stereo_seq::kViewDecalSafeCount, static_cast<unsigned long long>(sorted),
                static_cast<unsigned long long>(kept / sorted),
                static_cast<unsigned long long>(scored / sorted),
                static_cast<unsigned long long>(appended / sorted),
                static_cast<unsigned long long>(displaced / sorted), static_cast<unsigned long long>(bound),
                static_cast<unsigned long long>(full));
    }
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

// The decals chosen for an eye view by its gather, waiting for that view's sorting job. Every gather drops
// what its view had waiting first, so a set is only ever used by the sorting job of the gather that chose it
// (the sorting job sorts the list that gather listed); a view whose gather chose nothing (not an eye view any
// more, a freed view's address reused) gets nothing. The oldest set gives way when all slots are taken.
struct Chosen {
    const std::byte* view = nullptr;
    std::uint64_t sequence = 0;
    std::vector<std::uint16_t> indices;
};
std::mutex g_chosenMutex;
std::array<Chosen, 8> g_chosen{};
std::uint64_t g_chosenSequence = 0;

void dropChosen(const std::byte* view) {
    std::lock_guard lock(g_chosenMutex);
    for (auto& c : g_chosen) {
        if (c.view == view) {
            c.view = nullptr;
            c.indices.clear();
        }
    }
}

void storeChosen(const std::byte* view, std::vector<std::uint16_t>&& indices) {
    std::lock_guard lock(g_chosenMutex);
    // The view's own slot, else a free one, else the oldest.
    Chosen* slot = nullptr;
    for (auto& c : g_chosen) {
        if (c.view == view) {
            slot = &c;
            break;
        }
    }
    for (auto& c : g_chosen) {
        if (!slot && !c.view) {
            slot = &c;
        }
    }
    if (!slot) {
        slot = &g_chosen[0];
        for (auto& c : g_chosen) {
            if (c.sequence < slot->sequence) {
                slot = &c;
            }
        }
    }
    slot->view = view;
    slot->sequence = ++g_chosenSequence;
    slot->indices = std::move(indices);
}

std::vector<std::uint16_t> takeChosen(const std::byte* view) {
    std::lock_guard lock(g_chosenMutex);
    for (auto& c : g_chosen) {
        if (c.view == view) {
            c.view = nullptr;
            return std::move(c.indices);
        }
    }
    return {};
}

// The gather of an eye view: chooses the decals around the eye pair that the engine's list does not hold, the
// nearest first, and leaves them for the view's scoring. Nothing is added to the engine's list here.
void choosePairDecals(std::byte* params) {
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
    storeChosen(
        view,
        stereo_seq::nearestDecals(
            std::span(reinterpret_cast<const std::uint64_t*>(context + kVisibleDecals), kDecalCount / 64),
            kDecalCount, stereo_seq::kPairCandidates,
            [records](std::size_t i) {
                const std::byte* r = records + i * kRecordSize;
                return readAt<std::uint16_t>(r + kRecordActiveWord) != 0 &&
                       readAt<std::uint32_t>(r + kRecordActiveDword) != 0;
            },
            [records, &frustum](std::size_t i) {
                const auto box = readAt<std::array<float, 6>>(records + i * kRecordSize + kRecordBounds);
                return !stereo_seq::boxOutside(*frustum, box.data(), box.data() + 3);
            },
            [records, &origin](std::size_t i) {
                return stereo_seq::boxDistance2(
                    readAt<std::array<float, 6>>(records + i * kRecordSize + kRecordBounds), origin);
            }));
}

void gatherHook(std::byte* params, void* arg) {
    report();
    if (params && g_active.load(std::memory_order_acquire)) {
        if (const auto* viewContext = readAt<const std::byte*>(params)) {
            if (const auto* view = readAt<const std::byte*>(viewContext + 0x20)) {
                dropChosen(view);
            }
        }
        if (mp_guard::allowsGameTouch()) {
            choosePairDecals(params);
        }
    }
    g_gather(params, arg);
}

// The sorting job of a view: after the engine has kept and ranked its decals, the view's chosen decals are
// scored and ranked by the engine's own jobs on a copy of the job's parameters (their own list, keys and
// count), and placed after the engine's list with their keys (stereo_seq::placeExtras: under the limit, the
// engine's last-ranked decals giving way only to make the minimum room).
void appendChosen(std::byte* params) {
    const auto* view = readAt<const std::byte*>(params + 8);
    const std::vector<std::uint16_t> chosen = takeChosen(view);
    if (chosen.empty() || !view) {
        return;
    }
    stereo_seq::Matrix4 latched{};
    std::memcpy(latched.data(), view + render_view_object::kProjection, sizeof(latched));
    if (!isNotedEyeView(view, latched)) {
        return; // no longer the eye view its gather saw
    }
    int* countPtr = readAt<int*>(params + 0x28);
    auto** list = readAt<std::byte**>(params + 0x10);
    auto* engineKeys = readAt<std::uint64_t*>(params + 0x18);
    const auto* manager = readAt<const std::byte*>(params + 0x20);
    auto* base = manager ? readAt<std::byte*>(manager + 0x10) : nullptr;
    if (!countPtr || !list || !engineKeys || !base || *countPtr < 0 ||
        static_cast<std::size_t>(*countPtr) > kDecalCount) {
        return;
    }
    const std::size_t kept = static_cast<std::size_t>(*countPtr);
    ++g_counters.sorted;
    g_counters.kept += kept;
    // Those the engine's list already holds (other routes list some decals too) are not listed twice.
    thread_local std::array<std::uint64_t, kDecalCount / 64> held{};
    thread_local std::vector<std::uint16_t> extras;
    thread_local std::vector<std::byte*> pointers;
    thread_local std::vector<std::uint64_t> keys;
    stereo_seq::markHeld(held, std::span<std::byte* const>(list, kept), base, kRecordSize);
    stereo_seq::withoutHeld(chosen, held, extras);
    if (extras.empty()) {
        return;
    }
    pointers.clear();
    for (const std::uint16_t index : extras) {
        pointers.push_back(base + index * kRecordSize);
    }
    keys.assign(pointers.size(), 0);
    // The job's parameters with the chosen decals' list, keys and count in place of the view's.
    alignas(16) void* job[kJobParams / sizeof(void*)];
    std::memcpy(job, params, sizeof(job));
    int count = static_cast<int>(pointers.size());
    job[2] = pointers.data();
    job[3] = keys.data();
    job[5] = &count;
    g_score(reinterpret_cast<std::byte*>(job));
    g_sort(reinterpret_cast<std::byte*>(job));
    const std::size_t scored = std::min(static_cast<std::size_t>(std::max(count, 0)), keys.size());
    const stereo_seq::Placement place = stereo_seq::placeExtras(kept, scored);
    const auto origin = readAt<std::array<float, 3>>(view + kOrigin);
    stereo_seq::keepNearestKeys(
        std::span(keys.data(), scored), place.take, [base, &origin](std::size_t record) {
            return stereo_seq::boxDistance2(
                readAt<std::array<float, 6>>(base + record * kRecordSize + kRecordBounds), origin);
        });
    const std::size_t total =
        stereo_seq::writeExtras(std::span(list, kDecalCount), std::span(engineKeys, kDecalCount), place,
                                std::span(keys.data(), scored), base, kRecordSize);
    *countPtr = static_cast<int>(total);
    g_counters.scored += scored;
    g_counters.appended += total - place.keep;
    g_counters.displaced += kept - place.keep;
    if (scored > total - place.keep) {
        ++g_counters.bound;
    }
    if (stereo_seq::extraRoom(kept) == 0) {
        ++g_counters.full;
    }
}

void sortHook(std::byte* params) {
    g_sort(params);
    if (params && g_active.load(std::memory_order_acquire) && mp_guard::allowsGameTouch()) {
        appendChosen(params);
    }
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
        const std::byte* score = findUnique(image, kTag, "decal scoring job", kScoreSignature);
        const std::byte* sort = findUnique(image, kTag, "decal sorting job", kSortSignature);
        if (!score || !sort || functionStart(image, score) != score || functionStart(image, sort) != sort) {
            EVR_LOG("%s: the decal scoring jobs were not found; not installed", kTag);
            return;
        }
        std::string error;
        g_score = reinterpret_cast<JobFn>(const_cast<std::byte*>(score));
        if (!installInlineHook(const_cast<std::byte*>(sort), reinterpret_cast<void*>(&sortHook),
                               reinterpret_cast<void**>(&g_sort), error)) {
            EVR_LOG("%s: sorting job hook failed: %s", kTag, error.c_str());
            return;
        }
        if (!installInlineHook(const_cast<std::byte*>(gather), reinterpret_cast<void*>(&gatherHook),
                               reinterpret_cast<void**>(&g_gather), error)) {
            EVR_LOG("%s: hook failed: %s; the sorting job's detour passes through", kTag, error.c_str());
            return;
        }
        g_installed = true;
        g_active.store(true, std::memory_order_release);
        EVR_LOG("%s: decal gather (RVA 0x%X) and sorting job (RVA 0x%X) hooked: the decals around the eye "
                "pair that "
                "the engine's list lacks follow it after the engine's own scoring (scoring job RVA 0x%X)",
                kTag, image.rva(gather), image.rva(sort), image.rva(score));
    });
    return g_installed;
}

} // namespace evr::vkcore
