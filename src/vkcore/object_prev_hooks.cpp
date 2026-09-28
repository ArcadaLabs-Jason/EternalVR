#include "vkcore/object_prev_hooks.hpp"

#include "stereo_seq/object_prev.hpp"
#include "stereo_seq/stereo_taa.hpp"
#include "vkcore/game_code.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/seq_hooks.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-objprev";

// The parameter setup (RVA 0x1C54A84): `mov rcx, rsi`, `call counter` (+0x3), `lea r8d, [rax + 2]` (+0x8),
// the ring index mod 3, `mov rdx, [rip + prevJointOffsetsBuffer]` (+0x26), `call set` (+0x42); then the
// same for prevModelMatricesBuffer: `call counter` (+0x4A), `lea` (+0x4F), `mov rdx, [rip + param]` (+0x6D).
constexpr const char* kSignature =
    "48 8B CE E8 ?? ?? ?? ?? 44 8D 40 02 B8 56 55 55 55 41 F7 E8 49 8B 84 24 C0 9D 4D 00 8B CA C1 E9 1F "
    "03 D1 8D 0C 52 48 8B 15 ?? ?? ?? ?? 44 2B C1 49 8B CF 4D 63 C0 4E 8B 84 C0 20 86 C3 00 49 83 C0 18 "
    "E8 ?? ?? ?? ?? 48 8B CE E8 ?? ?? ?? ?? 44 8D 40 02 B8 56 55 55 55 41 F7 E8 49 8B 84 24 C0 9D 4D 00 "
    "8B CA C1 E9 1F 03 D1 8D 0C 52 48 8B 15 ?? ?? ?? ??";
constexpr std::size_t kCounterCalls[2] = {0x3, 0x4A};
constexpr std::size_t kHooks[2] = {0x8, 0x4F};
constexpr std::size_t kParamLoads[2] = {0x26, 0x6D};
constexpr std::string_view kParamNames[2] = {"prevJointOffsetsBuffer", "prevModelMatricesBuffer"};
// A shader parameter global holds the parameter object; the pointer 0x10 before it, the name.
constexpr std::size_t kParamName = 0x10;

std::once_flag g_once;
bool g_installed = false;

std::mutex g_mutex;
stereo_seq::ObjectPrevSlot g_slot;

struct Counters {
    std::atomic<std::uint64_t> moved{0}; // picks moved to eye R's own render two back
    std::atomic<std::uint64_t> kept{0};  // eye R picks left as they were (no eye R render two back)
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
    EVR_LOG("%s: eye R's previous-frame object buffers: %llu from its own render, %llu left as they were",
            kTag, static_cast<unsigned long long>(g_counters.moved.exchange(0)),
            static_cast<unsigned long long>(g_counters.kept.exchange(0)));
}

// On `lea r8d, [rax + 2]`: rax is the render counter the engine picks the previous frame's buffer with.
void onPrevPick(HookRegisters& r) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    const std::optional<stereo_seq::RenderTag> tag = seqTagInFlight();
    if (!tag || tag->eye != stereo_seq::Eye::Right) {
        return;
    }
    report();
    const auto counter = static_cast<std::uint32_t>(r.rax);
    std::uint32_t pick = counter;
    {
        std::lock_guard lock(g_mutex);
        pick = g_slot.counterFor(tag->eye, counter);
    }
    if (pick == counter) {
        ++g_counters.kept;
        return;
    }
    r.rax = (r.rax & ~std::uintptr_t{0xFFFFFFFF}) | pick;
    ++g_counters.moved;
}

} // namespace

bool installObjectPrevHooks() {
    std::call_once(g_once, [] {
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
        const std::byte* site = findUnique(image, kTag, "previous-frame object buffers", kSignature);
        if (!site) {
            EVR_LOG("%s: not installed; eye R's moving objects have no motion", kTag);
            return;
        }
        const std::byte* counter = relativeTarget(site + kCounterCalls[0]);
        bool ok = counter && image.inText(counter) && relativeTarget(site + kCounterCalls[1]) == counter;
        for (int i = 0; i < 2 && ok; ++i) {
            const std::byte* global = ripTarget(image, site + kParamLoads[i] + 3, site + kParamLoads[i] + 7);
            const std::byte* name = nullptr;
            if (global && image.contains(global - kParamName, sizeof(name))) {
                std::memcpy(&name, global - kParamName, sizeof(name));
            }
            ok = name && stringAt(image, name) == kParamNames[i];
        }
        if (!ok) {
            EVR_LOG("%s: the counter call or the parameter names did not check out; not installed", kTag);
            return;
        }
        std::string error;
        for (int i = 0; i < 2; ++i) {
            if (!installMidHookEdit(const_cast<std::byte*>(site + kHooks[i]), &onPrevPick, error)) {
                // The first hook alone moves only the joints: still right for eye R, and logged.
                EVR_LOG("%s: hook %d failed: %s", kTag, i, error.c_str());
                g_installed = i > 0;
                return;
            }
        }
        g_installed = true;
        EVR_LOG(
            "%s: previous-frame object buffers hooked (RVA 0x%X, 0x%X): eye R reads its own render of the "
            "tick before",
            kTag, image.rva(site + kHooks[0]), image.rva(site + kHooks[1]));
    });
    return g_installed;
}

} // namespace evr::vkcore
