#include "vkcore/shared_sun_hooks.hpp"

#include "stereo_seq/shared_sun.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/view_slots.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-sun";

// The shadow setup's check of a dynamic slice that has a cache entry (0x1CFE2B0; signature at RVA 0x1CFF2B8).
// rdi is the light, r13 the face's cache entry, r12d the slice (two per face or cascade: even the world's
// casters, odd the player's), bl the slice's flags (2 dynamic, 4 static part to build), [rsp + 0x48] the
// light type. The entry's counter is raised first (`mov edx, [r13 + 0x88]`, `inc edx`, `mov [r13 + 0x88],
// edx`, +0x0 to +0x10); the stale threshold goes to eax from one of four paths (the light's own rate,
// r_skipShadowThrottle, the player's r_shadowPlayerMaxStaleFrames, or the parsed stale tables: image base
// `lea rcx` at +0x50, the cascade table r_shadowParallelMaxStaleFrames at +0x62, the mip table at +0x71).
// They join at `cmp edx, eax` (+0x86, the first hook); `ja` (+0x88) releases the entry (+0x8F,
// `mov rcx, [rsp + 0x78]`, `mov rdx, r13`, `call`), and `test bl, 6` (+0x9F, the second hook, after
// `mov r13, r15` at +0x9C) puts the slice on the view's render list. A slice without a face entry jumps
// straight to the listing (`test r13, r13`, `je` at 0x1CFF296).
constexpr const char* kCheckSignature = "41 8B 95 88 00 00 00 FF C2 41 C6 85 86 00 00 00 01 41 89 95 88 00 "
                                        "00 00 8B 8F 8C 00 00 00 85 C9 75 58 48 "
                                        "8B 05 ?? ?? ?? ?? 39 48 08 74 08 45 33 FF 41 8B C7 EB 50 41 F6 C4 "
                                        "01 74 0F 48 8B 05 ?? ?? ?? ?? 45 33 FF "
                                        "8B 40 08 EB 3B 83 7C 24 48 02 48 8D 0D ?? ?? ?? ?? 75 12 41 8B C4 "
                                        "48 D1 E8 45 33 FF 8B 84 81 ?? ?? ?? ?? "
                                        "EB 1B 41 8B C6 45 33 FF 8B 84 81 ?? ?? ?? ?? EB 0C 45 33 FF 83 F9 "
                                        "01 41 8B C7 0F 94 C0 3B D0 77 05 F6 C3 "
                                        "04 74 10 48 8B 4C 24 78 49 8B D5 E8";
constexpr std::size_t kImageBaseLea = 0x50; // lea rcx, [rip + disp] (7 bytes): the module base
constexpr std::size_t kCheckHook = 0x86;
constexpr std::size_t kListing = 0x9C;
constexpr std::size_t kListingHook = 0x9F;
constexpr const char* kListingSignature = "4D 8B EF F6 C3 06 0F 84";
constexpr std::size_t kLightTypeFromRsp = 0x48;
constexpr std::size_t kEntryCounter = 0x88;
constexpr std::uintptr_t kFlagsListed = 6; // `test bl, 6`
constexpr std::int32_t kSpotLight = 1;
constexpr std::uint32_t kCascades = stereo_seq::kSunCascades;

using stereo_seq::Eye;
using stereo_seq::SharedSunMode;

std::once_flag g_once;
bool g_installed = false;
std::atomic<int> g_owner{0}; // ShadowCheckOwner
SharedSunMode g_mode = SharedSunMode::Off;

// What a listed slice shadows: the sun's world casters, a spot light's, a point light's, or the player's
// model (an odd slice of any light).
enum Kind : int { kSun, kSpot, kPoint, kPlayer, kKinds };

struct Counters {
    // By eye (mono or untagged, eye L, eye R).
    std::atomic<std::uint64_t> listed[3][kKinds]{};
    std::atomic<std::uint64_t> sunByCascade[3][kCascades]{};
    // Sun world slices listed without an entry (the stale check released it).
    std::atomic<std::uint64_t> sunNoEntry[3]{};
    // Eye R's sun world slices at the stale check: all, those eye L listed in the same tick with the counter
    // at 1 (what Shared keeps), and those whose entry eye R kept (Shared, Ceiling) by cascade; Ceiling: those
    // left off at the listing.
    std::atomic<std::uint64_t> checkedRight{0};
    std::atomic<std::uint64_t> freshRight{0};
    std::atomic<std::uint64_t> kept[kCascades]{};
    std::atomic<std::uint64_t> leftOff{0};
    std::atomic<std::uint64_t> lastReport{0};
    std::atomic<std::uint64_t> lastTicks{0};
} g_counters;

// The eye and game frame of the running render: its tag, found by the backend counter the job sees, looked up
// on every call (a short peek under the tag book's lock). Not cached per thread: a counter that starts again
// (a map load) or a tag book that resynchronises (mono until the next base) would hand an old tag back.
void tagNow(Eye& eye, std::uint64_t& tick) {
    eye = Eye::Mono;
    tick = 0;
    const std::optional<std::uint32_t> frame = seqBackendCounter();
    if (!frame) {
        return;
    }
    const std::optional<stereo_seq::RenderTag> tag = seqTagForBackendFrame(*frame + 1u);
    if (tag) {
        eye = tag->eye;
        tick = tag->tick;
    }
}

// The sun world slices eye L listed, by tick (Shared keeps only those). Emptied when the multiplayer guard
// stops allowing game touches, so nothing stamped before a trip is matched after it.
stereo_seq::SunTickStamps g_stamps;
std::atomic<bool> g_guardClosed{false};

// The multiplayer guard's answer for this call; the first refusal empties the stamps.
bool guardAllows() {
    if (mp_guard::allowsGameTouch()) {
        g_guardClosed.store(false, std::memory_order_relaxed);
        return true;
    }
    if (!g_guardClosed.exchange(true, std::memory_order_relaxed)) {
        g_stamps.clear();
    }
    return false;
}

int eyeSlot(Eye eye) {
    return eye == Eye::Left ? 1 : eye == Eye::Right ? 2 : 0;
}

std::int32_t lightType(const HookRegisters& r) {
    std::int32_t v = 0;
    std::memcpy(&v, reinterpret_cast<const void*>(r.rsp + kLightTypeFromRsp), sizeof(v));
    return v;
}

Kind kindOf(std::int32_t type, std::uint32_t slice) {
    if ((slice & 1u) != 0) {
        return kPlayer;
    }
    if (type == stereo_seq::kParallelLightType) {
        return kSun;
    }
    return type == kSpotLight ? kSpot : kPoint;
}

unsigned long long take(std::atomic<std::uint64_t>& c) {
    return static_cast<unsigned long long>(c.exchange(0));
}

// Every 10 s: the stereo ticks; per eye the slices listed by kind and the sun's by cascade; eye R's sun world
// slices at the stale check, those eye L listed in the tick with the counter at 1, and those eye R kept by
// cascade.
void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counters.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_counters.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    const std::uint64_t ticksNow = seqCounters().stereoTicks;
    const unsigned long long ticks = ticksNow - g_counters.lastTicks.exchange(ticksNow);
    unsigned long long l[3][kKinds];
    unsigned long long c[3][kCascades];
    unsigned long long n[3];
    for (int e = 0; e < 3; ++e) {
        for (int k = 0; k < kKinds; ++k) {
            l[e][k] = take(g_counters.listed[e][k]);
        }
        for (std::uint32_t i = 0; i < kCascades; ++i) {
            c[e][i] = take(g_counters.sunByCascade[e][i]);
        }
        n[e] = take(g_counters.sunNoEntry[e]);
    }
    unsigned long long k[kCascades];
    for (std::uint32_t i = 0; i < kCascades; ++i) {
        k[i] = take(g_counters.kept[i]);
    }
    const unsigned long long checked = take(g_counters.checkedRight);
    const unsigned long long fresh = take(g_counters.freshRight);
    const unsigned long long leftOff = take(g_counters.leftOff);
    EVR_LOG(
        "%s: last 10 s (%s): %llu stereo ticks; listed sun/spot/point/player: eye L %llu/%llu/%llu/%llu, "
        "eye R %llu/%llu/%llu/%llu, other %llu/%llu/%llu/%llu; sun by cascade c0/c1/c2/c3: eye L "
        "%llu/%llu/%llu/%llu (%llu released at the check), eye R %llu/%llu/%llu/%llu (%llu); eye R's sun "
        "checks %llu, eye L's of the tick %llu, kept by cascade %llu/%llu/%llu/%llu, left off at the listing "
        "%llu",
        kTag, stereo_seq::sharedSunModeName(g_mode), ticks, l[1][0], l[1][1], l[1][2], l[1][3], l[2][0],
        l[2][1], l[2][2], l[2][3], l[0][0], l[0][1], l[0][2], l[0][3], c[1][0], c[1][1], c[1][2], c[1][3],
        n[1], c[2][0], c[2][1], c[2][2], c[2][3], n[2], checked, fresh, k[0], k[1], k[2], k[3], leftOff);
}

// On `cmp edx, eax`: edx the entry's counter, eax the threshold, bl the slice's flags, r13 its entry. Eye R's
// sun world slices are counted; one sharedSunKeeps allows keeps its entry: the registers as sunKeepRegisters
// sets them (`ja` not taken, `test bl, 4` and `test bl, 6` find nothing, so `je` at 0x1CFF35A skips the
// listing; on that path bl is dead: the slice loop sets ebx again at 0x1CFE81C before any read, and the exit
// pops rbx) and the counter back to 0, as eye R's own render would have set it.
void onCheck(HookRegisters& r) {
    stereo_seq::SunCheck c;
    c.lightType = lightType(r);
    c.slice = static_cast<std::uint32_t>(r.r12);
    if (r.r13 == 0 || !stereo_seq::isSunWorldSlice(c.lightType, c.slice) || !guardAllows()) {
        return;
    }
    tagNow(c.eye, c.tick);
    if (c.eye != Eye::Right) {
        return;
    }
    c.counter = static_cast<std::uint32_t>(r.rdx);
    c.listedByLeftThisTick = g_stamps.stamped(r.rdi, c.slice, c.tick);
    ++g_counters.checkedRight;
    if (c.counter == 1 && c.listedByLeftThisTick) {
        ++g_counters.freshRight;
    }
    if (!stereo_seq::sharedSunKeeps(g_mode, c)) {
        return;
    }
    const stereo_seq::SunKeepRegisters k = stereo_seq::sunKeepRegisters(r.rax, r.rbx, c.counter);
    r.rax = static_cast<std::uintptr_t>(k.rax);
    r.rbx = static_cast<std::uintptr_t>(k.rbx);
    const std::uint32_t zero = 0;
    std::memcpy(reinterpret_cast<void*>(r.r13 + kEntryCounter), &zero, sizeof(zero));
    ++g_counters.kept[stereo_seq::sunCascade(c.slice)];
}

// On `test bl, 6`: a slice the view lists for rendering. Eye L's sun world slices are stamped with the tick
// (Shared keeps only those in eye R); the listed slices are counted (every kind with Count, the sun's
// otherwise); with Ceiling eye R's sun world slices are left off here too (bl's bits 2 and 4 cleared at this
// decision point: `je` skips the listing, and bl is dead on that path as above).
void onListed(HookRegisters& r) {
    if ((r.rbx & kFlagsListed) == 0) {
        return;
    }
    const auto slice = static_cast<std::uint32_t>(r.r12);
    const std::int32_t type = lightType(r);
    const Kind kind = kindOf(type, slice);
    if ((kind != kSun && g_mode != SharedSunMode::Count) || !guardAllows()) {
        return;
    }
    Eye eye = Eye::Mono;
    std::uint64_t tick = 0;
    tagNow(eye, tick);
    if (g_mode == SharedSunMode::Shared && eye == Eye::Left && stereo_seq::isSunWorldSlice(type, slice)) {
        g_stamps.stamp(r.rdi, slice, tick);
    }
    if (stereo_seq::sharedSunSkipsListing(g_mode, eye, tick, type, slice)) {
        r.rbx = static_cast<std::uintptr_t>(stereo_seq::sunKeepRegisters(r.rax, r.rbx, 0).rbx);
        ++g_counters.leftOff;
        report();
        return;
    }
    const int e = eyeSlot(eye);
    ++g_counters.listed[e][kind];
    if (kind == kSun) {
        ++g_counters.sunByCascade[e][stereo_seq::sunCascade(slice)];
        if (r.r13 == 0) {
            ++g_counters.sunNoEntry[e];
        }
    }
    report();
}

SharedSunMode requestedMode(bool& known, std::string& narrow) {
    std::wstring value;
    if (readEnv(L"ETERNALVR_SHARED_SUN", value)) {
        for (const wchar_t c : value) {
            narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
    }
    return stereo_seq::sharedSunMode(narrow, &known);
}

void releaseShadowCheck(ShadowCheckOwner owner) {
    int expected = static_cast<int>(owner);
    g_owner.compare_exchange_strong(expected, 0);
}

// Locates both sites, then hooks the stale check first (the decision; on its own it keeps entries as the mode
// asks, the stamps then missing: Shared keeps nothing, so a failed listing hook only costs the counts) and
// the listing second. The claim on the stale check is let go again when nothing was hooked.
void install() {
    bool known = true;
    std::string value;
    g_mode = requestedMode(known, value);
    if (!known) {
        EVR_LOG(
            "%s: ETERNALVR_SHARED_SUN=%s is not a known value (0, off, false, no; count; 1, on, true, yes, "
            "shared; ceiling); off",
            kTag, value.c_str());
    }
    if (g_mode == SharedSunMode::Off) {
        return; // off by default: nothing logged, nothing located
    }
    if (!mp_guard::allowsGameTouch()) {
        EVR_LOG("%s: the multiplayer guard is not armed; not installed", kTag);
        return;
    }
    if (parallelEyesRequested() || parallelEyesChangedEngine() ||
        !claimShadowCheck(ShadowCheckOwner::SharedSun)) {
        EVR_LOG("%s: Parallel Eye Rendering holds the shadow setup; not installed", kTag);
        return;
    }
    GameImage image;
    const std::byte* site = nullptr;
    if (locateGameImage(image, kTag)) {
        site = findUnique(image, kTag, "shadow stale check", kCheckSignature);
    }
    if (site) {
        const std::byte* leaTarget = site + kImageBaseLea + 7 + readI32(site + kImageBaseLea + 3);
        if (leaTarget != image.base || !matchesAt(image, site + kListing, kListingSignature)) {
            EVR_LOG("%s: the shadow setup's listing is not as known", kTag);
            site = nullptr;
        }
    }
    std::string error;
    if (site && !installMidHookEdit(const_cast<std::byte*>(site + kCheckHook), &onCheck, error)) {
        EVR_LOG("%s: stale check hook failed: %s", kTag, error.c_str());
        site = nullptr;
    }
    if (!site) {
        releaseShadowCheck(ShadowCheckOwner::SharedSun);
        EVR_LOG("%s: not installed; eye R renders its own sun cascades", kTag);
        return;
    }
    g_counters.lastTicks.store(seqCounters().stereoTicks);
    g_installed = true;
    if (!installMidHookEdit(const_cast<std::byte*>(site + kListingHook), &onListed, error)) {
        EVR_LOG("%s: listing hook failed: %s; no counts and no stamps of eye L's slices, so shared keeps "
                "nothing (ceiling still keeps at the check)",
                kTag, error.c_str());
    }
    const char* what = "the sun's slices counted per eye and cascade, the game unchanged";
    if (g_mode == SharedSunMode::Shared) {
        what = "eye R shades with the sun cascades eye L rendered in the same tick instead of rendering them "
               "again";
    } else if (g_mode == SharedSunMode::Ceiling) {
        what = "eye R renders none of the sun's cascades and shades with eye L's (a rig bound)";
    }
    EVR_LOG("%s: shadow setup hooked (stale check RVA 0x%X, listing RVA 0x%X), mode %s: %s", kTag,
            image.rva(site + kCheckHook), image.rva(site + kListingHook),
            stereo_seq::sharedSunModeName(g_mode), what);
}

} // namespace

bool claimShadowCheck(ShadowCheckOwner owner) {
    int expected = 0;
    const int wanted = static_cast<int>(owner);
    return g_owner.compare_exchange_strong(expected, wanted) || expected == wanted;
}

bool installSharedSunHooks() {
    std::call_once(g_once, install);
    return g_installed;
}

} // namespace evr::vkcore
