#include "vkcore/fx_rt_mask.hpp"

#include "stereo_seq/eye_tags.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/seq_hooks.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace evr::vkcore::fx_rt {

namespace {

using stereo_seq::FxAction;

// The job 0x18DEAF0 (rcx its arguments: [rcx] the world, rdi from +0xA): rbx = the world's ray tracing scene
// `[rdi + 0x71DAE0]` (+0x11), counters of the scene to 0, then (+0x6B) `movsxd r8, [rbx + 0x2A4]` (the mask's
// qwords), `mov rcx, [rbx + 0x298]` (the mask), `shl r8, 3`, `call` memset (+0x7D, the hook; rdx is 0) and
// (+0x82, where eye R resumes) `mov rcx, [rdi + 0x71DBD0]` (the GPU particle manager) before the tail jump to
// the manager's reset (0x1C28320). Nothing after the call reads rax; memset changes only volatile registers.
constexpr const char* kClearSignature = "48 89 5C 24 10 57 48 83 EC 20 48 8B 39 48 8B 51 08 48 8B 9F E0 DA "
                                        "71 00 48 85 D2 74 19 48 8B 92 D0 89 02 "
                                        "00 48 8D 4C 24 30 E8 ?? ?? ?? ?? 8B 08 89 8B DC BF 00 00 33 C0 33 "
                                        "D2 48 89 83 50 86 C3 00 48 89 83 78 86 "
                                        "C3 00 48 89 83 C8 26 00 00 48 89 83 58 86 C3 00 48 89 83 60 86 C3 "
                                        "00 48 89 83 68 86 C3 00 48 89 83 70 86 "
                                        "C3 00 4C 63 83 A4 02 00 00 48 8B 8B 98 02 00 00 49 C1 E0 03 E8 ?? "
                                        "?? ?? ?? 48 8B 8F D0 DB 71 00 48 8B 5C "
                                        "24 38 48 83 C4 20 5F E9 ?? ?? ?? ??";
constexpr std::size_t kClearCall = 0x7D;
constexpr std::size_t kClearResume = 0x82;
// memset 0x288D7C0: the byte widened to eight (`movabs r9, 0x0101010101010101`, `imul r9, rdx`).
constexpr const char* kMemsetCode = "4C 8B D9 0F B6 D2 49 B9 01 01 01 01 01 01 01 01 4C 0F AF CA";

// The particle bind 0x1955670 (r14 the model): the scene `rdi = [[r14 + 0x140]] -> +0x240` (+0xAC), then
// after the stage loop the ray tracing gate `cmp byte [rip + ...], 1` (+0x461) and, with stage records (`test
// eax, eax`, +0x4A4), the model's bit (index from 0x18DC5A0) `lock or [[rdi + 0x298] + i / 64 * 8]` (+0x4C3).
constexpr std::size_t kBindScene = 0xAC;
constexpr const char* kBindSceneCode = "49 8B 8E 40 01 00 00 48 8B 01 FF 90 40 02 00 00";
constexpr std::size_t kBindGate = 0x461;
constexpr const char* kBindGateCode = "80 3D ?? ?? ?? ?? 01";
constexpr std::size_t kBindSet = 0x4A4;
constexpr const char* kBindSetCode =
    "85 C0 7E 2D 8B C2 99 83 E2 3F 03 C2 44 8B C0 83 E0 3F 2B C2 41 C1 F8 06 8B C8 "
    "BA 01 00 00 00 48 8B 87 98 02 00 00 48 D3 E2 49 63 C8 F0 48 09 14 C8";
// The effect update 0x19511E0 (r15 the effect), in its bind: the gate, the scene `rbx = [[r15 + 0x140]] ->
// +0x240` and the effect's bit `lock or [[rbx + 0x298] + i / 64 * 8]` (+0x41F .. +0x472).
constexpr std::size_t kEffectSet = 0x41F;
constexpr const char* kEffectSetCode = "80 3D ?? ?? ?? ?? 01 75 4A 85 C0 7E 46 49 8B 8F 40 01 00 00 48 8B 01 "
                                       "FF 90 40 02 00 00 49 8B CF 48 8B D8 "
                                       "E8 ?? ?? ?? ?? 99 83 E2 3F 03 C2 44 8B C0 83 E0 3F 2B C2 41 C1 F8 06 "
                                       "8B C8 BA 01 00 00 00 48 8B 83 98 02 "
                                       "00 00 48 D3 E2 49 63 C8 F0 48 09 14 C8";

constexpr std::size_t kWorldRing = 0x25060; // the particle vertex ring (fx_sync_hooks.cpp)

RenderAction g_action = nullptr;
std::uintptr_t g_resume = 0;
bool g_installed = false;
unsigned g_rva = 0;

struct Counts {
    std::atomic<std::uint64_t> kept{0};      // eye R renders that kept eye L's mask (or would, counting)
    std::atomic<std::uint64_t> cleared[2]{}; // the engine's clears, eye L (and mono) / eye R
} g_counts;

bool fail(const char* tag, const char* what) {
    EVR_LOG("%s: %s did not check out; particles may be missing from eye R's ray-traced reflections", tag,
            what);
    return false;
}

// On the job's `call` of memset: rdi is the world, rcx the mask.
void onClear(HookRegisters& r) {
    std::uintptr_t ring = 0;
    std::memcpy(&ring, reinterpret_cast<const void*>(r.rdi + kWorldRing), sizeof(ring));
    const FxAction clear = stereo_seq::fxRtMaskClearFor(g_action(), ring != 0);
    if (clear == FxAction::UseEyeL) {
        ++g_counts.kept;
        r.resumeAt = g_resume;
        return;
    }
    if (clear == FxAction::RunCounted) {
        ++g_counts.kept;
    }
    ++g_counts.cleared[stereo_seq::eyeIndex(seqChainEye())];
}

} // namespace

bool install(const GameImage& image,
             const std::byte* particleBind,
             const std::byte* effectUpdate,
             RenderAction action,
             const char* tag) {
    const std::byte* clear = findUnique(image, tag, "ray-traced particle mask's clear", kClearSignature);
    if (!clear) {
        return fail(tag, "the ray-traced particle mask's clear (0x18DEAF0 on Steam)");
    }
    if (!matchesAt(image, branchTarget(image, clear + kClearCall), kMemsetCode)) {
        return fail(tag, "the mask clear's memset (0x288D7C0 on Steam)");
    }
    if (!particleBind || !effectUpdate || !matchesAt(image, particleBind + kBindScene, kBindSceneCode) ||
        !matchesAt(image, particleBind + kBindGate, kBindGateCode) ||
        !matchesAt(image, particleBind + kBindSet, kBindSetCode) ||
        !matchesAt(image, effectUpdate + kEffectSet, kEffectSetCode)) {
        return fail(tag, "the binds' ray-traced particle mask writes (RVA 0x1955B40 and 0x195164D on Steam)");
    }
    g_action = action;
    g_resume = reinterpret_cast<std::uintptr_t>(clear + kClearResume);
    std::string error;
    if (!installMidHookEdit(const_cast<std::byte*>(clear + kClearCall), &onClear, error)) {
        EVR_LOG(
            "%s: ray-traced particle mask hook (RVA 0x%X) failed: %s; particles may be missing from eye R's "
            "ray-traced reflections",
            tag, image.rva(clear + kClearCall), error.c_str());
        return false;
    }
    g_rva = image.rva(clear + kClearCall);
    g_installed = true;
    return true;
}

unsigned foundRva() {
    return g_rva;
}

std::string takeReadout() {
    if (!g_installed) {
        return {};
    }
    char text[128];
    std::snprintf(text, sizeof(text),
                  "; ray-traced particle mask kept by eye R %llu, cleared eye L %llu / eye R %llu",
                  static_cast<unsigned long long>(g_counts.kept.exchange(0)),
                  static_cast<unsigned long long>(g_counts.cleared[0].exchange(0)),
                  static_cast<unsigned long long>(g_counts.cleared[1].exchange(0)));
    return text;
}

} // namespace evr::vkcore::fx_rt
