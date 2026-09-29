#include "vkcore/world_gui_hooks.hpp"

#include "stereo_seq/world_gui.hpp"
#include "vkcore/game_build.hpp"
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

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-worldgui";

// The GUI-surface check 0x1C75090 (signature at RVA 0x1C750CD, 0x3D into it): `mov rbp, [rdx + 0x28]`,
// `mov rbx, [rbp + 0xA8]`, `mov rbx, [rbx + rax * 8]`, `bt rbx, 0x1F` (+0xF, bit 31: the surfaces are in
// the per-frame GUI buffer), `jae`, `test r8, r8`, `je`, `mov eax, [r8 + 0xBFD8]` (+0x1B, the world's frame
// number; r8 is the world, 0 for the screen's own GUIs), `cmp [rdx + 0xC], eax` (+0x22, the commit stamp),
// `jne skip` (+0x25).
constexpr const char* kCheckSignature =
    "48 8B 6A 28 48 8B 9D A8 00 00 00 48 8B 1C C3 48 0F BA E3 1F 73 15 4D 85 C0 74 10 41 8B 80 D8 BF 00 00 "
    "39 42 0C 0F 85 ?? ?? ?? ??";
constexpr std::size_t kCheckFunction = 0x3D; // the signature's offset into the function
constexpr std::size_t kProbe = 0xF;
constexpr std::size_t kHook = 0x22;
constexpr std::size_t kStamp = 0xC;
constexpr std::size_t kFrameNumber = 0xBFD8;
// At both hook points the function has pushed two registers and reserved 0xA8 bytes: its return address.
constexpr std::size_t kReturnAddress = 0xB8;
// The world-surface callback's call of the check (Steam RVA 0x1C74E03, returning to 0x1C74E08; per build in
// game_build.hpp): the draws of GUIs on world surfaces.

// The world's commit of a model (0x18D9FA0; signature at RVA 0x18DA7D2): `mov ebx, [rax + 0xBFD8]`, the
// model's last commit frame (+0x4A0) compared and set, `mov [rcx + 0xC], ebx` (the committed data's stamp),
// then `mov rcx, [rdi + 0xA8]` (+0x2B), where rdi is the model and rcx its committed data.
constexpr const char* kCommitSignature =
    "48 8B 8F A8 00 00 00 48 8B 41 20 48 85 C0 74 18 8B 98 D8 BF 00 00 39 9F A0 04 00 00 0F 84 ?? ?? ?? ?? "
    "89 9F A0 04 00 00 89 59 0C 48 8B 8F A8 00 00 00 48 8B 41 28";
constexpr std::size_t kCommitHook = 0x2B;

std::once_flag g_once;
bool g_installed = false;
stereo_seq::WorldGuiMode g_mode = stereo_seq::WorldGuiMode::On;
std::uintptr_t g_worldCallReturn = 0;
std::uintptr_t g_guiModelVtable = 0;

// Rows: mono, eye L, eye R.
struct Counters {
    // World GUI draws: [persistent buffer, per-frame current, per-frame one behind, per-frame older].
    std::atomic<std::uint64_t> world[3][4]{};
    std::atomic<std::uint64_t> screen[3]{};  // the screen's own GUIs and ray-traced layers
    std::atomic<std::uint64_t> moved{0};     // eye R world GUI draws given eye L's commit
    std::atomic<std::uint64_t> commits[3]{}; // GUI model commits per chain eye
    std::atomic<std::uint64_t> lastReport{0};
} g_counters;

int counterRow(stereo_seq::Eye eye) {
    switch (eye) {
    case stereo_seq::Eye::Left:
        return 1;
    case stereo_seq::Eye::Right:
        return 2;
    default:
        return 0;
    }
}

// The check runs in the backend frame's draw jobs (0x1C74D00 is called from the pass jobs), before that
// frame's present, where the tag in flight is the render's own; no copy of the engine's counter read is at
// hand here. A wrong eye would show in the counts as eye L draws one behind or eye R draws current.
stereo_seq::Eye backendEye() {
    const std::optional<stereo_seq::RenderTag> tag = seqTagInFlight();
    return tag ? tag->eye : stereo_seq::Eye::Mono;
}

stereo_seq::WorldGuiMode requestedMode() {
    std::wstring value;
    std::string narrow;
    if (readEnv(L"ETERNALVR_STEREO_WORLD_GUI", value)) {
        for (const wchar_t c : value) {
            narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
    }
    return stereo_seq::worldGuiMode(narrow);
}

std::uint32_t readU32(std::uintptr_t at) {
    std::uint32_t v = 0;
    std::memcpy(&v, reinterpret_cast<const void*>(at), sizeof(v));
    return v;
}

std::uintptr_t readPtr(std::uintptr_t at) {
    std::uintptr_t v = 0;
    std::memcpy(&v, reinterpret_cast<const void*>(at), sizeof(v));
    return v;
}

void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counters.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_counters.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    auto w = [](int eye, int kind) {
        return static_cast<unsigned long long>(g_counters.world[eye][kind].exchange(0));
    };
    auto s = [](int eye) {
        return static_cast<unsigned long long>(g_counters.screen[eye].exchange(0));
    };
    auto c = [](int eye) {
        return static_cast<unsigned long long>(g_counters.commits[eye].exchange(0));
    };
    EVR_LOG("%s: world GUI draws (persistent / per-frame current / one behind / older): eye L %llu / %llu / "
            "%llu / "
            "%llu, eye R %llu / %llu / %llu / %llu, mono %llu / %llu / %llu / %llu; %llu eye R draw(s) given "
            "eye L's "
            "commit; screen GUI draws L %llu R %llu mono %llu; GUI model commits L %llu R %llu mono %llu",
            kTag, w(1, 0), w(1, 1), w(1, 2), w(1, 3), w(2, 0), w(2, 1), w(2, 2), w(2, 3), w(0, 0), w(0, 1),
            w(0, 2), w(0, 3), static_cast<unsigned long long>(g_counters.moved.exchange(0)), s(1), s(2), s(0),
            c(1), c(2), c(0));
}

// On `bt rbx, 0x1F`: counts every GUI surface the check sees, by caller and eye.
void onProbe(const HookRegisters& r) {
    const int row = counterRow(backendEye());
    if (readPtr(r.rsp + kReturnAddress) != g_worldCallReturn) {
        ++g_counters.screen[row];
    } else if (((r.rbx >> 31) & 1u) == 0 || r.r8 == 0 || r.rdx == 0) {
        ++g_counters.world[row][0];
    } else {
        const auto stamp =
            stereo_seq::classifyWorldGuiStamp(readU32(r.rdx + kStamp), readU32(r.r8 + kFrameNumber));
        ++g_counters.world[row][1 + static_cast<int>(stamp)];
    }
    report();
}

// On `cmp [rdx + 0xC], eax`, reached only for a world GUI's per-frame surfaces: eax is the world's frame
// number, rdx the GUI's committed data.
void onStampCheck(HookRegisters& r) {
    if (!mp_guard::allowsGameTouch() || r.rdx == 0 || readPtr(r.rsp + kReturnAddress) != g_worldCallReturn) {
        return;
    }
    const auto current = static_cast<std::uint32_t>(r.rax);
    const std::uint32_t frame =
        stereo_seq::worldGuiFrameFor(g_mode, backendEye(), readU32(r.rdx + kStamp), current);
    if (frame != current) {
        r.rax = (r.rax & ~std::uintptr_t{0xFFFFFFFF}) | frame;
        ++g_counters.moved;
    }
}

// After the world stamps a model's commit: counts GUI models committed per chain eye.
void onCommit(const HookRegisters& r) {
    if (r.rdi != 0 && readPtr(r.rdi) == g_guiModelVtable) {
        ++g_counters.commits[counterRow(seqChainEye())];
    }
}

} // namespace

bool installWorldGuiHook() {
    std::call_once(g_once, [] {
        g_mode = requestedMode();
        if (g_mode == stereo_seq::WorldGuiMode::Off) {
            EVR_LOG("%s: off (ETERNALVR_STEREO_WORLD_GUI=0): world GUIs show in eye L only", kTag);
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
        const std::byte* site = findUnique(image, kTag, "world GUI stamp check", kCheckSignature);
        if (!site) {
            EVR_LOG("%s: not installed; world GUIs show in eye L only", kTag);
            return;
        }
        // The world-surface callback must call this check right before its return address.
        const GameBuild* build = currentGameBuild();
        if (!build) {
            EVR_LOG("%s: an unknown game build (no world-surface call site for it); not installed", kTag);
            return;
        }
        const std::byte* worldReturn = image.base + build->worldGuiCallReturn;
        if (!image.inText(worldReturn - 5, 5) || static_cast<std::uint8_t>(*(worldReturn - 5)) != 0xE8 ||
            relativeTarget(worldReturn - 5) != site - kCheckFunction) {
            EVR_LOG("%s: the world-surface call of the check is not at RVA 0x%X (%s); not installed", kTag,
                    build->worldGuiCallReturn, build->name);
            return;
        }
        g_worldCallReturn = reinterpret_cast<std::uintptr_t>(worldReturn);
        std::string error;
        if (!installMidHook(const_cast<std::byte*>(site + kProbe), &onProbe, error)) {
            EVR_LOG("%s: probe hook failed: %s", kTag, error.c_str());
        }
        if (!installMidHookEdit(const_cast<std::byte*>(site + kHook), &onStampCheck, error)) {
            EVR_LOG("%s: hook failed: %s", kTag, error.c_str());
            return;
        }
        g_installed = true;
        EVR_LOG("%s: world GUI stamp check hooked (RVA 0x%X): %s", kTag, image.rva(site + kHook),
                g_mode == stereo_seq::WorldGuiMode::Count
                    ? "counting only (ETERNALVR_STEREO_WORLD_GUI=count); world GUIs show in eye L only"
                    : "eye R draws the world GUIs eye L committed in the same tick");
        // The commit count is a diagnostic: its absence changes nothing.
        const std::byte* vtable = image.base + build->guiModelVtable;
        const std::byte* commit = findUnique(image, kTag, "model commit stamp", kCommitSignature);
        if (commit && image.contains(vtable, sizeof(void*))) {
            g_guiModelVtable = reinterpret_cast<std::uintptr_t>(vtable);
            if (installMidHook(const_cast<std::byte*>(commit + kCommitHook), &onCommit, error)) {
                EVR_LOG("%s: GUI model commits counted (RVA 0x%X)", kTag, image.rva(commit + kCommitHook));
            } else {
                EVR_LOG("%s: commit probe failed: %s", kTag, error.c_str());
            }
        }
    });
    return g_installed;
}

} // namespace evr::vkcore
