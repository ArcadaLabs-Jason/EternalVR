#include "vkcore/view_shadow_cache.hpp"

#include "vkcore/job_nodes.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/view_slots.hpp"

#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "view-redirects";

constexpr std::size_t kLightBlock = 0x5226F8; // the view's light block = render context + this

// The functions hooked, with their first bytes.
struct Function {
    std::uint32_t rva;
    std::uint8_t bytes[8];
};
constexpr Function kSetup{0x1CF0240, {0x48, 0x89, 0x4C, 0x24, 0x08, 0x53, 0x55, 0x56}}; // (light block)
constexpr Function kBegin{0x1D058B0, {0x40, 0x53, 0x57, 0x41, 0x54, 0x41, 0x57, 0x48}}; // (cache)
constexpr Function kReset{0x1D05A30, {0xC7, 0x41, 0x28, 0xFF, 0xFF, 0x00, 0x00, 0xC3}}; // (cache)
constexpr Function kLookup{0x1D05A40,
                           {0x40, 0x57, 0x44, 0x8B, 0x99, 0xA0, 0x00, 0x00}}; // (cache, key, touch)
constexpr Function kRelease{0x1D05BF0, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C}}; // (cache, entry)
// In the setup: `call begin`, `call reset`, then `inc qword ptr [r13 + 0x30]` (the
// cache's frame count) and `xor r12d, r12d`.
constexpr std::uint32_t kBeginCall = 0x1CF02AC;
constexpr std::uint32_t kResetCall = 0x1CF02B4;
constexpr std::uint32_t kFrameCount = 0x1CF02B9;
constexpr std::uint8_t kFrameCountBytes[] = {0x49, 0xFF, 0x45, 0x30, 0x45, 0x33, 0xE4};
constexpr std::uint32_t kAfterFrameCount = 0x1CF02C0;
// A cache key (0x1D05F20): (level + device context index * 8) << 21, plus the face
// and the light's index * 12; levels 0 to 4 by size, 5 for the static cache.
constexpr std::uint32_t kLevelShift = 21;
constexpr std::uint32_t kLevelMask = 7u << kLevelShift;
constexpr std::uint32_t kLevels = 5;

// The setup's light records (0x38 bytes each, made by 0x1CFE2B0) and the field of
// the light list the shading reads. Each view's light parameter build (0x1CF0B80,
// after its setup) finds a face's shadow map in the cache itself, from the level
// byte the setup wrote for the face into the light's entry in the light list
// (+0x6A + face, at 0x1CFEC27; the list is one array for both views): it looks up
// that level and the coarser ones (0x1CF0F0E..0x1CF0F40), and with none found
// shades from a dummy entry (0x39AE640, a zero rect). It does not read the records.
// Where view 1 took view 0's entry at a finer level than the one it picked, its
// shading would miss it: the face's level byte is set to the level of the entry the
// record holds.
constexpr std::size_t kRecords = 0x184E68;     // light block: the records
constexpr std::size_t kRecordCount = 0x184E70; // light block: how many the setup kept (int32)
constexpr std::size_t kRecordSize = 0x38;
constexpr std::size_t kRecordEntry = 0x10; // the cache entry (the static one is at +0x18)
constexpr std::size_t kRecordLight = 0x20; // the light's entry in the light list
constexpr std::size_t kRecordSlot = 0x28;  // & 0x7F: face * 2, odd for the static slot
constexpr std::size_t kFaceLevels = 0x6A;  // light list entry: a level byte per face
constexpr std::size_t kEntryLevel = 0x84;  // cache entry: its level
constexpr std::uint32_t kFaces = 6;

// The releases view 1 leaves out, by call site: the setup's own release of a
// light's entry at a level other than the one it picked (0x1CF063F), and those of
// its per-light record function 0x1CFE2B0 (called at 0x1CF03EA): every level of a
// light that changed (0x1CFF16A), the levels the light no longer uses (0x1CFF213),
// its static entry to be made again (0x1CFF34F). Any other release goes through,
// the allocation's own above all: when the atlas has no room, 0x1D05220 reuses an
// older entry no light used this frame, or releases one (at 0x1D053D8, or 0x1D04F83
// below 0x1D05520) and allocates again; with that release left out it would find
// the same entry again and recurse until the stack ran out.
constexpr std::uint32_t kSetupReleases[] = {0x1CF063F, 0x1CFF16A, 0x1CFF213, 0x1CFF34F};
constexpr std::uint32_t kEvictions[] = {0x1D053D8, 0x1D04F83};
constexpr std::uint32_t kCallSize = 5;

// The record function's redraw request for a face's static entry (0x1CFF2B8): the
// entry's request count (+0x88) goes up by one and its redraw byte (+0x86) is set;
// over the level's limit the entry is released (0x1CFF34F) and the record's entry
// cleared (r13 = 0 at 0x1CFF354), so the setup allocates a new one (0x1CF0714). Only
// view 0's render clears the count and the light's moved-caster bits, and view 1's
// setup runs between view 0's and that render: view 1 asked again, crossed the limit,
// and its allocation re-pointed the key to an entry no view renders, which view 0's
// shading then read (the gun's shadow flickered in eye L). View 1 makes the request
// but never takes the release branch (0x1CFF347, `mov rcx, [rsp+0x78]`; only the `ja`
// at 0x1CFF340 jumps there): it keeps view 0's entry and allocates nothing (resumes at
// 0x1CFF357, `test bl, 6`; r9 and r15 are set again after it). Skipping the request
// as well also stopped the flicker but left eye R about 12% darker than Route S (eye-1
// captures, 2026-10-03). ETERNALVR_TEST_PE_SHADOW_REFRESH=0 keeps the game's branch,
// =1 skips the request too (both for A/B runs).
constexpr std::uint32_t kRedrawRequest = 0x1CFF2B8;
constexpr std::uint8_t kRedrawRequestBytes[] = {0x41, 0x8B, 0x95, 0x88, 0x00, 0x00, 0x00, 0xFF, 0xC2,
                                                0x41, 0xC6, 0x85, 0x86, 0x00, 0x00, 0x00, 0x01};
constexpr std::uint32_t kReleaseBranch = 0x1CFF347;
constexpr std::uint8_t kReleaseBranchBytes[] = {0x48, 0x8B, 0x4C, 0x24, 0x78, 0x49, 0x8B, 0xD5};
constexpr std::uint32_t kAfterRedrawRequest = 0x1CFF357;
constexpr std::uint8_t kAfterRedrawRequestBytes[] = {0xF6, 0xC3, 0x06};

using SetupFn = void (*)(void* lightBlock);
using CacheFn = void* (*)(void* cache, void* a, void* b, void* c);
using LookupFn = void* (*)(void* cache, std::uint32_t key, std::uint8_t touch);

std::uintptr_t g_base = 0;
SetupFn g_setup = nullptr;
CacheFn g_begin = nullptr;
CacheFn g_reset = nullptr;
CacheFn g_release = nullptr;
LookupFn g_lookup = nullptr;

// Set while view 1's setup runs on this thread.
thread_local bool t_view1 = false;

// ---- The two views' setups in turn ----
// Both setups change the cache's lists without a lock, and view 1's takes the
// entries view 0's made this frame. View 1's waits for view 0's to finish (at most
// 2 ms; 16 waits in a row that run out stop the waiting for the session), and both
// run under one lock (recursive: a job system that runs another job on a waiting
// thread may start one setup inside the other). Only while view 0 is dispatched
// this frame (ETERNALVR_TEST_VIEW_ONLY=1 renders view 1 alone): otherwise view 1's
// setup is the game's own.
std::atomic<bool> g_view0Dispatched{false};
std::atomic<bool> g_view0Done{false};
std::recursive_mutex g_setupMutex;
job_nodes::BoundedWait* g_wait = nullptr;

std::int64_t ticks() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

std::atomic<std::uint64_t> g_setups{0};      // view 1's setups run on view 0's entries
std::atomic<std::uint64_t> g_order[4]{};     // view 0's done when view 1's began, waited for, ran out, off
std::atomic<std::uint64_t> g_leftOut{0};     // releases left out
std::atomic<std::uint64_t> g_evictions[2]{}; // older entries view 1's allocations released, by site
std::atomic<std::uint64_t> g_otherLevel{0};  // lookups that found the light at another level
std::atomic<std::uint64_t> g_faceLevels{0};  // faces whose level byte took the level of view 1's entry
std::atomic<std::uint64_t> g_requests{0}; // view 1's redraw requests skipped (=1) or release branches skipped
// How view 1 takes a static entry's redraw request (ETERNALVR_TEST_PE_SHADOW_REFRESH).
enum class Refresh { Game, SkipRequest, KeepEntry };
Refresh g_refresh = Refresh::KeepEntry;

template <typename T>
T read(const std::byte* at) {
    T value{};
    std::memcpy(&value, at, sizeof(value));
    return value;
}

void faceLevelsFromEntries(const std::byte* lightBlock) {
    const auto* records = read<const std::byte*>(lightBlock + kRecords);
    const auto count = read<std::int32_t>(lightBlock + kRecordCount);
    for (std::int32_t i = 0; records && i < count; ++i) {
        const std::byte* record = records + static_cast<std::size_t>(i) * kRecordSize;
        const auto slot = std::to_integer<std::uint32_t>(record[kRecordSlot]) & 0x7F;
        const auto* entry = read<const std::byte*>(record + kRecordEntry);
        auto* light = read<std::byte*>(record + kRecordLight);
        if ((slot & 1) || slot / 2 >= kFaces || !entry || !light) {
            continue;
        }
        const std::byte level = entry[kEntryLevel];
        std::byte& face = light[kFaceLevels + slot / 2];
        if (std::to_integer<std::uint32_t>(level) < kLevels && face != level) {
            face = level;
            g_faceLevels.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void setup(void* lightBlock) {
    const auto context1 = reinterpret_cast<std::uintptr_t>(viewSlotsContext1());
    const bool view1 = context1 && reinterpret_cast<std::uintptr_t>(lightBlock) - kLightBlock == context1;
    if (!parallelEyesTouch() || !g_view0Dispatched.load(std::memory_order_acquire)) {
        g_setup(lightBlock);
        return;
    }
    if (view1) {
        using Result = job_nodes::BoundedWait::Result;
        const Result result = g_wait->wait([] { return g_view0Done.load(std::memory_order_acquire); }, ticks,
                                           [] { YieldProcessor(); });
        g_order[static_cast<int>(result)].fetch_add(1, std::memory_order_relaxed);
        if (result == Result::TimedOut && g_wait->off()) {
            EVR_LOG("%s: view 0's shadow setup did not finish within 2 ms of view "
                    "1's in 16 frames in a row; "
                    "view 1's no longer waits for it (the two still take turns)",
                    kTag);
        }
        g_setups.fetch_add(1, std::memory_order_relaxed);
    }
    std::lock_guard lock(g_setupMutex);
    const bool saved = t_view1;
    t_view1 = view1;
    g_setup(lightBlock);
    t_view1 = saved;
    if (view1) {
        faceLevelsFromEntries(static_cast<const std::byte*>(lightBlock));
    } else {
        g_view0Done.store(true, std::memory_order_release);
    }
}

void* begin(void* cache, void* a, void* b, void* c) {
    return t_view1 ? nullptr : g_begin(cache, a, b, c);
}

void* reset(void* cache, void* a, void* b, void* c) {
    return t_view1 ? nullptr : g_reset(cache, a, b, c);
}

template <std::size_t N>
bool calledFrom(std::uintptr_t returnRva, const std::uint32_t (&sites)[N]) {
    for (const std::uint32_t rva : sites) {
        if (returnRva == rva + kCallSize) {
            return true;
        }
    }
    return false;
}

void* release(void* cache, void* a, void* b, void* c) {
    if (t_view1) {
        // The detour is jumped to from the function's first bytes: the return
        // address is the caller's.
        const std::uintptr_t from = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - g_base;
        if (calledFrom(from, kSetupReleases)) {
            g_leftOut.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }
        for (std::size_t i = 0; i < std::size(kEvictions); ++i) {
            if (from == kEvictions[i] + kCallSize) {
                g_evictions[i].fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    return g_release(cache, a, b, c);
}

// View 1 misses where it picked another level than view 0: the nearest level view 0
// has is taken.
void* lookup(void* cache, std::uint32_t key, std::uint8_t touch) {
    void* entry = g_lookup(cache, key, touch);
    const std::uint32_t level = (key & kLevelMask) >> kLevelShift;
    if (entry || !t_view1 || level >= kLevels) {
        return entry;
    }
    for (std::uint32_t d = 1; d < kLevels; ++d) {
        const std::uint32_t candidates[] = {level - d, level + d}; // a level below 0 wraps past kLevels
        for (const std::uint32_t l : candidates) {
            if (l >= kLevels) {
                continue;
            }
            entry = g_lookup(cache, (key & ~kLevelMask) | (l << kLevelShift), touch);
            if (entry) {
                g_otherLevel.fetch_add(1, std::memory_order_relaxed);
                return entry;
            }
        }
    }
    return nullptr;
}

// The cache's frame count is not counted for view 1 (the `xor r12d, r12d` inside
// the hook's bytes is done here).
void onFrameCount(HookRegisters& r) {
    if (t_view1) {
        r.r12 = 0;
        r.resumeAt = g_base + kAfterFrameCount;
    }
}

// Refresh::SkipRequest (test): view 1 makes no request.
void onRedrawRequest(HookRegisters& r) {
    if (t_view1) {
        g_requests.fetch_add(1, std::memory_order_relaxed);
        r.resumeAt = g_base + kAfterRedrawRequest;
    }
}

// Refresh::KeepEntry: view 1 keeps its entry over the request limit.
void onReleaseBranch(HookRegisters& r) {
    if (t_view1) {
        g_requests.fetch_add(1, std::memory_order_relaxed);
        r.resumeAt = g_base + kAfterRedrawRequest;
    }
}

const std::byte* callTarget(const std::byte* base, std::uint32_t rva) {
    if (base[rva] != std::byte{0xE8}) {
        return nullptr;
    }
    std::int32_t rel = 0;
    std::memcpy(&rel, base + rva + 1, sizeof(rel));
    return base + rva + kCallSize + rel;
}

template <std::size_t N>
bool releaseCalls(const std::byte* base, const std::uint32_t (&sites)[N]) {
    for (const std::uint32_t rva : sites) {
        if (callTarget(base, rva) != base + kRelease.rva) {
            EVR_LOG("%s: RVA 0x%X is not a shadow cache release call; not changed", kTag, rva);
            return false;
        }
    }
    return true;
}

} // namespace

bool prepareViewShadowCache(const std::byte* base) {
    for (const Function& f : {kSetup, kBegin, kReset, kLookup, kRelease}) {
        if (std::memcmp(base + f.rva, f.bytes, sizeof(f.bytes)) != 0) {
            EVR_LOG("%s: RVA 0x%X is not the shadow cache function known; not changed", kTag, f.rva);
            return false;
        }
    }
    if (callTarget(base, kBeginCall) != base + kBegin.rva ||
        callTarget(base, kResetCall) != base + kReset.rva ||
        std::memcmp(base + kFrameCount, kFrameCountBytes, sizeof(kFrameCountBytes)) != 0 ||
        std::memcmp(base + kRedrawRequest, kRedrawRequestBytes, sizeof(kRedrawRequestBytes)) != 0 ||
        std::memcmp(base + kReleaseBranch, kReleaseBranchBytes, sizeof(kReleaseBranchBytes)) != 0 ||
        std::memcmp(base + kAfterRedrawRequest, kAfterRedrawRequestBytes, sizeof(kAfterRedrawRequestBytes)) !=
            0) {
        EVR_LOG("%s: the shadow setup's cache begin is not as known (RVA 0x%X); "
                "not changed",
                kTag, kBeginCall);
        return false;
    }
    return releaseCalls(base, kSetupReleases) && releaseCalls(base, kEvictions);
}

bool installViewShadowCache(const std::byte* base) {
    g_base = reinterpret_cast<std::uintptr_t>(base);
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    static job_nodes::BoundedWait wait(frequency.QuadPart / 500, 16); // 2 ms
    g_wait = &wait;
    const struct {
        std::uint32_t rva;
        void* hook;
        void** original;
    } hooks[] = {
        {kSetup.rva, reinterpret_cast<void*>(&setup), reinterpret_cast<void**>(&g_setup)},
        {kBegin.rva, reinterpret_cast<void*>(&begin), reinterpret_cast<void**>(&g_begin)},
        {kReset.rva, reinterpret_cast<void*>(&reset), reinterpret_cast<void**>(&g_reset)},
        {kLookup.rva, reinterpret_cast<void*>(&lookup), reinterpret_cast<void**>(&g_lookup)},
        {kRelease.rva, reinterpret_cast<void*>(&release), reinterpret_cast<void**>(&g_release)},
    };
    for (const auto& h : hooks) {
        std::string error;
        if (!installInlineHook(const_cast<std::byte*>(base + h.rva), h.hook, h.original, error)) {
            EVR_LOG("%s: shadow cache hook at RVA 0x%X failed: %s", kTag, h.rva, error.c_str());
            return false;
        }
    }
    std::string error;
    if (!installMidHookEdit(const_cast<std::byte*>(base + kFrameCount), &onFrameCount, error)) {
        EVR_LOG("%s: shadow cache frame count hook at RVA 0x%X failed: %s", kTag, kFrameCount, error.c_str());
        return false;
    }
    std::wstring mode;
    if (readEnv(L"ETERNALVR_TEST_PE_SHADOW_REFRESH", mode)) {
        g_refresh = mode == L"0" ? Refresh::Game : mode == L"1" ? Refresh::SkipRequest : Refresh::KeepEntry;
        EVR_LOG("%s: test: view 1's static entry redraw requests: %s (ETERNALVR_TEST_PE_SHADOW_REFRESH=%ls)",
                kTag,
                g_refresh == Refresh::Game          ? "as the game's (release branch kept)"
                : g_refresh == Refresh::SkipRequest ? "skipped"
                                                    : "made, release branch skipped",
                mode.c_str());
    }
    if (g_refresh == Refresh::Game) {
        return true;
    }
    const std::uint32_t at = g_refresh == Refresh::SkipRequest ? kRedrawRequest : kReleaseBranch;
    if (!installMidHookEdit(const_cast<std::byte*>(base + at),
                            g_refresh == Refresh::SkipRequest ? &onRedrawRequest : &onReleaseBranch, error)) {
        EVR_LOG("%s: shadow cache redraw request hook at RVA 0x%X failed: %s", kTag, at, error.c_str());
        return false;
    }
    return true;
}

void viewShadowCacheFrameStart(bool view0Dispatched) {
    g_view0Done.store(false, std::memory_order_relaxed);
    g_view0Dispatched.store(view0Dispatched, std::memory_order_release);
}

void viewShadowCacheLogCounts() {
    EVR_LOG("%s: view 1's shadow setup on view 0's shadow cache entries %llu "
            "time(s): view 0's had finished "
            "%llu, waited for %llu, ran out %llu, did not wait %llu; %llu "
            "release(s) left out, older entries "
            "released for view 1's allocations %llu + %llu, %llu light(s) found at "
            "another level, %llu face "
            "level(s) set to the entry's, %llu static entry %s of view 1's",
            kTag, static_cast<unsigned long long>(g_setups.load()),
            static_cast<unsigned long long>(g_order[0].load()),
            static_cast<unsigned long long>(g_order[1].load()),
            static_cast<unsigned long long>(g_order[2].load()),
            static_cast<unsigned long long>(g_order[3].load()),
            static_cast<unsigned long long>(g_leftOut.load()),
            static_cast<unsigned long long>(g_evictions[0].load()),
            static_cast<unsigned long long>(g_evictions[1].load()),
            static_cast<unsigned long long>(g_otherLevel.load()),
            static_cast<unsigned long long>(g_faceLevels.load()),
            static_cast<unsigned long long>(g_requests.load()),
            g_refresh == Refresh::SkipRequest ? "redraw request(s) skipped" : "release branch(es) skipped");
}

} // namespace evr::vkcore
