#include "vkcore/view_clones.hpp"

#include "vkcore/clone_limits.hpp"
#include "vkcore/hook_guard.hpp"
#include "vkcore/log.hpp"
#include "vkcore/view_clone_make.hpp"
#include "vkcore/view_clone_map.hpp"
#include "vkcore/view_clones_impl.hpp"
#include "vkcore/view_slots.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>

namespace evr::vkcore {

namespace view_clone {

const std::byte* g_base = nullptr;
std::byte* g_dc1 = nullptr;             // view 1's device context
std::atomic<const Map*> g_map{nullptr}; // published once built
int g_builds = 0;
bool g_stableNames = false;    // stable names, with the resize hook that purges (prepareViewClones)
std::uint64_t g_builtAt = 0;   // GetTickCount64 at the last build
std::uint64_t g_censusAt = 0;  // the census of the last build is reported from then on (0: reported)
std::byte* g_shadow = nullptr; // the global region's shadow

// View 1's device context for this frame: the engine's, with the clones in place.
void refreshDc1(const Map& map) {
    const std::uintptr_t dc = deviceContext();
    if (!g_dc1 || !dc || map.dcFields.empty()) {
        return;
    }
    std::memcpy(g_dc1, reinterpret_cast<const void*>(dc), kDcSize);
    for (const auto& [offset, value] : map.dcFields) {
        std::memcpy(g_dc1 + offset, &value, sizeof(value));
    }
}

namespace {

using clone_limits::kMaxBuilds; // the build limits (clone_limits.hpp)
using clone_limits::kMaxBuildsNewNames;
using clone_limits::kMaxRemakes;
int g_remakes = 0; // builds after the engine freed a clone
int g_forced = 0;  // builds ETERNALVR_TEST_VIEW_CLONE_REBUILD asked for
std::atomic<bool> g_stopped{false};
std::uint64_t g_nextReport = 0;
bool g_on = false;

// The engine's resize, after its device idle (kResizeIdle): every clone purged, made again at the next build.
void onResizeIdle(const HookRegisters&) {
    if (!g_on || !g_stableNames || g_stopped.load(std::memory_order_relaxed) || !parallelEyesTouch()) {
        return;
    }
    const Map* map = g_map.load(std::memory_order_acquire);
    g_map.store(nullptr, std::memory_order_release); // its images go now
    std::string waiting;
    const int purged = releaseClones(map, waiting);
    EVR_LOG("%s: the engine's resize: %d image clone(s) purged%s%s; made again at the next two-view frame",
            kTag, purged, waiting.empty() ? "" : ", of them waiting: ", waiting.c_str());
}

} // namespace

const std::byte* base() {
    return g_base;
}

const Map* currentMap() {
    return g_map.load(std::memory_order_acquire);
}

} // namespace view_clone

using namespace view_clone;

bool prepareViewClones(const std::byte* gameBase) {
    g_base = gameBase;
    if (!parallelEyesSettings().clones) {
        return true;
    }
    g_shadow = static_cast<std::byte*>(
        VirtualAlloc(nullptr, kRegionEnd - kRegionStart, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!g_shadow) {
        return false;
    }
    if (partOn(parallel_eyes::kDcCopy)) {
        g_dc1 =
            static_cast<std::byte*>(VirtualAlloc(nullptr, kDcSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!g_dc1) {
            return false;
        }
    }
    // `call 0x1C32A80` then `mov sil, 1`; else the clones keep the names of each build, as before, and a
    // build never purges.
    std::int32_t rel = 0;
    std::memcpy(&rel, g_base + kResizeIdleCall + 1, sizeof(rel));
    constexpr std::byte kAfter[] = {std::byte{0x40}, std::byte{0xB6}, std::byte{0x01}};
    const bool idle = g_base[kResizeIdleCall] == std::byte{0xE8} &&
                      kResizeIdleCall + 5 + rel == kDeviceIdle &&
                      std::memcmp(g_base + kResizeIdle, kAfter, sizeof(kAfter)) == 0;
    g_stableNames = !parallelEyesSettings().cloneBuildNames && idle;
    if (!idle) {
        EVR_LOG("%s: RVA 0x%X is not the engine's resize device idle; new names each build, as before", kTag,
                kResizeIdleCall);
    }
    return checkBinds() && prepareViewOnePasses();
}

bool installViewClones() {
    if (!parallelEyesSettings().clones) {
        EVR_LOG("%s: off (ETERNALVR_TEST_VIEW_CLONES=0): view 1 shares the engine's targets", kTag);
        return true;
    }
    if (!installBinds() || !installViewOnePasses() ||
        (g_stableNames && !watchAt(kResizeIdle, &onResizeIdle, "resize device idle"))) {
        return false;
    }
    g_on = true;
    const parallel_eyes::Settings& s = parallelEyesSettings();
    EVR_LOG("%s: on: %zu image slots, %zu target slots, %zu device context fields%s; left shared "
            "(ETERNALVR_TEST_VIEW_CLONE_SKIP): %s; census %s; %s; rebuilds asked for: %s",
            kTag, std::size(kImageSlots), std::size(kTargetSlots),
            std::size(kContextFields) + std::size(kRefractionFields),
            g_dc1 ? ", view 1's device context" : "", parallel_eyes::cloneSkipText(s.cloneSkip).c_str(),
            s.cloneCensus ? "on" : "off (ETERNALVR_TEST_VIEW_CLONE_LOG=0)",
            g_stableNames       ? "stable names, purged at the engine's resize"
            : s.cloneBuildNames ? "new names each build (ETERNALVR_TEST_VIEW_CLONE_NAMES=build)"
                                : "new names each build",
            s.cloneRebuildSeconds ? ("every " + std::to_string(s.cloneRebuildSeconds) + " s, " +
                                     std::to_string(parallel_eyes::kForcedRebuilds) + " times")
                                        .c_str()
                                  : "none");
    return true;
}

namespace {

// viewClonesPrepare's work: makes, remakes or checks the clones.
bool prepareClones() {
    if (!g_on || g_stopped.load(std::memory_order_relaxed)) {
        return false;
    }
    const Map* map = g_map.load(std::memory_order_relaxed);
    const char* why = nullptr;
    if (const Clone* gone = map ? goneClone(*map, why) : nullptr) {
        // Not counted against kMaxBuilds: each map load frees them once.
        EVR_LOG("%s: clone '%s' %p was freed or reused by the engine (%s); making the clones again", kTag,
                gone->name.c_str(), reinterpret_cast<void*>(gone->image), why);
        g_map.store(nullptr, std::memory_order_release);
        if (++g_remakes > kMaxRemakes) {
            g_stopped.store(true);
            EVR_LOG("%s: %d remakes; clones off, view 0 alone from now on", kTag, kMaxRemakes);
            return false;
        }
        return build();
    }
    const parallel_eyes::Settings& settings = parallelEyesSettings();
    if (map && !sourcesChanged()) {
        refreshDc1(*map);
        const std::uint64_t now = GetTickCount64();
        if (settings.cloneRebuildSeconds > 0 && g_forced < parallel_eyes::kForcedRebuilds &&
            now >= g_builtAt + 1000ull * static_cast<std::uint64_t>(settings.cloneRebuildSeconds)) {
            // Not counted against kMaxBuilds.
            EVR_LOG("%s: rebuild %d of %d asked for (ETERNALVR_TEST_VIEW_CLONE_REBUILD=%d)", kTag, ++g_forced,
                    parallel_eyes::kForcedRebuilds, settings.cloneRebuildSeconds);
            return build();
        }
        if (now >= g_nextReport) {
            g_nextReport = now + 60000;
            reportBinds();
            reportPasses();
            if (g_censusAt && now >= g_censusAt) {
                g_censusAt = 0;
                reportCensus(*map, g_builds - 1);
            }
        }
        return false;
    }
    if (g_builds - g_remakes - g_forced >= (g_stableNames ? kMaxBuilds : kMaxBuildsNewNames)) {
        g_stopped.store(true);
        g_map.store(nullptr, std::memory_order_release);
        EVR_LOG("%s: %d builds and the engine's targets still change; clones off, view 0 alone from now on",
                kTag, g_builds);
        return false;
    }
    return build();
}

HookGuard g_prepareGuard("the view clones' frame check (the view dispatcher's detour)");

} // namespace

// On the render thread, in the view dispatcher's detour. A build or report that throws (no memory for the
// clone map) turns the clones off as the build limit does, view 0 alone from now on: how much of a set it
// made is not known.
bool viewClonesPrepare() {
    bool made = false;
    if (g_prepareGuard.run([&] { made = prepareClones(); })) {
        return made;
    }
    g_stopped.store(true);
    g_map.store(nullptr, std::memory_order_release);
    EVR_LOG("%s: the clones could not be made; clones off, view 0 alone from now on", kTag);
    return false;
}

bool viewClonesStopped() {
    return g_stopped.load(std::memory_order_relaxed);
}

void* viewClonesFinalImage() {
    const Map* map = g_map.load(std::memory_order_acquire);
    return map ? reinterpret_cast<void*>(map->finalImage) : nullptr;
}

void* viewClonesScreenImage() {
    const Map* map = g_map.load(std::memory_order_acquire);
    return map ? reinterpret_cast<void*>(map->screenImage) : nullptr;
}

} // namespace evr::vkcore
