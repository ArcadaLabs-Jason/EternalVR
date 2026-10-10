#include "vkcore/view_skin_alloc.hpp"

#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/view_slots.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "view-redirects";

constexpr std::size_t kContextSize = 0x71B170;        // a render context (view_slots.cpp)
constexpr std::uint32_t kGeometryManager = 0x5BF13B8; // the world geometry manager's pointer
constexpr std::size_t kMainOffset = 0x7CC;            // the main part's start in the output (bytes)
constexpr std::size_t kMainSize = 0x7C8;              // and its size (bytes)

struct Site {
    std::uint32_t rva;
    std::uint32_t counter; // block offset of the counter the xadd adds to
    std::uint32_t next;    // the instruction after the 9-byte xadd
};
constexpr Site kSites[] = {
    {0x1C0101D, 0x274, 0x1C01026}, // geo decal part
    {0x1C0104D, 0x270, 0x1C01056}, // main part, after the geo decal part was full
    {0x1C010B8, 0x270, 0x1C010C1}, // main part
};
// `lock xadd dword ptr [rsi + disp32], r15d`, then the disp32 (the counter)
constexpr std::uint8_t kXadd[] = {0xF0, 0x44, 0x0F, 0xC1, 0xBE};

std::uintptr_t g_base = 0;
std::atomic<std::uint64_t> g_shared{0}; // view 1's allocations taken from view 0's counter
std::atomic<std::int32_t> g_peak{0};    // the highest end one of them reached (floats)
std::atomic<int> g_logged{0};           // the kinds of allocation logged (bit per kind)
// View 0 is dispatched this frame: its 0x1C5C2F0 starts its counters at the output's start. With view 1
// alone (ETERNALVR_TEST_VIEW_ONLY=1) nothing would, and view 1 keeps its own counters.
std::atomic<bool> g_view0Dispatched{false};

// rsi = the view's block in its render context (+0x4D9F50, set up by 0x1C5C2F0), r15d = the room asked for.
// For view 1 the xadd is done on the same counter in view 0's render context and skipped; r15 then holds the
// old value with its upper half zero, as the xadd leaves it.
template <std::size_t I>
void onAlloc(HookRegisters& r) {
    const auto context1 = reinterpret_cast<std::uintptr_t>(viewSlotsContext1());
    const auto context0 = reinterpret_cast<std::uintptr_t>(viewSlotsContext0());
    if (context1 && context0) {
        // Once per kind: where the allocation's block lies (view 0's, view 1's render context, or neither).
        const int kind = r.rsi - context0 < kContextSize ? 0 : r.rsi - context1 < kContextSize ? 1 : 2;
        if (!(g_logged.load(std::memory_order_relaxed) & (1 << kind)) &&
            !(g_logged.fetch_or(1 << kind, std::memory_order_relaxed) & (1 << kind))) {
            static constexpr const char* kKinds[] = {"view 0's render context +", "view 1's render context +",
                                                     "neither render context: "};
            EVR_LOG("%s: compute skinning: a surface allocation with its block at %s0x%llX", kTag,
                    kKinds[kind],
                    static_cast<unsigned long long>(kind == 0   ? r.rsi - context0
                                                    : kind == 1 ? r.rsi - context1
                                                                : r.rsi));
        }
    }
    if (!context1 || !context0 || r.rsi - context1 >= kContextSize || !parallelEyesTouch() ||
        !g_view0Dispatched.load(std::memory_order_acquire)) {
        return;
    }
    constexpr Site s = kSites[I];
    auto* counter = reinterpret_cast<std::atomic<std::int32_t>*>(context0 + (r.rsi - context1) + s.counter);
    const auto size = static_cast<std::int32_t>(r.r15 & 0xFFFFFFFFu);
    const std::int32_t start = counter->fetch_add(size, std::memory_order_relaxed);
    r.r15 = static_cast<std::uint32_t>(start);
    r.resumeAt = g_base + s.next;
    g_shared.fetch_add(1, std::memory_order_relaxed);
    const std::int32_t end = start + size;
    std::int32_t peak = g_peak.load(std::memory_order_relaxed);
    while (end > peak && !g_peak.compare_exchange_weak(peak, end, std::memory_order_relaxed)) {
    }
}
constexpr MidHookEditCallback kCallbacks[] = {&onAlloc<0>, &onAlloc<1>, &onAlloc<2>};
static_assert(std::size(kCallbacks) == std::size(kSites));

// ---- The output's size: room for both views ----

// The world geometry manager's setup 0x1D242E0 reads r_worldGeometryManagerCSSkinBufferSize (positions,
// default 491520) into ebx at 0x1D24EE3 and keeps it at [rsp + 0x58], the geo decal part's size into ecx and
// [rsp + 0x50], then at 0x1D24EF8 adds the two (at most 0xFFFFF: a draw surface keeps 20 bits of offset) and
// makes the output with that room. Two views' surfaces from one counter ran past the default's end in
// e1m1_intro (rig run pgf2-still: a sleeve as spikes in one frame of 44, none with twice the room). The main
// part is doubled for Parallel Eye Rendering, within the 20 bits; the cvar itself is not written.
constexpr std::uint32_t kSizeHook = 0x1D24EF8;
struct Bytes {
    std::uint32_t rva;
    std::uint8_t bytes[11];
    std::size_t count;
};
constexpr Bytes kSizeBytes[] = {
    {0x1D24EE3, {0x8B, 0x58, 0x08}, 3},       // mov ebx, [rax + 8]
    {0x1D24EED, {0x89, 0x5C, 0x24, 0x58}, 4}, // mov [rsp + 0x58], ebx
    {0x1D24EF1, {0x8B, 0x48, 0x08, 0x89, 0x4C, 0x24, 0x50, 0x44, 0x8D, 0x24, 0x19}, 11}, // ecx; lea r12d
};
constexpr std::int32_t kMaxPositions = 0xFFFFF;
std::atomic<bool> g_sizeLogged{false};

void onSizeRead(HookRegisters& r) {
    if (!viewSlotsActive() || !mp_guard::allowsGameTouch()) {
        return;
    }
    const auto main = static_cast<std::int32_t>(r.rbx & 0xFFFFFFFFu);
    const auto decals = static_cast<std::int32_t>(r.rcx & 0xFFFFFFFFu);
    if (main <= 0 || decals < 0 || main + decals > kMaxPositions) {
        return; // the game's own check fails it either way
    }
    const std::int32_t doubled = main * 2 < kMaxPositions - decals ? main * 2 : kMaxPositions - decals;
    r.rbx = static_cast<std::uint32_t>(doubled);
    std::memcpy(reinterpret_cast<void*>(r.rsp + 0x58), &doubled, sizeof(doubled));
    if (!g_sizeLogged.exchange(true)) {
        EVR_LOG("%s: compute skinning output for both views: %d positions "
                "(r_worldGeometryManagerCSSkinBufferSize "
                "%d, geo decals %d)",
                kTag, doubled, main, decals);
    }
}

} // namespace

bool prepareViewSkinAlloc(const std::byte* base) {
    for (const Bytes& b : kSizeBytes) {
        if (std::memcmp(base + b.rva, b.bytes, b.count) != 0) {
            EVR_LOG("%s: RVA 0x%X is not the compute skinning output's size read; not changed", kTag, b.rva);
            return false;
        }
    }
    for (const Site& s : kSites) {
        const std::byte* at = base + s.rva;
        std::uint32_t disp = 0;
        std::memcpy(&disp, at + sizeof(kXadd), sizeof(disp));
        if (std::memcmp(at, kXadd, sizeof(kXadd)) != 0 || disp != s.counter ||
            s.next - s.rva != sizeof(kXadd) + sizeof(disp)) {
            EVR_LOG("%s: RVA 0x%X is not the compute skinning allocation; not changed", kTag, s.rva);
            return false;
        }
    }
    return true;
}

bool installViewSkinAlloc(const std::byte* base) {
    g_base = reinterpret_cast<std::uintptr_t>(base);
    std::string sizeError;
    if (!installMidHookEdit(const_cast<std::byte*>(base + kSizeHook), &onSizeRead, sizeError)) {
        EVR_LOG("%s: compute skinning output size hook at RVA 0x%X failed: %s", kTag, kSizeHook,
                sizeError.c_str());
        return false;
    }
    for (std::size_t i = 0; i < std::size(kSites); ++i) {
        std::string error;
        if (!installMidHookEdit(const_cast<std::byte*>(base + kSites[i].rva), kCallbacks[i], error)) {
            EVR_LOG("%s: compute skinning hook at RVA 0x%X failed: %s", kTag, kSites[i].rva, error.c_str());
            return false;
        }
    }
    return true;
}

void viewSkinAllocFrameStart(bool view0Dispatched) {
    g_view0Dispatched.store(view0Dispatched, std::memory_order_release);
}

void viewSkinAllocLogCounts() {
    std::int32_t end = 0;
    if (g_base) {
        const auto* manager = *reinterpret_cast<const std::byte* const*>(g_base + kGeometryManager);
        if (manager) {
            std::int32_t offset = 0;
            std::int32_t size = 0;
            std::memcpy(&offset, manager + kMainOffset, sizeof(offset));
            std::memcpy(&size, manager + kMainSize, sizeof(size));
            end = (offset >> 2) + (size >> 2); // as 0x1C00F70 checks it
        }
    }
    EVR_LOG("%s: compute skinning: view 1's surfaces took their room from view 0's counter %llu time(s); the "
            "highest end one reached %d of the main part's %d (floats)",
            kTag, static_cast<unsigned long long>(g_shared.load()), g_peak.load(), end);
}

} // namespace evr::vkcore
