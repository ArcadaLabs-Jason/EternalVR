#include "vkcore/object_prev_hooks.hpp"

#include "stereo_seq/object_prev.hpp"
#include "stereo_seq/stereo_taa.hpp"
#include "vkcore/game_code.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/ring_trace.hpp"
#include "vkcore/seq_hooks.hpp"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-objprev";

// The render-view job's current-frame picks (RVA 0x1C549F1): `lea rsi, [rip + counter object]`, `mov rcx,
// rsi`, `call counter` (+0xA), `mov r8d, eax` (+0xF, the hook), the ring index mod 3, `mov rdx, [rip +
// jointOffsetsBuffer]` (+0x2C), `mov r8, [rax + r8 * 8 + 0xC38620]`, `call set`; then the same for
// modelMatricesBuffer: `call counter` (+0x50), `mov r8d, eax` (+0x55), `mov rdx, [rip + param]` (+0x72),
// `mov r8, [rax + r8 * 8 + 0xC38638]`.
constexpr const char* kCurrentSignature =
    "48 8D 35 ?? ?? ?? ?? 48 8B CE E8 ?? ?? ?? ?? 44 8B C0 B8 56 55 55 55 41 F7 E8 49 8B 84 24 C0 9D 4D 00 "
    "8B CA C1 E9 1F 03 D1 8D 0C 52 48 8B 15 ?? ?? ?? ?? 44 2B C1 49 8B CF 4D 63 C0 4E 8B 84 C0 20 86 C3 00 "
    "49 83 C0 18 E8 ?? ?? ?? ?? 48 8B CE E8 ?? ?? ?? ?? 44 8B C0 B8 56 55 55 55 41 F7 E8 49 8B 84 24 C0 9D "
    "4D 00 8B CA C1 E9 1F 03 D1 8D 0C 52 48 8B 15 ?? ?? ?? ?? 44 2B C1 49 8B CF 4D 63 C0 4E 8B 84 C0 38 86 "
    "C3 00 49 83 C0 18";
constexpr std::size_t kCurrentCalls[2] = {0xA, 0x50};
constexpr std::size_t kCurrentHooks[2] = {0xF, 0x55};
constexpr std::size_t kCurrentParams[2] = {0x2C, 0x72};
constexpr std::string_view kCurrentNames[2] = {"jointOffsetsBuffer", "modelMatricesBuffer"};

// The previous-frame picks (RVA 0x1C54A84): `mov rcx, rsi`, `call counter` (+0x3), `lea r8d, [rax + 2]`
// (+0x8, the hook), the ring index mod 3, `mov rdx, [rip + prevJointOffsetsBuffer]` (+0x26), `call set`
// (+0x42); then the same for prevModelMatricesBuffer: `call counter` (+0x4A), `lea` (+0x4F), `mov rdx, [rip +
// param]` (+0x6D).
constexpr const char* kPreviousSignature =
    "48 8B CE E8 ?? ?? ?? ?? 44 8D 40 02 B8 56 55 55 55 41 F7 E8 49 8B 84 24 C0 9D 4D 00 8B CA C1 E9 1F "
    "03 D1 8D 0C 52 48 8B 15 ?? ?? ?? ?? 44 2B C1 49 8B CF 4D 63 C0 4E 8B 84 C0 20 86 C3 00 49 83 C0 18 "
    "E8 ?? ?? ?? ?? 48 8B CE E8 ?? ?? ?? ?? 44 8D 40 02 B8 56 55 55 55 41 F7 E8 49 8B 84 24 C0 9D 4D 00 "
    "8B CA C1 E9 1F 03 D1 8D 0C 52 48 8B 15 ?? ?? ?? ??";
constexpr std::size_t kPreviousCalls[2] = {0x3, 0x4A};
constexpr std::size_t kPreviousHooks[2] = {0x8, 0x4F};
constexpr std::size_t kPreviousParams[2] = {0x26, 0x6D};
constexpr std::string_view kPreviousNames[2] = {"prevJointOffsetsBuffer", "prevModelMatricesBuffer"};

// The ring's upload (in 0x1C00B40, RVA 0x1C00C22): `lea rcx, [rip + counter object]`, `call counter` (+0x7),
// `mov r8d, eax` (+0xC, the hook), the ring index mod 3, `mov rcx, [rsi + 0x18]`, `movsxd rax, r8d`, `mov
// ebx, [rcx + 0x1362A0]`, then the two slots' buffers (+ 0xC38620 and + 0xC38638).
constexpr const char* kUploadSignature =
    "48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 44 8B C0 B8 56 55 55 55 41 F7 E8 8B CA "
    "C1 E9 1F 03 D1 8D 0C 52 44 2B C1 48 8B 4E 18 49 63 C0 8B 99 A0 62 13 00 "
    "48 8B BC C1 20 86 C3 00 4C 8B A4 C1 38 86 C3 00";
constexpr std::size_t kUploadCall = 0x7;
constexpr std::size_t kUploadHook = 0xC;

// A shader parameter global holds the parameter object; the pointer 0x10 before it, the name.
constexpr std::size_t kParamName = 0x10;

std::once_flag g_once;
bool g_installed = false;
// Set once all five hooks are in: until then every hook leaves the engine's picks alone, so a failed install
// never mixes the ring's slots with the engine's.
std::atomic<bool> g_live{false};

std::mutex g_mutex;
stereo_seq::ObjectRing g_ring;

struct Counters {
    std::atomic<std::uint64_t> remapped{0}; // renders given other slots than the engine's
    std::atomic<std::uint64_t> engine{0};   // renders whose slots happened to be the engine's
    std::atomic<std::uint64_t> untagged{0}; // of those, renders without a tag (handled as mono)
    // Renders first asked with a counter other than the last one + 1: the slot allocator assumes renders are
    // first asked in counter order (object_prev.hpp), and a render asked again after its answer was dropped
    // counts here too.
    std::atomic<std::uint64_t> outOfOrder{0};
    std::atomic<std::uint64_t> lastReport{0};
} g_counters;

bool requested() {
    std::wstring value;
    std::string narrow;
    if (readEnv(L"ETERNALVR_STEREO_OBJECT_PREV", value)) {
        for (const wchar_t c : value) {
            narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
    }
    return stereo_seq::switchValue(narrow, true);
}

void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counters.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_counters.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    EVR_LOG(
        "%s: object-transform ring: %llu render(s) on other slots than the engine's, %llu on its own, %llu "
        "of them without a tag, %llu first asked out of counter order",
        kTag, static_cast<unsigned long long>(g_counters.remapped.exchange(0)),
        static_cast<unsigned long long>(g_counters.engine.exchange(0)),
        static_cast<unsigned long long>(g_counters.untagged.exchange(0)),
        static_cast<unsigned long long>(g_counters.outOfOrder.exchange(0)));
}

// The picks for the render with this counter; nullopt until all five hooks are in (the engine's own picks).
// Every render goes through the ring, mono ones as well. The render's tag is the one that presents with
// backend frame counter + 1 (ring_trace.hpp showed the two in step on every render). Not the tag "in flight":
// this job runs ahead of the backend, and under a headset's load the backend is often a frame behind, which
// handed renders another render's eye.
std::optional<stereo_seq::ObjectRing::Picks> picksFor(std::uint32_t counter, bool count) {
    if (!g_live.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    // A render without a tag (the main menu, a load) goes through the ring as mono, so the engine's own picks
    // never mix with the ring's.
    const std::optional<stereo_seq::RenderTag> tag = seqTagForBackendFrame(counter + 1u);
    const stereo_seq::Eye eye = tag ? tag->eye : stereo_seq::Eye::Mono;
    stereo_seq::ObjectRing::Picks picks;
    bool inOrder = true;
    {
        std::lock_guard lock(g_mutex);
        picks = g_ring.picksFor(eye, tag ? tag->tick : 0u, counter, &inOrder);
    }
    if (!inOrder) {
        ++g_counters.outOfOrder;
    }
    if (count) {
        ++(picks.remapped ? g_counters.remapped : g_counters.engine);
        if (!tag) {
            ++g_counters.untagged;
        }
        report();
    }
    return picks;
}

void setCounter(HookRegisters& r, std::uint32_t counter) {
    r.rax = (r.rax & ~std::uintptr_t{0xFFFFFFFF}) | counter;
}

// On each `mov r8d, eax` after the counter call: rax is the render counter.
void onCurrent(HookRegisters& r, RingSite site, bool count) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    const auto engine = static_cast<std::uint32_t>(r.rax);
    std::uint32_t given = engine;
    if (const auto picks = picksFor(engine, count)) {
        given = picks->current;
        setCounter(r, given);
    }
    ringTraceRecord(site, engine, given);
}
void onCurrentJoints(HookRegisters& r) {
    onCurrent(r, RingSite::CurrentJoints, true);
}
void onCurrentMatrices(HookRegisters& r) {
    onCurrent(r, RingSite::CurrentMatrices, false);
}
void onUpload(HookRegisters& r) {
    onCurrent(r, RingSite::Upload, false);
}

// On each `lea r8d, [rax + 2]`: rax is the render counter the engine picks the previous frame's buffer with.
void onPrevious(HookRegisters& r, RingSite site) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    const auto engine = static_cast<std::uint32_t>(r.rax);
    std::uint32_t given = engine;
    if (const auto picks = picksFor(engine, false)) {
        given = picks->previous;
        setCounter(r, given);
    }
    ringTraceRecord(site, engine, given);
}
void onPreviousJoints(HookRegisters& r) {
    onPrevious(r, RingSite::PreviousJoints);
}
void onPreviousMatrices(HookRegisters& r) {
    onPrevious(r, RingSite::PreviousMatrices);
}

// The counter calls reach `counter` and each parameter load names `names[i]`.
bool checks(const GameImage& image,
            const std::byte* site,
            const std::size_t (&calls)[2],
            const std::size_t (&params)[2],
            const std::string_view (&names)[2],
            const std::byte* counter) {
    for (int i = 0; i < 2; ++i) {
        if (relativeTarget(site + calls[i]) != counter) {
            return false;
        }
        const std::byte* global = ripTarget(image, site + params[i] + 3, site + params[i] + 7);
        const std::byte* name = nullptr;
        if (global && image.contains(global - kParamName, sizeof(name))) {
            std::memcpy(&name, global - kParamName, sizeof(name));
        }
        if (!name || stringAt(image, name) != names[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

bool installObjectPrevHooks() {
    std::call_once(g_once, [] {
        initRingTrace();
        if (!requested()) {
            EVR_LOG("%s: off (ETERNALVR_STEREO_OBJECT_PREV=0): eye R's moving objects have no motion", kTag);
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
        const std::byte* current = findUnique(image, kTag, "current-frame object buffers", kCurrentSignature);
        const std::byte* previous =
            findUnique(image, kTag, "previous-frame object buffers", kPreviousSignature);
        const std::byte* upload = findUnique(image, kTag, "object buffer upload", kUploadSignature);
        if (!current || !previous || !upload) {
            EVR_LOG("%s: not installed; eye R's moving objects have no motion", kTag);
            return;
        }
        const std::byte* counter = relativeTarget(current + kCurrentCalls[0]);
        const bool ok = counter && image.inText(counter) &&
                        checks(image, current, kCurrentCalls, kCurrentParams, kCurrentNames, counter) &&
                        checks(image, previous, kPreviousCalls, kPreviousParams, kPreviousNames, counter) &&
                        relativeTarget(upload + kUploadCall) == counter;
        if (!ok) {
            EVR_LOG("%s: the counter calls or the parameter names did not check out; not installed", kTag);
            return;
        }
        // All five or none: slots from the ring in some places and the engine's in others would mix frames.
        const struct {
            const std::byte* at;
            MidHookEditCallback callback;
        } hooks[] = {
            {current + kCurrentHooks[0], &onCurrentJoints},
            {current + kCurrentHooks[1], &onCurrentMatrices},
            {previous + kPreviousHooks[0], &onPreviousJoints},
            {previous + kPreviousHooks[1], &onPreviousMatrices},
            {upload + kUploadHook, &onUpload},
        };
        std::string error;
        for (const auto& h : hooks) {
            if (!installMidHookEdit(const_cast<std::byte*>(h.at), h.callback, error)) {
                // Hooks that went in stay in (they cannot be removed) but leave the engine's picks alone:
                // the ring goes live only once all five are in.
                EVR_LOG("%s: hook at RVA 0x%X failed: %s; the object ring stays off", kTag, image.rva(h.at),
                        error.c_str());
                return;
            }
        }
        g_installed = true;
        g_live.store(true, std::memory_order_release);
        EVR_LOG(
            "%s: object-transform ring: picks at RVA 0x%X, 0x%X, 0x%X, 0x%X, upload at 0x%X: each render its "
            "own slot, the previous frame from the tick before",
            kTag, image.rva(hooks[0].at), image.rva(hooks[1].at), image.rva(hooks[2].at),
            image.rva(hooks[3].at), image.rva(hooks[4].at));
    });
    return g_installed;
}

} // namespace evr::vkcore
