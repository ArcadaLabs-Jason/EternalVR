#include "vkcore/keep_prev_hooks.hpp"

#include "stereo_seq/keep_prev.hpp"
#include "vkcore/game_code.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/taa_hooks.hpp"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-prev";

// 0x1C8AE60: `movups xmm0, [rsi]` (the current matrix), `mov r9, rsi`, `add rcx, [rdx + 0x108]`, `movups
// [rdi + r14], xmm0` (the previous one); rbx is the entity's index, rdi its index * 64, r14 the previous
// matrices.
constexpr const char* kCopySignature = "0F 10 06 4C 8B CE 48 03 8A 08 01 00 00 42 0F 11 04 37";
// 0x1C8AEA7: `call` (the new current matrix), `test byte [r13 + 0x10], 1`, `jne`, then the second copy.
constexpr const char* kAfterCallSignature = "E8 ?? ?? ?? ?? 41 F6 45 10 01 75 26 0F 10 06 42 0F 11 04 37";
constexpr std::size_t kAfterCallHook = 5;
constexpr std::ptrdiff_t kCallFromCopy = 0x47; // 0x1C8AEA7 - 0x1C8AE60
constexpr std::size_t kMatrixBytes = 64;
// Entities with a larger index keep the engine's behaviour.
constexpr std::size_t kMaxEntities = std::size_t{1} << 20;

std::once_flag g_once;
bool g_installed = false;
stereo_seq::KeepPrevMode g_mode = stereo_seq::KeepPrevMode::On;
stereo_seq::PairClock g_clock;
std::unique_ptr<std::atomic<std::uint32_t>[]> g_leftStamps;

// The previous matrix saved between the two hooks, on the thread running the commit.
struct Saved {
    std::byte* at = nullptr;
    std::byte matrix[kMatrixBytes];
};
thread_local Saved t_saved;

// Rows: eye L (and mono), eye R.
struct Counters {
    std::atomic<std::uint64_t> copies[2]{}; // the commit's previous = current copies
    std::atomic<std::uint64_t> sameTick{0}; // eye R copies of entities eye L committed in the same tick
    std::atomic<std::uint64_t> kept{0};     // previous matrices put back
    std::atomic<std::uint64_t> lastReport{0};
} g_counters;

stereo_seq::KeepPrevMode requestedMode() {
    std::wstring value;
    std::string narrow;
    if (readEnv(L"ETERNALVR_STEREO_KEEP_PREV", value)) {
        for (const wchar_t c : value) {
            narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
    }
    return stereo_seq::keepPrevMode(narrow);
}

void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counters.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_counters.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    EVR_LOG(
        "%s: previous-matrix copies L %llu R %llu; eye R copies of entities eye L committed this tick %llu, "
        "previous kept %llu",
        kTag, static_cast<unsigned long long>(g_counters.copies[0].exchange(0)),
        static_cast<unsigned long long>(g_counters.copies[1].exchange(0)),
        static_cast<unsigned long long>(g_counters.sameTick.exchange(0)),
        static_cast<unsigned long long>(g_counters.kept.exchange(0)));
}

// Before the first copy: stamp (eye L) or save (eye R).
void onCopy(const HookRegisters& r) {
    t_saved.at = nullptr;
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    const std::size_t index = static_cast<std::uint32_t>(r.rbx);
    const stereo_seq::Eye eye = seqChainEye();
    const bool right = eye == stereo_seq::Eye::Right;
    ++g_counters.copies[right ? 1 : 0];
    report();
    if (index >= kMaxEntities) {
        return;
    }
    if (!right) {
        g_leftStamps[index].store(g_clock.leftStamp(), std::memory_order_relaxed);
        return;
    }
    const std::uint32_t pair = g_clock.rightPair(seqRightTick());
    const std::uint32_t stamp = g_leftStamps[index].load(std::memory_order_relaxed);
    if (stamp == pair && pair != 0) {
        ++g_counters.sameTick;
    }
    if (!stereo_seq::keepPrevious(g_mode, eye, stamp, pair)) {
        return;
    }
    auto* previous = reinterpret_cast<std::byte*>(r.r14 + r.rdi);
    std::memcpy(t_saved.matrix, previous, kMatrixBytes);
    t_saved.at = previous;
}

// After the new current matrix: put the saved previous back.
void onAfterCall(const HookRegisters&) {
    if (t_saved.at == nullptr) {
        return;
    }
    std::memcpy(t_saved.at, t_saved.matrix, kMatrixBytes);
    t_saved.at = nullptr;
    ++g_counters.kept;
}

} // namespace

bool installKeepPrevHooks() {
    std::call_once(g_once, [] {
        g_mode = requestedMode();
        if (g_mode == stereo_seq::KeepPrevMode::Off) {
            EVR_LOG(
                "%s: off (ETERNALVR_STEREO_KEEP_PREV=0): eye R's recommits reset the previous model matrix",
                kTag);
            return;
        }
        if (!mp_guard::allowsGameTouch()) {
            EVR_LOG("%s: the multiplayer guard is not armed; not installed", kTag);
            return;
        }
        if (!taaRequested()) {
            EVR_LOG("%s: not installed: no per-eye temporal history (anti-aliasing off)", kTag);
            return;
        }
        GameImage image;
        if (!locateGameImage(image, kTag)) {
            return;
        }
        const std::byte* copy = findUnique(image, kTag, "commit's previous-matrix copy", kCopySignature);
        const std::byte* call = findUnique(image, kTag, "commit's current-matrix call", kAfterCallSignature);
        if (!copy || !call || call - copy != kCallFromCopy) {
            EVR_LOG("%s: the commit's model-matrix step did not check out; not installed", kTag);
            return;
        }
        g_leftStamps = std::make_unique<std::atomic<std::uint32_t>[]>(kMaxEntities);
        std::string error;
        if (!installMidHook(const_cast<std::byte*>(copy), &onCopy, error)) {
            EVR_LOG("%s: copy hook failed: %s", kTag, error.c_str());
            return;
        }
        if (!installMidHook(const_cast<std::byte*>(call + kAfterCallHook), &onAfterCall, error)) {
            // The first hook only stamps and saves; with nothing to put back it changes nothing.
            EVR_LOG("%s: after-call hook failed: %s; counting only", kTag, error.c_str());
            g_mode = stereo_seq::KeepPrevMode::Count;
            return;
        }
        g_installed = true;
        EVR_LOG("%s: the commit's model-matrix step hooked (RVA 0x%X, 0x%X): %s", kTag, image.rva(copy),
                image.rva(call + kAfterCallHook),
                g_mode == stereo_seq::KeepPrevMode::Count
                    ? "counting only (ETERNALVR_STEREO_KEEP_PREV=count)"
                    : "eye R keeps eye L's previous model matrix for entities eye L committed this tick");
    });
    return g_installed;
}

} // namespace evr::vkcore
