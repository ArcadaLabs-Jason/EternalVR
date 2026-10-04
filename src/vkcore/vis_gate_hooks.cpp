#include "vkcore/vis_gate_hooks.hpp"

#include "stereo_seq/stereo_taa.hpp"
#include "stereo_seq/vis_gate.hpp"
#include "vkcore/game_code.hpp"
#include "vkcore/game_text.hpp"
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
#include <vector>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-visgate";

constexpr std::size_t kCounter = 0x2BC1F4; // the view's render counter (S + 0x2BC1F4)
// Per model: the counter + 1 of the render that last counted it.
constexpr std::size_t kLastVisible = 0x1A81F8;

// The main gather's gate (RVA 0x1C775E4): `mov eax, [rcx + 0x2BC1F4]`, `cmp [rcx + rdx*4 + 0x1A81F8], eax`,
// `jl restart` (+0xD), `mov r8d, [rcx + rdx*4 + 0x1E81F8]` (+0xF, the count goes on), `inc eax` (+0x17).
constexpr const char* kMainSignature = "8B 81 F4 C1 2B 00 39 84 91 F8 81 1A 00 7C 08";
constexpr std::size_t kMainGoesOn = 0xF;
constexpr std::size_t kMainRestarts = 0x17;
static_assert(kMainRestarts == kMainGoesOn + 0x08); // `jl` (7C 08) ends at the count load
// The second gather's two gates (RVA 0x1C79131, 0x1C7943C): the same with S in rdx and the model index in
// r15, `cmp` one byte longer: the count load at +0x10, `inc eax` at +0x18.
constexpr const char* kSecondSignature = "8B 82 F4 C1 2B 00 42 39 84 BA F8 81 1A 00 7C 08";
constexpr std::size_t kSecondGoesOn = 0x10;
constexpr std::size_t kSecondRestarts = 0x18;
static_assert(kSecondRestarts == kSecondGoesOn + 0x08);

// Particles' UpdateInView (0x1955150, at RVA 0x195523D): `mov rax, [rdi]` (P, the particle frame owner),
// `mov ecx, [rax + 0x4D0]` (the frame number, stepped once per render), `dec ecx`, `cmp [rbx + 0x61C], ecx`
// (the model's last update), `jne skip` (+0x11): +0x13 simulates, the jump target +0x29 skips the simulation.
constexpr const char* kParticleSignature =
    "48 8B 07 8B 88 D0 04 00 00 FF C9 39 8B 1C 06 00 00 75 16 48 8B CB E8";
constexpr std::size_t kParticleSimulatesAt = 0x13;
constexpr std::size_t kParticleSkipsAt = 0x29;
static_assert(kParticleSkipsAt == kParticleSimulatesAt + 0x16); // `jne` (75 16) ends at the simulation
constexpr std::size_t kParticleFrame = 0x4D0;
constexpr std::size_t kParticleStamp = 0x61C;
std::uintptr_t g_particleSimulates = 0;
std::uintptr_t g_particleSkips = 0;

struct Site {
    int base = 0; // x86 register numbers (registerByNumber)
    int index = 0;
    std::uintptr_t goesOn = 0;   // the count load
    std::uintptr_t restarts = 0; // `inc eax`, with the count register already 0
};

// At most three sites, written once at install before any hook can fire.
Site g_sites[3];
std::once_flag g_once;
bool g_installed = false;
// Set once all six hooks are in: until then they keep the engine's own behaviour, so a failed install never
// counts a model with one test in one gather and the other test in another (the stamps are shared), and never
// draws a one-eye model without the no-wait query copies and the particle check.
std::atomic<bool> g_live{false};

// The copies of the occlusion queries' results (0x1C32F20, at the start of each view's emissive and blend
// job): `mov dword [rsp + 0x38], 3`, the flags argument VK_QUERY_RESULT_64_BIT | WAIT_BIT, at RVA 0x1C32FAF
// (a run of pending queries) and 0x1C33039 (the last run). Once a model one eye sees is drawn, the copy in
// the other eye's render waits on queries that render never issued, and the game's queue stalls for good (the
// Parallel Eye hang, pe-geom view_query_copy.cpp). The hooks write the flags without the wait, for every
// copy: which queries are left over is not known, and an unfinished query keeps its slot's previous result
// (an object culled or kept by a stale occlusion result for a frame). Pending queries are plain counts: no
// crash path.
constexpr const char* kQueryRunSignature = "C7 44 24 38 03 00 00 00 45 2B C8 49 63 C0 41 FF";
constexpr const char* kQueryLastSignature = "C7 44 24 38 03 00 00 00 49 63 C0 48 C1 E0 03 48";
constexpr std::size_t kQueryFlagsSize = 8;  // the store's length
constexpr std::uint32_t kQueryNoWait = 0x1; // VK_QUERY_RESULT_64_BIT
std::uintptr_t g_queryResume[2] = {};

// Counted on the gather jobs' threads for the periodic line: relaxed, and the report is tried once per 4096
// gate hits rather than on every one.
struct Counters {
    std::atomic<std::uint64_t> widened{0}; // continued only thanks to the two-render test (the other eye's)
    std::atomic<std::uint64_t> engine{0};  // continued by the engine's own test
    std::atomic<std::uint64_t> restarted{0};
    std::atomic<std::uint64_t> particleWidened{0}; // simulated only thanks to the two-render test
    std::atomic<std::uint64_t> queryNoWait{0};     // occlusion query copies made without the wait
    std::atomic<std::uint64_t> lastReport{0};
} g_counters;

bool requested() {
    std::wstring value;
    std::string narrow;
    if (readEnv(L"ETERNALVR_STEREO_VIS_GATE", value)) {
        for (const wchar_t c : value) {
            narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
    }
    return stereo_seq::switchValue(narrow, true);
}

void add(std::atomic<std::uint64_t>& counter) {
    counter.fetch_add(1, std::memory_order_relaxed);
}

void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counters.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_counters.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    EVR_LOG("%s: first-visible gate: %llu count(s) went on through the other eye's render, %llu by the "
            "engine's own test, %llu restarted; %llu particle update(s) through the other eye's render; %llu "
            "occlusion query copies without the wait",
            kTag, static_cast<unsigned long long>(g_counters.widened.exchange(0)),
            static_cast<unsigned long long>(g_counters.engine.exchange(0)),
            static_cast<unsigned long long>(g_counters.restarted.exchange(0)),
            static_cast<unsigned long long>(g_counters.particleWidened.exchange(0)),
            static_cast<unsigned long long>(g_counters.queryNoWait.exchange(0)));
}

std::int32_t readI32(std::uintptr_t at) {
    std::int32_t v = 0;
    std::memcpy(&v, reinterpret_cast<const void*>(at), sizeof(v));
    return v;
}

// On the counter load: the load and the compare done here, then on to the engine's continue or restart path.
void onGate(HookRegisters& r, const Site& site) {
    const std::uintptr_t s = registerByNumber(r, site.base);
    const std::uintptr_t index = registerByNumber(r, site.index); // full register, as the engine addresses
    const std::int32_t counter = readI32(s + kCounter);
    const std::int32_t lastVisible = readI32(s + index * 4 + kLastVisible);
    // The `mov eax` the resume skips (zero-extended, as it would).
    r.rax = static_cast<std::uint32_t>(counter);
    const bool engine = stereo_seq::visGateContinues(lastVisible, counter, 1);
    const bool widen = g_live.load(std::memory_order_acquire) && mp_guard::allowsGameTouch();
    const bool goesOn = engine || (widen && stereo_seq::visGateContinues(lastVisible, counter, 2));
    r.resumeAt = goesOn ? site.goesOn : site.restarts;
    std::atomic<std::uint64_t>& counted =
        goesOn ? (engine ? g_counters.engine : g_counters.widened) : g_counters.restarted;
    if ((counted.fetch_add(1, std::memory_order_relaxed) & 0xFFF) == 0) {
        report();
    }
}
// On the particle frame load: the loads and the compare done here, then on to the simulation or past it.
void onParticle(HookRegisters& r) {
    const std::uintptr_t p = *reinterpret_cast<const std::uintptr_t*>(r.rdi);
    const std::int32_t frame = readI32(p + kParticleFrame);
    const std::int32_t stamp = readI32(r.rbx + kParticleStamp);
    r.rax = p; // the skipped `mov rax, [rdi]`
    r.rcx = static_cast<std::uint32_t>(static_cast<std::uint32_t>(frame) - 1u); // and `mov ecx` + `dec ecx`
    const bool engine = stereo_seq::updateInViewContinues(stamp, frame, 1);
    const bool widen = !engine && g_live.load(std::memory_order_acquire) && mp_guard::allowsGameTouch() &&
                       stereo_seq::updateInViewContinues(stamp, frame, 2);
    r.resumeAt = engine || widen ? g_particleSimulates : g_particleSkips;
    if (widen) {
        add(g_counters.particleWidened);
    }
}

// On a query copy's flags store: the flags written without the wait, once the gates draw one-eye models. Like
// every hook here it stands down when the multiplayer guard trips (a multiplayer map load or menu screen).
template <int I>
void onQueryFlags(HookRegisters& r) {
    if (!g_live.load(std::memory_order_acquire) || !mp_guard::allowsGameTouch()) {
        return; // the game's own store runs
    }
    std::memcpy(reinterpret_cast<void*>(r.rsp + 0x38), &kQueryNoWait, sizeof(kQueryNoWait));
    r.resumeAt = g_queryResume[I];
    add(g_counters.queryNoWait);
}

void onGate0(HookRegisters& r) {
    onGate(r, g_sites[0]);
}
void onGate1(HookRegisters& r) {
    onGate(r, g_sites[1]);
}
void onGate2(HookRegisters& r) {
    onGate(r, g_sites[2]);
}

} // namespace

bool installVisGateHooks() {
    std::call_once(g_once, [] {
        if (!requested()) {
            EVR_LOG("%s: off (ETERNALVR_STEREO_VIS_GATE=0): models seen by one eye only are not drawn", kTag);
            return;
        }
        if (!mp_guard::allowsGameTouch()) {
            EVR_LOG("%s: the multiplayer guard is not armed; not installed", kTag);
            return;
        }
        GameImage image;
        if (!locateGameImage(image, kTag)) {
            return;
        }
        const std::byte* main = findUnique(image, kTag, "main gather's first-visible gate", kMainSignature);
        std::vector<const std::byte*> second;
        if (const auto pattern = resolver::Pattern::parse(kSecondSignature)) {
            for (const std::size_t offset : resolver::findAll(image.text, *pattern)) {
                second.push_back(image.text.data() + offset);
            }
        }
        const std::byte* particle =
            findUnique(image, kTag, "particles' update-in-view check", kParticleSignature);
        const std::byte* queryRun = findUnique(image, kTag, "occlusion query copy (run)", kQueryRunSignature);
        const std::byte* queryLast =
            findUnique(image, kTag, "occlusion query copy (last)", kQueryLastSignature);
        if (!main || second.size() != 2 || !particle || !queryRun || !queryLast) {
            EVR_LOG(
                "%s: %s, %zu of 2 second-gather gate(s), particle check %s, query copies %s; not installed: "
                "models seen by one eye only are not drawn",
                kTag, main ? "main gate found" : "main gate missing", second.size(),
                particle ? "found" : "missing", queryRun && queryLast ? "found" : "missing");
            return;
        }
        const auto at = [](const std::byte* p, std::size_t off) {
            return reinterpret_cast<std::uintptr_t>(p) + off;
        };
        // rcx = 1, rdx = 2, r15 = 15 (registerByNumber).
        g_sites[0] = {1, 2, at(main, kMainGoesOn), at(main, kMainRestarts)};
        g_sites[1] = {2, 15, at(second[0], kSecondGoesOn), at(second[0], kSecondRestarts)};
        g_sites[2] = {2, 15, at(second[1], kSecondGoesOn), at(second[1], kSecondRestarts)};
        g_particleSimulates = at(particle, kParticleSimulatesAt);
        g_particleSkips = at(particle, kParticleSkipsAt);
        g_queryResume[0] = at(queryRun, kQueryFlagsSize);
        g_queryResume[1] = at(queryLast, kQueryFlagsSize);
        // All hooks stay inert until g_live, set only once every one is in: a gate that draws one-eye models
        // must never run without the no-wait copies (the queue stalls) or the particle check (effects never
        // simulated).
        MidHookEditCallback callbacks[6] = {&onQueryFlags<0>, &onQueryFlags<1>, &onParticle,
                                            &onGate0,         &onGate1,         &onGate2};
        const std::byte* sites[6] = {queryRun, queryLast, particle, main, second[0], second[1]};
        std::string error;
        for (int i = 0; i < 6; ++i) {
            if (!installMidHookEdit(const_cast<std::byte*>(sites[i]), callbacks[i], error)) {
                // Hooks already in stay in (they cannot be removed) and keep the engine's own test (g_live).
                EVR_LOG("%s: hook at RVA 0x%X failed: %s; the gates keep the engine's test", kTag,
                        image.rva(sites[i]), error.c_str());
                return;
            }
        }
        g_installed = true;
        g_live.store(true, std::memory_order_release);
        EVR_LOG(
            "%s: first-visible gates at RVA 0x%X, 0x%X, 0x%X, the particle check at 0x%X, query copies at "
            "0x%X, 0x%X: a model counts on and simulates through either eye's last render; query copies do "
            "not wait",
            kTag, image.rva(sites[3]), image.rva(sites[4]), image.rva(sites[5]), image.rva(sites[2]),
            image.rva(sites[0]), image.rva(sites[1]));
    });
    return g_installed;
}

} // namespace evr::vkcore
