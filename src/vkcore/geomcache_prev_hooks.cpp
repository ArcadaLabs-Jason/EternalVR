#include "vkcore/geomcache_prev_hooks.hpp"

#include "stereo_seq/geomcache_prev.hpp"
#include "vkcore/game_code.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/seh_filter.hpp"
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

constexpr const char* kTag = "seq-geomcache";

// The commit 0x1944950, transform slot flip (RVA 0x1944B19): `mov ecx, [rdi + 0x534]`, `inc ecx`, `mov [rsp +
// 0x58], rax`, `and ecx, 0x80000001` and its sign fix-up (ecx = (slot + 1) mod 2), `cmp byte [rbp + 0x20], 0`
// (the cache animates, its byte +0x504), `mov [rdi + 0x534], ecx` (+0x20, the hook), `jne`. The hook sits
// between the `cmp` and the `jne`: the mid hook's stub saves and restores the flags around the callback.
constexpr const char* kTransformFlipSignature =
    "8B 8F 34 05 00 00 FF C1 48 89 44 24 58 81 E1 01 00 00 80 7D 07 "
    "FF C9 83 C9 FE FF C1 80 7D 20 00 89 8F 34 05 00 00 75";
constexpr std::size_t kTransformFlipHook = 0x20;
// The position slot flip (RVA 0x1944EA8): `mov eax, [rdi + 0x530]`, `inc eax`, `and eax, 0x80000001`, the
// fix-up, `mov [rdi + 0x530], eax` (+0x16, the hook), `cmp dl, 1`. Every path from the first flip to the
// update passes it; the abort paths leave before it.
constexpr const char* kPositionFlipSignature =
    "8B 87 30 05 00 00 FF C0 25 01 00 00 80 7D 07 FF C8 83 C8 FE FF C0 89 87 30 05 00 00 80 FA 01";
constexpr std::size_t kPositionFlipHook = 0x16;
constexpr std::ptrdiff_t kPositionFromTransform = 0x38F; // 0x1944EA8 - 0x1944B19
// The update 0x1949350, its previous-frame check (RVA 0x194953F): `lea rcx, [rip + render system]`, `call
// counter` (+0x7, 0x1CBB2D0), `mov r13d, eax`, `lea ecx, [rax - 1]`, `cmp [rsi + 0x538], ecx` (the cache's
// last update was the render just before), `jne`, `cmp byte [rsi + 0x504], 1` (it animates), `jne`, `mov
// bpl, 1`, `jmp`, `xor bpl, bpl`, `mov r14, [rsp + 0xF0]` (+0x2B, the hook). rsi is the cache, bpl the
// validity both pickers (0x194A1A0, 0x194A7E0) are handed.
constexpr const char* kValiditySignature =
    "48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 44 8B E8 8D 48 FF 39 8E 38 05 00 00 "
    "75 0E 80 BE 04 05 00 00 01 75 05 40 B5 01 EB 03 40 32 ED 4C 8B B4 24 "
    "F0 00 00 00";
constexpr std::size_t kValidityCall = 0x7;
constexpr std::size_t kValidityHook = 0x2B;
constexpr std::size_t kRenderSystemLea = 0x3; // the disp32 of `lea rcx, [rip + render system]`
// The counter (0x1CBB2D0): `mov rax, [rcx + 0xF58]`, `test rax, rax`, `je`, `mov eax, [rax + 0xB0]` (the
// backend frame counter), `ret`, `mov eax, [rcx + 0x10]` (the render system's render frame counter), `ret`.
constexpr unsigned char kCounterCode[] = {0x48, 0x8B, 0x81, 0x58, 0x0F, 0x00, 0x00, 0x48,
                                          0x85, 0xC0, 0x74, 0x07, 0x8B, 0x80, 0xB0, 0x00,
                                          0x00, 0x00, 0xC3, 0x8B, 0x41, 0x10, 0xC3};
// The render system's render frame counter (0x66E2C30 + 0x10): one up per render, in the render-frame job
// 0x1CB9EE0 before the world-views pass runs the world's jobs, so it holds still through a render's commits.
constexpr std::size_t kRenderFrame = 0x10;

// The cache (idRenderModelGeomCache).
constexpr std::size_t kRenderEntity = 0xA8; // its render entity; the entity's handle at + 0x30, index << 8
constexpr std::size_t kEntityHandle = 0x30;
constexpr std::size_t kAnimates = 0x504;     // byte
constexpr std::size_t kPositionSlot = 0x530; // int, 0 or 1
constexpr std::size_t kTransformSlot = 0x534;
// Caches whose entity index is larger keep the engine's behaviour.
constexpr std::size_t kMaxEntities = std::size_t{1} << 20;

std::once_flag g_once;
bool g_installed = false;
// Set once all three hooks are in: until then they leave the engine's flips alone, so a failed install never
// keeps one slot and flips the other.
std::atomic<bool> g_live{false};
stereo_seq::GeomCachePrevMode g_mode = stereo_seq::GeomCachePrevMode::On;
const std::byte* g_renderSystem = nullptr;
std::unique_ptr<stereo_seq::GeomCacheStamps> g_stamps;

// The commit running on this thread, from the transform flip to its update's check.
struct Commit {
    std::uintptr_t cache = 0;
    std::size_t index = 0;
    std::uint64_t modelTime = 0;
    std::uint32_t renderFrame = 0;
    bool right = false;
    bool keep = false;
    bool leftValid = false;
};
thread_local Commit t_commit;

// Rows: eye L (and mono), eye R.
struct Counters {
    std::atomic<std::uint64_t> commits[2]{};
    std::atomic<std::uint64_t> sameTick{0};  // eye R commits of caches eye L updated in the render before
    std::atomic<std::uint64_t> otherTime{0}; // of those, at another model time than eye L's
    std::atomic<std::uint64_t> kept{0};
    std::atomic<std::uint64_t> updates[2]{};
    std::atomic<std::uint64_t> valid[2]{};     // updates given a valid previous frame
    std::atomic<std::uint64_t> engineValid{0}; // eye R updates the engine's own check found valid
    std::atomic<std::uint64_t> lastReport{0};
} g_counters;

stereo_seq::GeomCachePrevMode requestedMode() {
    std::wstring value;
    std::string narrow;
    if (readEnv(L"ETERNALVR_STEREO_GEOMCACHE_PREV", value)) {
        for (const wchar_t c : value) {
            narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
    }
    return stereo_seq::geomCachePrevMode(narrow);
}

unsigned long long take(std::atomic<std::uint64_t>& counter) {
    return static_cast<unsigned long long>(counter.exchange(0));
}

void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counters.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_counters.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    EVR_LOG(
        "%s: commits L %llu R %llu; eye R commits of caches eye L updated in the render before %llu (%llu at "
        "another "
        "model time), slots kept %llu; updates with a valid previous frame L %llu of %llu, R %llu of %llu "
        "(the engine's own check: %llu)",
        kTag, take(g_counters.commits[0]), take(g_counters.commits[1]), take(g_counters.sameTick),
        take(g_counters.otherTime), take(g_counters.kept), take(g_counters.valid[0]),
        take(g_counters.updates[0]), take(g_counters.valid[1]), take(g_counters.updates[1]),
        take(g_counters.engineValid));
}

struct CacheState {
    std::uint32_t transformSlot = 0;
    std::uint32_t handle = 0;
    bool animates = false;
};

// The cache's transform slot, animation byte and entity handle; false when a read faulted.
bool readCache(std::uintptr_t cache, CacheState& out) {
    __try {
        std::memcpy(&out.transformSlot, reinterpret_cast<const void*>(cache + kTransformSlot),
                    sizeof(out.transformSlot));
        std::uint8_t animates = 0;
        std::memcpy(&animates, reinterpret_cast<const void*>(cache + kAnimates), sizeof(animates));
        out.animates = animates != 0;
        std::uintptr_t entity = 0;
        std::memcpy(&entity, reinterpret_cast<const void*>(cache + kRenderEntity), sizeof(entity));
        if (entity == 0) {
            return false;
        }
        std::memcpy(&out.handle, reinterpret_cast<const void*>(entity + kEntityHandle), sizeof(out.handle));
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

bool readSlot(std::uintptr_t at, std::uint32_t& out) {
    __try {
        std::memcpy(&out, reinterpret_cast<const void*>(at), sizeof(out));
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

// On the transform slot's store: rdi is the cache, ecx the flipped slot, rbx the model time.
void onTransformFlip(HookRegisters& r) {
    t_commit = Commit{};
    if (!g_live.load(std::memory_order_acquire) || !mp_guard::allowsGameTouch()) {
        return;
    }
    CacheState cache;
    if (r.rdi == 0 || !readCache(r.rdi, cache)) {
        return;
    }
    const stereo_seq::Eye eye = seqChainEye();
    const bool right = eye == stereo_seq::Eye::Right;
    ++g_counters.commits[right ? 1 : 0];
    report();
    t_commit.cache = r.rdi;
    t_commit.index = cache.handle >> 8;
    t_commit.modelTime = r.rbx;
    std::memcpy(&t_commit.renderFrame, g_renderSystem + kRenderFrame, sizeof(t_commit.renderFrame));
    t_commit.right = right;
    if (!right) {
        return;
    }
    const stereo_seq::GeomCacheStamps::Left left =
        g_stamps->left(t_commit.index, t_commit.cache, t_commit.renderFrame, r.rbx);
    if (left.updated) {
        ++g_counters.sameTick;
        if (!left.sameTime) {
            ++g_counters.otherTime;
        }
    }
    if (!stereo_seq::keepGeomCacheSlots(g_mode, eye, left, cache.animates)) {
        return;
    }
    t_commit.keep = true;
    t_commit.leftValid = left.valid;
    r.rcx = cache.transformSlot; // the store writes the slot back unchanged
    ++g_counters.kept;
}

// On the position slot's store: rdi is the cache, eax the flipped slot.
void onPositionFlip(HookRegisters& r) {
    if (!t_commit.keep || t_commit.cache != r.rdi) {
        return;
    }
    std::uint32_t slot = 0;
    if (readSlot(r.rdi + kPositionSlot, slot)) {
        r.rax = slot;
    }
}

// After the update's previous-frame check: rsi is the cache, bpl the engine's verdict.
void onValidity(HookRegisters& r) {
    const Commit commit = t_commit;
    t_commit = Commit{};
    if (commit.cache == 0 || commit.cache != r.rsi || !mp_guard::allowsGameTouch()) {
        return;
    }
    const bool engineValid = (r.rbp & 0xFF) != 0;
    const int row = commit.right ? 1 : 0;
    ++g_counters.updates[row];
    if (!commit.right) {
        g_stamps->markLeft(commit.index, commit.cache, commit.renderFrame, engineValid, commit.modelTime);
        if (engineValid) {
            ++g_counters.valid[0];
        }
        return;
    }
    if (engineValid) {
        ++g_counters.engineValid;
    }
    const bool valid = stereo_seq::geomCachePreviousValid(commit.keep, engineValid, commit.leftValid);
    if (valid) {
        ++g_counters.valid[1];
    }
    if (valid != engineValid) {
        r.rbp = (r.rbp & ~std::uintptr_t{0xFF}) | (valid ? 1u : 0u);
    }
}

} // namespace

bool installGeomCachePrevHooks() {
    std::call_once(g_once, [] {
        g_mode = requestedMode();
        if (g_mode == stereo_seq::GeomCachePrevMode::Off) {
            EVR_LOG("%s: off (ETERNALVR_STEREO_GEOMCACHE_PREV=0): geometry caches (banners, hanging bodies) "
                    "have no motion in eye R",
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
        const std::byte* transform =
            findUnique(image, kTag, "geometry cache commit's transform slot flip", kTransformFlipSignature);
        const std::byte* position =
            findUnique(image, kTag, "geometry cache commit's position slot flip", kPositionFlipSignature);
        const std::byte* validity =
            findUnique(image, kTag, "geometry cache update's previous-frame check", kValiditySignature);
        if (!transform || !position || !validity) {
            EVR_LOG("%s: not installed; geometry caches have no motion in eye R", kTag);
            return;
        }
        const std::byte* counter = relativeTarget(validity + kValidityCall);
        const std::byte* renderSystem =
            ripTarget(image, validity + kRenderSystemLea, validity + kRenderSystemLea + 4);
        const bool ok = position - transform == kPositionFromTransform && counter &&
                        image.inText(counter, sizeof(kCounterCode)) &&
                        std::memcmp(counter, kCounterCode, sizeof(kCounterCode)) == 0 && renderSystem &&
                        image.contains(renderSystem + kRenderFrame, sizeof(std::uint32_t));
        if (!ok) {
            EVR_LOG("%s: the commit's two flips or the update's counter did not check out; not installed",
                    kTag);
            return;
        }
        g_renderSystem = renderSystem;
        g_stamps = std::make_unique<stereo_seq::GeomCacheStamps>(kMaxEntities);
        // All three or none: one slot kept and the other flipped would pair the wrong frames.
        const struct {
            const std::byte* at;
            MidHookEditCallback callback;
        } hooks[] = {
            {transform + kTransformFlipHook, &onTransformFlip},
            {position + kPositionFlipHook, &onPositionFlip},
            {validity + kValidityHook, &onValidity},
        };
        std::string error;
        for (const auto& h : hooks) {
            if (!installMidHookEdit(const_cast<std::byte*>(h.at), h.callback, error)) {
                // Hooks that went in stay in (they cannot be removed) but do nothing: g_live stays false.
                EVR_LOG("%s: hook at RVA 0x%X failed: %s; geometry caches have no motion in eye R", kTag,
                        image.rva(h.at), error.c_str());
                return;
            }
        }
        g_installed = true;
        g_live.store(true, std::memory_order_release);
        EVR_LOG("%s: geometry cache slot flips (RVA 0x%X, 0x%X) and previous-frame check (0x%X) hooked: %s",
                kTag, image.rva(hooks[0].at), image.rva(hooks[1].at), image.rva(hooks[2].at),
                g_mode == stereo_seq::GeomCachePrevMode::Count
                    ? "counting only (ETERNALVR_STEREO_GEOMCACHE_PREV=count); geometry caches have no motion "
                      "in eye R"
                    : "eye R keeps eye L's slots for caches eye L updated in the render before");
    });
    return g_installed;
}

} // namespace evr::vkcore
