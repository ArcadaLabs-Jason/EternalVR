#include "vkcore/moved_flag_hooks.hpp"

#include "stereo_seq/moved_flag.hpp"
#include "vkcore/game_code.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/taa_hooks.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-moved";

// The list builder 0x18DEDB0 (signature at RVA 0x18DF0AA): `movzx ecx, byte [r14 + rsi + 8]` (the status
// byte), `test cl, cl` (+0x6, the hook), `je skip` (+0x8, rel32 to 0x18DF548), ..., `cmp cl, 1`, `jne`,
// the entity's flags, `and qword [rcx + rdx * 8], ~2` (the moved flag cleared).
constexpr const char* kStatusSignature = "41 0F B6 4C 36 08 84 C9 0F 84 ?? ?? ?? ?? 48 8B 86 E0 70 02 00 45 "
                                         "0F B6 94 36 58 71 18 00 45 0F B6 CA 44 "
                                         "88 55 04 41 80 E1 01 4A 8B 3C F0 48 8B 86 10 71 0E 00 4A 8B 1C F0 "
                                         "80 F9 01 0F 85 ?? ?? ?? ?? 48 8B 43 28 "
                                         "8B 53 30 48 C1 EA 08 48 8B 88 A8 00 00 00 48 83 24 D1 FD";
constexpr std::size_t kStatusHook = 0x6;
constexpr std::size_t kSkipJump = 0x8;        // je rel32
constexpr std::ptrdiff_t kSkipTarget = 0x49E; // 0x18DF548 - 0x18DF0AA

// The surface rebuild's moved check in 0x1C8D050 (RVA 0x1C8D14D): `cmp byte [rbx], sil`, `je`; rbx is the
// rebuild's key, whose first byte is the moved flag.
constexpr const char* kRebuildSignature =
    "40 38 33 74 0C 48 8B 05 ?? ?? ?? ?? 8B 50 78 EB 02 8B D6 8B 4B 0C 0B 4B 10";

std::once_flag g_once;
bool g_installed = false;
stereo_seq::MovedFlagMode g_mode = stereo_seq::MovedFlagMode::On;

// Rows: mono, eye L, eye R.
struct Counters {
    std::atomic<std::uint64_t> cleanups[3]{};   // status-1 entities the list builder saw
    std::atomic<std::uint64_t> kept{0};         // eye R cleanups skipped
    std::atomic<std::uint64_t> rebuilt[3][2]{}; // surface rebuilds [not moved, moved]
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

stereo_seq::MovedFlagMode requestedMode() {
    std::wstring value;
    std::string narrow;
    if (readEnv(L"ETERNALVR_STEREO_MOVED", value)) {
        for (const wchar_t c : value) {
            narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
    }
    return stereo_seq::movedFlagMode(narrow);
}

void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counters.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_counters.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    auto c = [](int eye) {
        return static_cast<unsigned long long>(g_counters.cleanups[eye].exchange(0));
    };
    auto r = [](int eye, int moved) {
        return static_cast<unsigned long long>(g_counters.rebuilt[eye][moved].exchange(0));
    };
    EVR_LOG("%s: moved-flag cleanups seen L %llu R %llu mono %llu, %llu kept for eye R; surface rebuilds "
            "(not moved / moved) L %llu / %llu, R %llu / %llu, mono %llu / %llu",
            kTag, c(1), c(2), c(0), static_cast<unsigned long long>(g_counters.kept.exchange(0)), r(1, 0),
            r(1, 1), r(2, 0), r(2, 1), r(0, 0), r(0, 1));
}

// On `test cl, cl` after the status byte's load: cl is the status.
void onStatus(HookRegisters& r) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    const auto status = static_cast<std::uint8_t>(r.rcx & 0xFF);
    if (status != 1) {
        return;
    }
    const stereo_seq::Eye eye = seqChainEye();
    ++g_counters.cleanups[counterRow(eye)];
    report();
    const std::uint8_t act = stereo_seq::movedFlagStatusFor(g_mode, eye, status);
    if (act != status) {
        r.rcx = (r.rcx & ~std::uintptr_t{0xFF}) | act;
        ++g_counters.kept;
    }
}

// On `cmp byte [rbx], sil`: the first byte at rbx is the rebuild's moved flag.
void onRebuild(const HookRegisters& r) {
    if (r.rbx == 0) {
        return;
    }
    std::uint8_t moved = 0;
    std::memcpy(&moved, reinterpret_cast<const void*>(r.rbx), sizeof(moved));
    ++g_counters.rebuilt[counterRow(seqChainEye())][moved != 0 ? 1 : 0];
    report();
}

} // namespace

bool installMovedFlagHooks() {
    std::call_once(g_once, [] {
        g_mode = requestedMode();
        if (g_mode == stereo_seq::MovedFlagMode::Off) {
            EVR_LOG("%s: off (ETERNALVR_STEREO_MOVED=0): moving objects have no motion vectors in eye R",
                    kTag);
            return;
        }
        if (!mp_guard::allowsGameTouch()) {
            EVR_LOG("%s: the multiplayer guard is not armed; not installed", kTag);
            return;
        }
        // Motion vectors matter only to per-eye TAA or DLSS; keeping entities moved costs GPU time otherwise.
        if (!taaRequested()) {
            EVR_LOG("%s: not installed: no per-eye temporal history (anti-aliasing off)", kTag);
            return;
        }
        GameImage image;
        if (!locateGameImage(image, kTag)) {
            return;
        }
        const std::byte* site =
            findUnique(image, kTag, "world list builder's status check", kStatusSignature);
        if (!site) {
            EVR_LOG("%s: not installed; moving objects have no motion vectors in eye R", kTag);
            return;
        }
        const std::byte* skip = site + kSkipJump + 6 + readI32(site + kSkipJump + 2);
        if (skip != site + kSkipTarget) {
            EVR_LOG("%s: the status check's skip branch did not check out; not installed", kTag);
            return;
        }
        std::string error;
        if (!installMidHookEdit(const_cast<std::byte*>(site + kStatusHook), &onStatus, error)) {
            EVR_LOG("%s: hook failed: %s", kTag, error.c_str());
            return;
        }
        g_installed = true;
        EVR_LOG("%s: world list builder's status check hooked (RVA 0x%X): %s", kTag,
                image.rva(site + kStatusHook),
                g_mode == stereo_seq::MovedFlagMode::Count
                    ? "counting only (ETERNALVR_STEREO_MOVED=count); moving objects have no motion vectors "
                      "in eye R"
                    : "eye R keeps the moved flag, so moving objects write motion vectors in both eyes");
        // The rebuild count is a diagnostic: its absence changes nothing.
        if (const std::byte* rebuild =
                findUnique(image, kTag, "surface rebuild's moved check", kRebuildSignature)) {
            if (installMidHook(const_cast<std::byte*>(rebuild), &onRebuild, error)) {
                EVR_LOG("%s: surface rebuilds counted (RVA 0x%X)", kTag, image.rva(rebuild));
            } else {
                EVR_LOG("%s: rebuild probe failed: %s", kTag, error.c_str());
            }
        }
    });
    return g_installed;
}

} // namespace evr::vkcore
