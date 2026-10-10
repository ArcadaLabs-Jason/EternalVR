// A second opinion from the view frustum for the lights Umbra hides (light_cull.hpp).

#include "vkcore/light_cull.hpp"

#include "vkcore/game_code.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

namespace evr::vkcore {
namespace {

constexpr const char* kTag = "light-cull";
constexpr std::uint32_t kKnownTimestamp = 0x6A7B9B8C; // Steam build 25216728
// The anchor (RVA 0x1C7851E): mov rax, [rdi+18h]; cmp byte [rax+24h], 0 (useUmbra); je +0x43F (the Umbra-off
// frustum path); mov edx, [rbx+54h] (the light's Umbra object); test edx, edx; je ...
constexpr const char* kSignature = "48 8B 47 18 80 78 24 00 0F 84 3F 04 00 00 8B 53 54 85 D2 0F 84";

// The rest of the gather, by offset from the anchor, each checked byte for byte before anything is hooked.
struct Expected {
    std::size_t offset;
    const char* what;
    std::size_t size;
    std::uint8_t bytes[8];
};
constexpr std::size_t kGateA = 0x23;      // test al, al; jne keep (after the visible-light-set lookup)
constexpr std::size_t kGateB = 0x2C9;     // test eax, eax; je drop (after the occlusion-buffer test)
constexpr std::size_t kSphere = 0x432;    // test al, al; je drop (after the sphere test of a kept light)
constexpr std::size_t kGateBDrop = 0x43C; // gate B's drop: rbx back, the light's done bit cleared
constexpr std::size_t kSkipCheck = 0x44D; // cmp byte [rax+2Bh], 0: the frustum path's r_skipLightCPUCulling
constexpr std::size_t kFrustum = 0x453;   // the frustum test, past that check
constexpr std::size_t kVerdict = 0x4A5;   // test al, al; jne drop (the frustum test's verdict)
constexpr std::size_t kKeep = 0x4AD;      // mov r10, [rsp+38h]: the light goes on into the view's list
// The drop the frustum test jumps to: 0x1C78549 (rbx back, the light's done bit left set).
constexpr std::size_t kFrustumDrop = 0x2B;
constexpr Expected kExpected[] = {
    {kGateA, "gate A", 8, {0x84, 0xC0, 0x0F, 0x85, 0xA6, 0x02, 0x00, 0x00}},
    {kGateB, "gate B", 8, {0x85, 0xC0, 0x0F, 0x84, 0x6B, 0x01, 0x00, 0x00}},
    {kSphere, "sphere test", 8, {0x84, 0xC0, 0x0F, 0x84, 0xF1, 0xFB, 0xFF, 0xFF}},
    {kGateBDrop, "gate B drop", 8, {0x48, 0x8B, 0x5C, 0x24, 0x30, 0x40, 0xF6, 0xD6}},
    {kSkipCheck, "frustum skip check", 4, {0x80, 0x78, 0x2B, 0x00}},
    {kFrustum, "frustum test", 8, {0x48, 0x8B, 0x4F, 0x20, 0x4C, 0x8D, 0x83, 0xF0}},
    {kVerdict, "frustum verdict", 8, {0x84, 0xC0, 0x0F, 0x85, 0x7E, 0xFB, 0xFF, 0xFF}},
    {kKeep, "keep", 5, {0x4C, 0x8B, 0x54, 0x24, 0x38}},
    {kFrustumDrop, "frustum drop", 5, {0x48, 0x8B, 0x5C, 0x24, 0x30}},
};
constexpr std::size_t kSettings = 0x18;     // rdi: the gather's context; [rdi+18h] its settings
constexpr std::size_t kSkipLightCpu = 0x2B; // settings: r_skipLightCPUCulling

enum Gate { NoGate = 0, GateA, GateB, GateSphere, Gates };

// Written once at install, before any hook can fire.
std::uintptr_t g_frustum = 0;
std::uintptr_t g_keep = 0;
std::uintptr_t g_drop[Gates] = {}; // per gate, where its own drop goes
std::once_flag g_once;
bool g_installed = false;
// Set once every hook is in: until then the hooks keep the engine's own verdicts.
std::atomic<bool> g_live{false};

// The gate whose drop the light in flight on this thread is getting a frustum test for (NoGate: the engine's
// own frustum path, with Umbra off). The gather runs one light at a time on a job thread.
thread_local int t_asking = NoGate;

// Counted on the gather jobs' threads for the periodic line: relaxed, tried once per 256 lights tested.
struct Counters {
    std::atomic<std::uint64_t> kept[Gates] = {}; // dropped by the gate, kept by the frustum test
    std::atomic<std::uint64_t> dropped{0};       // dropped by both
    std::atomic<std::uint64_t> tested{0};
    std::atomic<std::uint64_t> lastReport{0};
} g_counters;

bool asked() {
    std::wstring value;
    return readEnv(L"ETERNALVR_LIGHT_FRUSTUM", value) && value == L"1";
}

void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counters.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_counters.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    const auto take = [](std::atomic<std::uint64_t>& c) {
        return static_cast<unsigned long long>(c.exchange(0));
    };
    const unsigned long long a = take(g_counters.kept[GateA]);
    const unsigned long long b = take(g_counters.kept[GateB]);
    const unsigned long long s = take(g_counters.kept[GateSphere]);
    EVR_LOG("%s: lights Umbra hid that the view frustum kept: %llu by the visible-light set, %llu by the "
            "occlusion buffer, %llu by the sphere test; %llu hidden by both",
            kTag, a, b, s, take(g_counters.dropped));
}

// At one of Umbra's drops: a light Umbra hides goes to the frustum test, as with Umbra off, and is dropped
// only when that test drops it too.
void onGate(HookRegisters& r, Gate gate, bool hidden) {
    if (!hidden || !g_live.load(std::memory_order_acquire) || !mp_guard::allowsGameTouch()) {
        return;
    }
    std::uintptr_t settings = 0;
    std::memcpy(&settings, reinterpret_cast<const void*>(r.rdi + kSettings), sizeof(settings));
    r.rax = settings; // as on the game's own way in (0x1C7851E): the settings block
    if (*reinterpret_cast<const std::uint8_t*>(settings + kSkipLightCpu) != 0) {
        r.resumeAt = g_keep; // r_skipLightCPUCulling: the frustum test is off, so every light is kept
        return;
    }
    t_asking = gate;
    r.resumeAt = g_frustum;
}
void onGateA(HookRegisters& r) {
    onGate(r, GateA, (r.rax & 0xFF) == 0);
}
void onGateB(HookRegisters& r) {
    onGate(r, GateB, (r.rax & 0xFFFFFFFF) == 0);
}
void onSphere(HookRegisters& r) {
    onGate(r, GateSphere, (r.rax & 0xFF) == 0);
}

// At the frustum test's verdict: a light it drops takes the drop of the gate that asked (gate B clears the
// light's done bit, so another area of the view can still keep it).
void onVerdict(HookRegisters& r) {
    const int gate = t_asking;
    if (gate == NoGate) {
        return;
    }
    t_asking = NoGate;
    const bool drops = (r.rax & 0xFF) != 0;
    if (drops) {
        r.resumeAt = g_drop[gate];
        g_counters.dropped.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_counters.kept[gate].fetch_add(1, std::memory_order_relaxed);
    }
    if ((g_counters.tested.fetch_add(1, std::memory_order_relaxed) & 0xFF) == 0) {
        report();
    }
}

void install() {
    if (!asked()) {
        return;
    }
    if (!mp_guard::allowsGameTouch()) {
        EVR_LOG("%s: the multiplayer guard is not armed; not installed", kTag);
        return;
    }
    GameText text;
    if (!findGameText(text)) {
        EVR_LOG("%s: the game's code cannot be read", kTag);
        return;
    }
    if (text.timestamp != kKnownTimestamp) {
        EVR_LOG("%s: game build 0x%08X is not the one this was read on; lights keep Umbra's culling", kTag,
                text.timestamp);
        return;
    }
    const std::byte* at = findUniqueInText(text, kTag, "the light gather's Umbra test", kSignature);
    if (!at) {
        EVR_LOG("%s: the light gather's Umbra test is not in this build; lights keep Umbra's culling", kTag);
        return;
    }
    const std::byte* end = text.bytes.data() + text.bytes.size();
    for (const Expected& e : kExpected) {
        if (at + e.offset + e.size > end || std::memcmp(at + e.offset, e.bytes, e.size) != 0) {
            EVR_LOG("%s: unexpected code for the %s at RVA 0x%X; lights keep Umbra's culling", kTag, e.what,
                    rvaOf(text, at + e.offset));
            return;
        }
    }
    const auto address = [&](std::size_t offset) {
        return reinterpret_cast<std::uintptr_t>(at + offset);
    };
    g_frustum = address(kFrustum);
    g_keep = address(kKeep);
    g_drop[GateA] = address(kFrustumDrop);
    g_drop[GateB] = address(kGateBDrop);
    g_drop[GateSphere] = address(kFrustumDrop);
    // The verdict's hook first: a gate can only send a light there once it is in (and all stay inert until
    // g_live anyway).
    struct Hook {
        std::size_t offset;
        MidHookEditCallback callback;
    };
    const Hook hooks[] = {
        {kVerdict, &onVerdict}, {kGateA, &onGateA}, {kGateB, &onGateB}, {kSphere, &onSphere}};
    std::string error;
    for (const Hook& h : hooks) {
        if (!installMidHookEdit(const_cast<std::byte*>(at + h.offset), h.callback, error)) {
            // Hooks already in stay in (they cannot be removed) and keep Umbra's verdicts (g_live).
            EVR_LOG("%s: hook at RVA 0x%X failed: %s; lights keep Umbra's culling", kTag,
                    rvaOf(text, at + h.offset), error.c_str());
            return;
        }
    }
    g_installed = true;
    g_live.store(true, std::memory_order_release);
    EVR_LOG("%s: a light Umbra hides is dropped only when the view frustum drops it too (gates at RVA 0x%X, "
            "0x%X, 0x%X)",
            kTag, rvaOf(text, at + kGateA), rvaOf(text, at + kGateB), rvaOf(text, at + kSphere));
}

} // namespace

bool installLightFrustumCull() {
    std::call_once(g_once, install);
    return g_installed;
}

} // namespace evr::vkcore
