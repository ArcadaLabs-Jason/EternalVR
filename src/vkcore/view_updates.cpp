#include "vkcore/view_updates.hpp"

#include "vkcore/job_nodes.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/seh_filter.hpp"
#include "vkcore/stereo_hooks.hpp"
#include "vkcore/update_lists.hpp"
#include "vkcore/view_slots.hpp"

#include <windows.h>

#include <immintrin.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "view-updates";

// ---- Code (Steam build 25216728) ----

constexpr std::uint32_t kHook = 0x18E51DC;
struct Bytes {
    std::uint32_t rva;
    std::uint8_t bytes[14];
    std::size_t count;
};
constexpr Bytes kChecks[] = {
    // The world update 0x18E5070: `inc [rax+0xBFD8]` (rax = [world+0x71DAE0]: the world frame number),
    // `mov rbx, rcx` (the world), `movsxd r8, [rbx+0x270E8]` (its model count), `test r15, r15; je` (r15 =
    // view 0's render view), the hooked `mov rax, [rbx+0x270E0]` (its models), then the four counts the job
    // counts are made from.
    {0x18E50BB, {0xFF, 0x80, 0xD8, 0xBF, 0x00, 0x00}, 6},
    {0x18E50C1, {0x48, 0x8B, 0xD9}, 3},
    {0x18E5106, {0x4C, 0x63, 0x83, 0xE8, 0x70, 0x02, 0x00}, 7},
    {0x18E51D3, {0x4D, 0x85, 0xFF, 0x0F, 0x84}, 5},
    {kHook, {0x48, 0x8B, 0x83, 0xE0, 0x70, 0x02, 0x00}, 7},
    {0x18E5222, {0x41, 0x8B, 0x8F, 0x70, 0x09, 0x02, 0x00}, 7},
    {0x18E5233, {0x41, 0x8B, 0x8F, 0x74, 0x09, 0x02, 0x00}, 7},
    {0x18E5255, {0x41, 0x8B, 0x8F, 0x78, 0x09, 0x02, 0x00}, 7},
    {0x18E528D, {0x41, 0x8B, 0x8F, 0x7C, 0x09, 0x02, 0x00}, 7},
    // The render-view job setup 0x1C5C2F0: the gather block `lea rbx, [rbp+0x4D9F50]` (rbp the render
    // context), its render view (+0x20), the world frame number it gathers at (+0x17C), its counts
    // (+0x90) and lists (+0x98).
    {0x1C5C79A, {0x48, 0x8D, 0x9D, 0x50, 0x9F, 0x4D, 0x00}, 7},
    {0x1C5C7C2, {0x4C, 0x89, 0x73, 0x20}, 4},
    {0x1C5C957, {0x41, 0x8B, 0x84, 0x24, 0xD8, 0xBF, 0x00, 0x00, 0x89, 0x83, 0x7C, 0x01, 0x00, 0x00}, 14},
    {0x1C5C977, {0x49, 0x8D, 0x86, 0x70, 0x09, 0x02, 0x00, 0x48, 0x89, 0x83, 0x90, 0x00, 0x00, 0x00}, 14},
    {0x1C5C985, {0x49, 0x8D, 0x86, 0x70, 0x09, 0x00, 0x00, 0x48, 0x89, 0x83, 0x98, 0x00, 0x00, 0x00}, 14},
    // The gather's last job 0x1C79FC0 (`mov r15, rcx`: r15 the block to its end), from the gather setup
    // 0x1C7FFD0 inline (`mov rcx, r12; call`) or as the job of record 0x39A5D68 after every other gather job;
    // the end hook goes on `mov rcx, [rbp+0x47FD0]` in its one epilogue, after its own appends.
    {0x1C79FF3, {0x4C, 0x8B, 0xF9}, 3},
    {0x1C813B3, {0x49, 0x8B, 0xCC, 0xE8, 0x05, 0x8C, 0xFF, 0xFF}, 8},
    {0x1C7A8F5, {0x48, 0x8B, 0x8D, 0xD0, 0x7F, 0x04, 0x00}, 7},
    // The gather's append (0x1C76C80): list 1 at +0x2000 entries, list 3 at +0x18000 bytes.
    {0x1C77988, {0x48, 0x05, 0x00, 0x20, 0x00, 0x00}, 6},
    {0x1C779E4, {0x48, 0x81, 0xC7, 0x00, 0x80, 0x01, 0x00}, 7},
};

// ---- Engine layouts ----

constexpr std::size_t kWorldModelCount = 0x270E8;
constexpr std::size_t kWorldFrameOwner = 0x71DAE0;
constexpr std::size_t kFrameNumber = 0xBFD8;
// idRenderView: list k at +0x970 + k * 0x8000, its count at +0x20970 + k * 4.
constexpr std::size_t kViewLists = 0x970;
constexpr std::size_t kListStride = 0x8000;
constexpr std::size_t kViewCounts = 0x20970;
// The gather block in a render context.
constexpr std::size_t kGatherBlock = 0x4D9F50;
constexpr std::size_t kBlockView = 0x20;
constexpr std::size_t kBlockCounts = 0x90;
constexpr std::size_t kBlockLists = 0x98;
constexpr std::size_t kBlockFrame = 0x17C;
constexpr std::uint32_t kGatherEnd = 0x1C79FC0;
constexpr std::uint32_t kGatherEndRecord = 0x39A5D68; // the job record: the function's address
constexpr std::uint32_t kGatherEndHook = 0x1C7A8F5;

constexpr const char* kListNames[update_lists::kLists] = {"particles", "flares", "beams", "ribbons"};
constexpr const char* const kNotInstalled =
    "not installed: models only eye R sees are not prepared or updated";

std::atomic<bool> g_live{false};

std::mutex g_mutex; // one world update at a time
update_lists::ListUnion g_union;
std::array<std::vector<std::int32_t>, update_lists::kLists> g_view0;
std::array<std::vector<std::int32_t>, update_lists::kLists> g_view1;
std::array<std::vector<std::int32_t>, update_lists::kLists> g_added;

std::int64_t g_waitTicks = 0;
job_nodes::BoundedWait* g_wait = nullptr; // under g_mutex

// Per render context (view 0's, view 1's): the view and world frame its last gather ended for, set by the
// gather's last job (a backend job thread) and read by the world update.
struct Ended {
    std::uintptr_t view = 0;
    std::uint32_t frame = 0;
};
std::mutex g_endedMutex;
std::array<Ended, 2> g_ended; // under g_endedMutex

struct Counters {
    std::atomic<std::uint64_t> frames{0};
    std::atomic<std::uint64_t> waited{0};
    std::array<std::atomic<std::uint64_t>, update_lists::kLists> added{};
    std::array<std::atomic<std::uint64_t>, update_lists::kLists> already{};
    std::array<std::atomic<std::uint64_t>, update_lists::kLists> dropped{};
    std::atomic<std::uint64_t> invalid{0};
    std::atomic<std::uint64_t> noView1{0};    // the world has no second render view
    std::atomic<std::uint64_t> sameView{0};   // the second render view is the world update's own
    std::atomic<std::uint64_t> noContexts{0}; // a view's render context not built yet
    std::atomic<std::uint64_t> stale{0};
    std::atomic<std::uint64_t> notEnded0{0}; // view 0's gather had not ended
    std::atomic<std::uint64_t> notEnded1{0}; // view 1's gather had not ended
    std::atomic<std::uint64_t> odd{0};
    std::atomic<std::uint64_t> faults{0};
    std::atomic<std::uint64_t> lastReport{0};
} g_counters;

void add(std::atomic<std::uint64_t>& counter, std::uint64_t n = 1) {
    counter.fetch_add(n, std::memory_order_relaxed);
}

bool guardedCopy(void* destination, const void* source, std::size_t size) {
    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

template <typename T>
bool readAt(std::uintptr_t address, T& out) {
    return address != 0 && guardedCopy(&out, reinterpret_cast<const void*>(address), sizeof(T));
}

// The count moves from `expected` to `count` only if nothing else changed it.
bool guardedSetCount(std::uintptr_t address, std::int32_t expected, std::int32_t count, bool& faulted) {
    faulted = false;
    __try {
        return InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(address), count, expected) ==
               expected;
    } __except (accessViolationOnly(GetExceptionCode())) {
        faulted = true;
        return false;
    }
}

std::int64_t ticks() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

// What a view's gather block says of the last gather set up in its render context.
struct Gather {
    std::uintptr_t view = 0;
    std::uintptr_t counts = 0;
    std::uintptr_t lists = 0;
    std::uint32_t frame = 0;
};

bool readGather(const std::byte* context, Gather& g) {
    const auto block = reinterpret_cast<std::uintptr_t>(context) + kGatherBlock;
    return context && readAt(block + kBlockView, g.view) && readAt(block + kBlockCounts, g.counts) &&
           readAt(block + kBlockLists, g.lists) && readAt(block + kBlockFrame, g.frame);
}

// Set up for `view` at world frame `frame`.
bool gatheredAt(const Gather& g, std::uintptr_t view, std::uint32_t frame) {
    return g.view == view && g.counts == view + kViewCounts && g.lists == view + kViewLists &&
           g.frame == frame;
}

// The gather's last job is about to return: its block (r15) names the view and frame its gather ended for.
void onGatherEnd(const HookRegisters& r) {
    if (!g_live.load(std::memory_order_acquire) || !viewSlotsActive()) {
        return;
    }
    const auto context0 = reinterpret_cast<std::uintptr_t>(viewSlotsContext0());
    const auto context1 = reinterpret_cast<std::uintptr_t>(viewSlotsContext1());
    const std::uintptr_t context = r.r15 - kGatherBlock;
    const int slot = context0 && context == context0 ? 0 : context1 && context == context1 ? 1 : -1;
    Ended e;
    if (slot < 0 || !readAt(r.r15 + kBlockView, e.view) || !readAt(r.r15 + kBlockFrame, e.frame)) {
        return;
    }
    std::lock_guard lock(g_endedMutex);
    g_ended[static_cast<std::size_t>(slot)] = e;
}

bool ended(int slot, std::uintptr_t view, std::uint32_t frame) {
    std::lock_guard lock(g_endedMutex);
    const Ended& e = g_ended[static_cast<std::size_t>(slot)];
    return e.view == view && e.frame == frame;
}

enum class Skip { None, NoView1, SameView, NoContexts, Stale, NotEnded0, NotEnded1, Odd, Fault };

// The first frame left out for each reason, with what the checks saw (once per reason).
std::atomic<std::uint32_t> g_explained{0};
bool firstOf(Skip skip) {
    const std::uint32_t bit = 1u << static_cast<int>(skip);
    return !(g_explained.load(std::memory_order_relaxed) & bit) &&
           !(g_explained.fetch_or(bit, std::memory_order_relaxed) & bit);
}

bool readList(std::uintptr_t view, int list, std::int32_t count, std::vector<std::int32_t>& out) {
    out.resize(static_cast<std::size_t>(count));
    return count == 0 || guardedCopy(out.data(),
                                     reinterpret_cast<const void*>(
                                         view + kViewLists + static_cast<std::size_t>(list) * kListStride),
                                     out.size() * sizeof(std::int32_t));
}

// View 1's entries not on view 0's lists, added to view 0's. Under g_mutex.
Skip unionLists(std::uintptr_t world, std::uintptr_t view0) {
    const auto view1 =
        reinterpret_cast<std::uintptr_t>(stereoSecondRenderView(reinterpret_cast<std::byte*>(world)));
    const std::byte* context0 = viewSlotsContext0();
    const std::byte* context1 = viewSlotsContext1();
    const Skip missing = !view1                   ? Skip::NoView1
                         : view1 == view0         ? Skip::SameView
                         : !context0 || !context1 ? Skip::NoContexts
                                                  : Skip::None;
    if (missing != Skip::None) {
        if (firstOf(missing)) {
            EVR_LOG("%s: left out: world %p, view 0 %p, its second render view %p, render contexts %p / %p",
                    kTag, reinterpret_cast<void*>(world), reinterpret_cast<void*>(view0),
                    reinterpret_cast<void*>(view1), static_cast<const void*>(context0),
                    static_cast<const void*>(context1));
        }
        return missing;
    }
    // View 1's view index is not checked: what matters is that view 1's gather block names it (below).
    std::uintptr_t owner = 0;
    std::uint32_t frame = 0;
    Gather g0;
    Gather g1;
    if (!readAt(world + kWorldFrameOwner, owner) || !readAt(owner + kFrameNumber, frame) ||
        !readGather(context0, g0) || !readGather(context1, g1)) {
        return Skip::Fault;
    }
    // Both views gathered this world at its last frame (the world update raised the number before the hook).
    if (!gatheredAt(g0, view0, frame - 1) || !gatheredAt(g1, view1, frame - 1)) {
        if (firstOf(Skip::Stale)) {
            EVR_LOG(
                "%s: left out (stale): world frame %u; view 0 %p, its gather: view %p, counts %p, lists %p, "
                "frame %u; view 1 %p, its gather: view %p, counts %p, lists %p, frame %u",
                kTag, frame, reinterpret_cast<void*>(view0), reinterpret_cast<void*>(g0.view),
                reinterpret_cast<void*>(g0.counts), reinterpret_cast<void*>(g0.lists), g0.frame,
                reinterpret_cast<void*>(view1), reinterpret_cast<void*>(g1.view),
                reinterpret_cast<void*>(g1.counts), reinterpret_cast<void*>(g1.lists), g1.frame);
        }
        return Skip::Stale;
    }
    // Both gathers ended: their last job's hook ran for this frame (the lock orders their writes before the
    // reads below).
    using Result = job_nodes::BoundedWait::Result;
    const Result waited = g_wait->wait(
        [&] { return ended(0, view0, frame - 1) && ended(1, view1, frame - 1); }, ticks, [] { _mm_pause(); });
    if (waited == Result::TimedOut || waited == Result::Off) {
        if (waited == Result::TimedOut && g_wait->off()) {
            EVR_LOG("%s: a view's gather had not ended within 0.5 ms in 16 frames in a row; no more waiting "
                    "(frames whose gathers have not ended are left out)",
                    kTag);
        }
        return ended(0, view0, frame - 1) ? Skip::NotEnded1 : Skip::NotEnded0;
    }
    if (waited == Result::Waited) {
        add(g_counters.waited);
    }
    std::int32_t models = 0;
    std::array<std::int32_t, update_lists::kLists> count0{};
    std::array<std::int32_t, update_lists::kLists> count1{};
    if (!readAt(world + kWorldModelCount, models) ||
        !guardedCopy(count0.data(), reinterpret_cast<const void*>(g0.counts), sizeof(count0)) ||
        !guardedCopy(count1.data(), reinterpret_cast<const void*>(g1.counts), sizeof(count1))) {
        return Skip::Fault;
    }
    for (int k = 0; k < update_lists::kLists; ++k) {
        if (count0[k] < 0 || count0[k] > update_lists::kCapacity || count1[k] < 0 ||
            count1[k] > update_lists::kCapacity) {
            return Skip::Odd;
        }
        if (!readList(view0, k, count0[k], g_view0[k]) || !readList(view1, k, count1[k], g_view1[k])) {
            return Skip::Fault;
        }
    }
    g_union.start(models);
    for (const auto& list : g_view0) {
        g_union.mark(list);
    }
    std::array<update_lists::ListCounts, update_lists::kLists> counts{};
    for (int k = 0; k < update_lists::kLists; ++k) {
        g_added[k].clear();
        counts[k] = g_union.add(g_view1[k], count0[k], g_added[k]);
    }
    // Each list's count first, from the count read (a count that moved leaves the list alone), then its new
    // entries in the room that reserved; the jobs that read them are made after the hook.
    for (int k = 0; k < update_lists::kLists; ++k) {
        const std::vector<std::int32_t>& added = g_added[k];
        if (!added.empty()) {
            bool faulted = false;
            if (!guardedSetCount(view0 + kViewCounts + static_cast<std::size_t>(k) * sizeof(std::int32_t),
                                 count0[k], count0[k] + static_cast<std::int32_t>(added.size()), faulted) &&
                !faulted) {
                add(g_counters.odd);
                continue;
            }
            const std::uintptr_t to = view0 + kViewLists + static_cast<std::size_t>(k) * kListStride +
                                      static_cast<std::size_t>(count0[k]) * sizeof(std::int32_t);
            faulted = faulted || !guardedCopy(reinterpret_cast<void*>(to), added.data(),
                                              added.size() * sizeof(std::int32_t));
            if (faulted) {
                g_live.store(false, std::memory_order_release);
                EVR_LOG("%s: writing view 0's %s list faulted; off for the rest of the session", kTag,
                        kListNames[k]);
                return Skip::Fault;
            }
        }
        add(g_counters.added[k], counts[k].added);
        add(g_counters.already[k], counts[k].already);
        add(g_counters.dropped[k], counts[k].dropped);
        add(g_counters.invalid, counts[k].invalid);
    }
    if (firstOf(Skip::None)) {
        std::int32_t index1 = -1;
        readAt(view1 + render_view_object::kViewIndex, index1);
        EVR_LOG(
            "%s: first frame with view 1's lists added: view 1 %p (view index field %d at the world update), "
            "added %u / %u / %u / %u",
            kTag, reinterpret_cast<void*>(view1), index1, counts[0].added, counts[1].added, counts[2].added,
            counts[3].added);
    }
    return Skip::None;
}

void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counters.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_counters.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    const auto take = [](std::atomic<std::uint64_t>& c) {
        return static_cast<unsigned long long>(c.exchange(0));
    };
    auto& c = g_counters;
    EVR_LOG("%s: last 10 s: %llu frame(s) with view 1's lists added to view 0's (%llu waited for a gather); "
            "view 1 only, added: particles %llu, flares %llu, beams %llu, ribbons %llu; already on view 0's: "
            "%llu, %llu, %llu, %llu; over the 0x2000 cap: %llu, %llu, %llu, %llu; not a model %llu; frames "
            "left out: %llu without a second render view, %llu with it the update's own, %llu without render "
            "contexts, %llu stale, %llu with view 0's gather not ended, %llu with view 1's, %llu odd, %llu "
            "faulted",
            kTag, take(c.frames), take(c.waited), take(c.added[0]), take(c.added[1]), take(c.added[2]),
            take(c.added[3]), take(c.already[0]), take(c.already[1]), take(c.already[2]), take(c.already[3]),
            take(c.dropped[0]), take(c.dropped[1]), take(c.dropped[2]), take(c.dropped[3]), take(c.invalid),
            take(c.noView1), take(c.sameView), take(c.noContexts), take(c.stale), take(c.notEnded0),
            take(c.notEnded1), take(c.odd), take(c.faults));
}

void onWorldUpdate(const HookRegisters& r) {
    if (!g_live.load(std::memory_order_acquire) || !viewSlotsActive() || !parallelEyesTouch() || r.rbx == 0 ||
        r.r15 == 0) {
        return;
    }
    Skip skip = Skip::None;
    {
        std::lock_guard lock(g_mutex);
        skip = unionLists(r.rbx, r.r15);
    }
    switch (skip) {
    case Skip::None:
        add(g_counters.frames);
        break;
    case Skip::NoView1:
        add(g_counters.noView1);
        break;
    case Skip::SameView:
        add(g_counters.sameView);
        break;
    case Skip::NoContexts:
        add(g_counters.noContexts);
        break;
    case Skip::Stale:
        add(g_counters.stale);
        break;
    case Skip::NotEnded0:
        add(g_counters.notEnded0);
        break;
    case Skip::NotEnded1:
        add(g_counters.notEnded1);
        break;
    case Skip::Odd:
        add(g_counters.odd);
        break;
    case Skip::Fault:
        add(g_counters.faults);
        break;
    }
    report();
}

} // namespace

void installViewUpdates(const std::byte* base) {
    if (!parallelEyesSettings().updateUnion) {
        EVR_LOG(
            "%s: off (ETERNALVR_TEST_PE_UPDATE_UNION=0): models only eye R sees are not prepared or updated",
            kTag);
        return;
    }
    for (const Bytes& b : kChecks) {
        if (std::memcmp(base + b.rva, b.bytes, b.count) != 0) {
            EVR_LOG("%s: RVA 0x%X is not as known; %s", kTag, b.rva, kNotInstalled);
            return;
        }
    }
    std::uintptr_t record = 0;
    std::memcpy(&record, base + kGatherEndRecord, sizeof(record));
    if (record != reinterpret_cast<std::uintptr_t>(base + kGatherEnd)) {
        EVR_LOG("%s: the job record at RVA 0x%X is not the gather's last job; %s", kTag, kGatherEndRecord,
                kNotInstalled);
        return;
    }
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    g_waitTicks = frequency.QuadPart / 2000; // 0.5 ms
    static job_nodes::BoundedWait wait(g_waitTicks, 16);
    g_wait = &wait;
    // The end hook first: the union waits for what it records.
    const std::pair<std::uint32_t, MidHookCallback> hooks[] = {{kGatherEndHook, &onGatherEnd},
                                                               {kHook, &onWorldUpdate}};
    for (const auto& [rva, callback] : hooks) {
        std::string error;
        if (!installMidHook(const_cast<std::byte*>(base + rva), callback, error)) {
            EVR_LOG("%s: hook at RVA 0x%X failed: %s; %s", kTag, rva, error.c_str(), kNotInstalled);
            return;
        }
    }
    g_live.store(true, std::memory_order_release);
    EVR_LOG("%s: hooks at RVA 0x%X (the world update, before its job counts) and 0x%X (the gather's end): "
            "view 1's "
            "particle, flare, beam and ribbon lists are added to view 0's once both views' gathers ended, so "
            "models "
            "only eye R sees are prepared and updated (ETERNALVR_TEST_PE_UPDATE_UNION=0 leaves it out)",
            kTag, kHook, kGatherEndHook);
}

} // namespace evr::vkcore
