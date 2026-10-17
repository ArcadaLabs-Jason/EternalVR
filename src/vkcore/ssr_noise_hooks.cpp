#include "vkcore/ssr_noise_hooks.hpp"

#include "stereo_seq/ssr_noise.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/taa_hooks.hpp"

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

constexpr const char* kTag = "seq-ssr";

// SSR's parameter setup (RVA 0x1C72503; rdi is the render-view job's block, render context + 0x6A7E20, handed
// down from 0x1C56AF7 through 0x1C71630): `movzx ecx, byte [rdi + 0xB0]` (the low byte of the backend counter
// the render-view job read for its render, stored at 0x1C56A19), `and cl, 0xF` (+0x7, the hook), the four
// bits reversed, `movzx ecx, al`, `jmp`, `mov ecx, ebx` (the path without noise: 0), `mov rax, [rip + ...]`,
// `test r9d, r9d`, `mov rdx, [rip + SSRParms]` (+0x3B, its disp32 at +0x3E), `xorps xmm2, xmm2`, `movd xmm0,
// ecx` (the index SSRParms.w gets).
constexpr const char* kIndexSignature =
    "0F B6 8F B0 00 00 00 80 E1 0F 0F B6 D1 0F B6 C1 D0 EA 02 C0 32 D0 02 C9 80 E2 55 32 D1 0F B6 C2 C0 EA "
    "02 24 F3 C0 E0 02 0A C2 0F B6 C8 EB 02 8B CB 48 8B 05 ?? ?? ?? ?? 45 85 C9 48 8B 15 ?? ?? ?? ?? 0F 57 "
    "D2 66 0F 6E C1";
constexpr std::size_t kCounterHook = 0x7;
constexpr std::size_t kParmDisp = 0x3E;
constexpr std::size_t kParmEnd = 0x42;
constexpr std::size_t kJobCounter = 0xB0; // the render-view job's stored counter (as ssdo_hooks.cpp reads it)
// The parameter's declaration in .data: its name, then its handle 0x10 on (the handle is what `mov rdx`
// reads).
constexpr std::size_t kParmNameBefore = 0x10;
constexpr std::string_view kParmName = "SSRParms";
constexpr const char* kEngineIndex = "each eye keeps the engine's index (one eye only the even counters, the "
                                     "other only the odd ones: half of the sequence each)";
// The renders given the tick's counter after which the session's single count line is written (about 60 s of
// stereo play at 90 Hz).
constexpr std::uint64_t kReportAt = 10800;

std::once_flag g_once;
bool g_installed = false;
std::atomic<bool> g_live{false};

// By eyeIndex (eye L and mono, eye R): renders given the tick's counter, and renders left with the engine's
// (mono, untagged, no view applied, no per-eye history).
std::atomic<std::uint64_t> g_replaced[2]{};
std::atomic<std::uint64_t> g_left[2]{};
std::atomic<std::uint64_t> g_replacedTotal{0};

// After the counter's low byte is read into cl (rdi the render-view job's block).
void onCounter(HookRegisters& r) {
    if (!g_live.load(std::memory_order_acquire) || !mp_guard::allowsGameTouch()) {
        return;
    }
    std::uint32_t counter = 0;
    std::memcpy(&counter, reinterpret_cast<const std::byte*>(r.rdi) + kJobCounter, sizeof(counter));
    // The render's own tag, by the counter the render-view job read for it (seq_hooks.hpp).
    const std::optional<stereo_seq::RenderTag> tag = seqTagForBackendFrame(counter + 1u);
    const std::uint32_t given =
        stereo_seq::ssrCounterFor(tag, seqAlternateEyes(), taaPerEyeActive(), counter);
    const int eye = stereo_seq::eyeIndex(tag ? tag->eye : stereo_seq::Eye::Mono);
    if (given == counter) { // the engine's (a tick's counter is below 16, the backend counter far above)
        ++g_left[eye];
        return;
    }
    // The engine reverses the low four bits next.
    r.rcx = (r.rcx & ~std::uintptr_t{0xFF}) | (given & 0xFFu);
    ++g_replaced[eye];
    if (g_replacedTotal.fetch_add(1) + 1 == kReportAt) {
        // Exactly one render reaches the count: one line per session.
        EVR_LOG("%s: SSR noise index of the tick given to %llu eye L / %llu eye R render(s); the engine's "
                "kept for %llu eye L or mono / %llu eye R render(s)",
                kTag, static_cast<unsigned long long>(g_replaced[0].load()),
                static_cast<unsigned long long>(g_replaced[1].load()),
                static_cast<unsigned long long>(g_left[0].load()),
                static_cast<unsigned long long>(g_left[1].load()));
    }
}

} // namespace

bool installSsrNoiseHooks() {
    std::call_once(g_once, [] {
        if (!mp_guard::allowsGameTouch()) {
            EVR_LOG("%s: the multiplayer guard is not armed; not installed", kTag);
            return;
        }
        GameImage image;
        if (!locateGameImage(image, kTag)) {
            return;
        }
        const std::byte* site = findUnique(image, kTag, "SSR noise index", kIndexSignature);
        const std::byte* handle = site ? ripTarget(image, site + kParmDisp, site + kParmEnd) : nullptr;
        std::string_view name;
        if (handle && handle - image.base >= static_cast<std::ptrdiff_t>(kParmNameBefore) &&
            image.contains(handle - kParmNameBefore, sizeof(std::uintptr_t))) {
            std::uintptr_t namePointer = 0;
            std::memcpy(&namePointer, handle - kParmNameBefore, sizeof(namePointer));
            name = stringAt(image, reinterpret_cast<const std::byte*>(namePointer), 32);
        }
        if (name != kParmName) {
            EVR_LOG("%s: the SSR noise index or its parameter (%s) not found; not installed: %s", kTag,
                    site ? "named otherwise" : "no unique match", kEngineIndex);
            return;
        }
        std::string error;
        if (!installMidHookEdit(const_cast<std::byte*>(site + kCounterHook), &onCounter, error)) {
            EVR_LOG("%s: SSR noise index hook failed: %s; not installed: %s", kTag, error.c_str(),
                    kEngineIndex);
            return;
        }
        g_installed = true;
        g_live.store(true, std::memory_order_release);
        EVR_LOG(
            "%s: SSR noise index (%.*s.w) hooked at RVA 0x%X: with per-eye history both eyes of a tick take "
            "the tick's index, and the ticks walk all 16 as in flat play",
            kTag, static_cast<int>(name.size()), name.data(), image.rva(site + kCounterHook));
    });
    return g_installed;
}

} // namespace evr::vkcore
