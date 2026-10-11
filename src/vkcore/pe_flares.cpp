#include "vkcore/pe_flares.hpp"

#include "features/flares/flare_book.hpp"
#include "features/flares/flare_draws.hpp"
#include "stereo_seq/stereo_taa.hpp"
#include "vkcore/flare_core.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/pe_flare_contexts.hpp"
#include "vkcore/pe_flare_guard.hpp"
#include "vkcore/pe_flare_sites.hpp"
#include "vkcore/seh_filter.hpp"
#include "vkcore/stereo_hooks.hpp"
#include "vkcore/view_slots.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <mutex>
#include <optional>
#include <span>
#include <string>

namespace evr::vkcore {

namespace {

namespace sites = pe_flare_sites;

constexpr const char* kTag = "pe-flares";
constexpr const char* kHeldOff = "lens flares stay off (r_skipFlares 1)";

// idRenderModelFlare and its geometry records (flare_core.hpp has the rest).
constexpr std::size_t kModelGeometry = 0x4E8; // element i's record at [+0x4E8] + i * 0x188
constexpr std::size_t kGeometryStride = 0x188;
constexpr std::size_t kGeometryIndexOffset = 0x6C; // its index range's byte offset in the quad index buffer
constexpr std::size_t kGeometryIndexCount = 0x104;
constexpr std::int32_t kQuadIndexCount = 6;
constexpr std::size_t kRenderContextSize = 0x71B170; // a render context (view_slots.cpp)
constexpr std::int32_t kMaxGeometries = 64;          // elements of one flare that get a shift

std::uintptr_t g_base = 0;
std::atomic<bool> g_on{false};      // installed and not turned off
std::atomic<bool> g_perView{false}; // this frame: each view issues, copies and reads its own queries
std::atomic<bool> g_loggedOff{false};
bool g_waitAllowed = true; // ETERNALVR_TEST_PE_FLARE_WAIT=0: the copies never wait (a rig A/B)

flares::DrawShifts g_shifts; // written at the latches, under g_latchMutex
std::mutex g_latchMutex;
bool g_lastPerView = false; // under g_latchMutex
thread_local bool t_copyWaits = false;

struct Counters {
    std::atomic<std::uint64_t> frames{0};
    std::atomic<std::uint64_t> perViewFrames{0};
    std::atomic<std::uint64_t> rooms{0};
    std::atomic<std::uint64_t> rebuilt0{0};
    std::atomic<std::uint64_t> rebuilt1{0};
    std::atomic<std::uint64_t> left{0}; // returned early, moved, odd or without room: the engine's quads
    std::atomic<std::uint64_t> shifted{0};
    std::atomic<std::uint64_t> view0Binds{0};
    std::atomic<std::uint64_t> unshifted{0}; // view 1 bound a flare record whose half 1 was not rebuilt
    std::atomic<std::uint64_t> marks{0};
    std::atomic<std::uint64_t> setups{0}; // flares either view's surface setup marked (the draw watch)
    std::atomic<std::uint64_t> issued{0};
    std::atomic<std::uint64_t> copies{0};
    std::atomic<std::uint64_t> waited{0};
    std::atomic<std::uint64_t> lastReport{0};
} g_counts;

std::atomic<bool> g_loggedFirst{false};
std::atomic<bool> g_loggedShift{false};

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

template <typename T>
T load(std::uintptr_t address) {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

bool on() {
    return g_on.load(std::memory_order_acquire);
}

void turnOff(const char* why) {
    if (!g_on.exchange(false)) {
        return;
    }
    flare_core::stopTwoHalves();
    if (!g_loggedOff.exchange(true)) {
        EVR_LOG("%s: off: %s; %s from the next present", kTag, why, kHeldOff);
    }
}

bool inViewOneBlock(std::uintptr_t block) {
    const auto context1 = reinterpret_cast<std::uintptr_t>(viewSlotsContext1());
    return context1 != 0 && block - context1 < kRenderContextSize;
}

// ---- Room for two halves (the prepare's call to the quad allocator) ----

void onRoom(HookRegisters& r) {
    if (!flare_core::twoHalves()) {
        return; // one half, as the engine has it; a record says so too (flare_core.hpp)
    }
    const auto count = static_cast<std::uint32_t>(r.rdx);
    if (count == 0 || count > static_cast<std::uint32_t>(flares::kMaxQuads / 2)) {
        return; // the allocator fails it either way
    }
    r.rdx = count * 2u;
    add(g_counts.rooms);
}

// ---- View 1's binds of a flare's index range ----

using sites::BindSite;
using sites::kBatchRecordOffset;
using sites::kBindSites;
using sites::Override;

template <std::size_t I>
void onBind(HookRegisters& r) {
    if (g_shifts.empty()) {
        return;
    }
    constexpr BindSite s = kBindSites[I];
    const bool overridden =
        s.kind == Override::Rdi ? r.rdi != 0 : load<std::uintptr_t>(r.rbp - s.rbpOffset) != 0;
    if (overridden) {
        return; // another index buffer than the record's
    }
    const std::uintptr_t record = s.recordInRsi ? r.rsi - kBatchRecordOffset : r.r12;
    const auto offset = static_cast<std::int32_t>(static_cast<std::uint32_t>(r.rax));
    const std::optional<std::int32_t> shift = g_shifts.find(record, offset);
    if (!shift) {
        return;
    }
    const int view = pe_flare_contexts::bindView(r.rbx);
    if (view == 0) {
        add(g_counts.view0Binds);
    } else if (view == 1 && *shift == 0) {
        add(g_counts.unshifted);
    } else if (view == 1) {
        r.rax = static_cast<std::uint32_t>(offset + *shift);
        add(g_counts.shifted);
        if (!g_loggedShift.exchange(true)) {
            EVR_LOG("%s: view 1 drew its half of a flare first at RVA 0x%X (index offset 0x%X + 0x%X)", kTag,
                    s.rva, static_cast<unsigned>(offset), static_cast<unsigned>(*shift));
        }
    }
}
constexpr MidHookEditCallback kBindCallbacks[] = {&onBind<0>, &onBind<1>, &onBind<2>, &onBind<3>};
static_assert(std::size(kBindCallbacks) == std::size(kBindSites));

// ---- View 1's queries: the upper half of the pool ----

// The surface setup's marks (0x1C7CF80, rdi the view's block, r10 the render entity): the two loads made
// here, view 1's twins in a frame split by view.
void onMarks(HookRegisters& r) {
    auto a = load<std::int64_t>(r.r10 + 0x108);
    auto b = load<std::int64_t>(r.r10 + 0x110);
    const int view = inViewOneBlock(r.rdi) ? 1 : 0;
    if (view == 1 && g_perView.load(std::memory_order_acquire)) {
        a = flares::viewQuery(a, 1);
        b = flares::viewQuery(b, 1);
        add(g_counts.marks);
    }
    add(g_counts.setups);
    pe_flare_guard::marked(view, a); // before the setup sets the bits: a copy that sees them sees these
    pe_flare_guard::marked(view, b);
    r.rax = static_cast<std::uintptr_t>(a);
    r.r9 = static_cast<std::uintptr_t>(b);
    r.resumeAt = g_base + sites::kMarksResume;
}

// The occlusion pass (rdi the render entity): the slot it begins and ends, view 1's twin in its contexts.
// Noted as issued by the pass's view (in a context of neither view: by none), for the check of what its copy
// waited on at view 0's next latch (the copy is recorded before the pass, flare_pending.hpp).
template <std::size_t Field>
std::uintptr_t occlusionQuery(std::uintptr_t entity, std::uintptr_t context) {
    auto slot = load<std::int64_t>(entity + Field);
    const int view = pe_flare_contexts::occlusionView(context);
    if (g_perView.load(std::memory_order_acquire) && view == 1) {
        slot = flares::viewQuery(slot, 1);
        if constexpr (Field == 0x108) {
            add(g_counts.issued);
        }
    }
    pe_flare_guard::issued(view, slot);
    return static_cast<std::uintptr_t>(slot);
}

void onOcclusionA(HookRegisters& r) {
    r.rbx = occlusionQuery<0x108>(r.rdi, r.rcx);
    r.resumeAt = g_base + sites::kOcclusionResumeA;
}

void onOcclusionB(HookRegisters& r) {
    r.rbx = occlusionQuery<0x110>(r.rdi, load<std::uintptr_t>(r.rbp - sites::kOcclusionContext));
    r.resumeAt = g_base + sites::kOcclusionResumeB;
}

// The copy (rbp the command context, r14 the engine's word count, esi 0): view 1 scans the upper half; each
// view waits for its own queries while the split holds and every bit it scans is its own of this frame.
void onCopyScan(HookRegisters& r) {
    t_copyWaits = false;
    const int view = g_perView.load(std::memory_order_acquire) ? pe_flare_contexts::copyView(r.rbp) : -1;
    if (view < 0) {
        return; // the engine's own scan, without the wait
    }
    const auto taken = load<std::int32_t>(g_base + sites::kSlotCounter);
    const flares::CopyScan scan = flares::copyScan(view, static_cast<std::int32_t>(r.r14), taken);
    add(g_counts.copies);
    t_copyWaits = pe_flare_guard::copyMayWait(view, scan.firstWord, scan.words, scan.wait && g_waitAllowed);
    if (t_copyWaits) {
        add(g_counts.waited);
    }
    if (view == 0) {
        return; // the engine's scan (the lea runs)
    }
    r.rdi = g_base + sites::kPendingBits + static_cast<std::uintptr_t>(scan.firstWord) * 4;
    r.rsi = static_cast<std::uintptr_t>(scan.firstSlot);
    r.r14 = static_cast<std::uintptr_t>(scan.words);
    r.resumeAt = g_base + sites::kCopyLoop;
}

// ---- The latches ----

// A flare's element records after a half was rebuilt: their offsets this frame and the shift view 1 adds
// (none for half 0, which is only counted). A record that is not as the prepare made it is left out.
void noteRebuilt(const flares::FlareRecord& f, int half) {
    add(half == 0 ? g_counts.rebuilt0 : g_counts.rebuilt1);
    std::uintptr_t records = 0;
    if (!readAt(f.model + kModelGeometry, records) || records == 0 || f.quads > kMaxGeometries) {
        return;
    }
    std::array<std::int32_t, kMaxGeometries> offsets{};
    for (std::int32_t i = 0; i < f.quads; ++i) {
        const std::uintptr_t record = records + static_cast<std::uintptr_t>(i) * kGeometryStride;
        std::int32_t count = 0;
        if (!readAt(record + kGeometryIndexOffset, offsets[static_cast<std::size_t>(i)]) ||
            !readAt(record + kGeometryIndexCount, count) || count != kQuadIndexCount ||
            offsets[static_cast<std::size_t>(i)] != offsets[0] + i * flares::kIndexBytesPerQuad) {
            return;
        }
    }
    const std::int32_t shift = half == 1 ? flares::halfIndexShift(f.quads) : 0;
    for (std::int32_t i = 0; i < f.quads; ++i) {
        g_shifts.put(records + static_cast<std::uintptr_t>(i) * kGeometryStride,
                     offsets[static_cast<std::size_t>(i)], shift);
    }
}

// View 0's render view in view 1's world: the flare job's (the records' key). The engine's render view list
// holds view 0 alone; view 1's is the world's second render view (stereo_hooks.hpp), which the world vtable's
// RenderViewForIndex(1) returns (the world's list read 1 entry, rig run pf2-c-pdiag).
bool viewZeroOf(std::byte* renderView, std::uintptr_t& view0) {
    std::uintptr_t world = 0;
    std::uintptr_t views = 0;
    std::int32_t count = 0;
    const auto rv = reinterpret_cast<std::uintptr_t>(renderView);
    return readAt(rv + render_view_object::kOwningWorld, world) && world != 0 &&
           stereoSecondRenderView(reinterpret_cast<const std::byte*>(world)) == renderView &&
           readAt(world + sites::kWorldRenderViewCount, count) && count >= 1 &&
           readAt(world + sites::kWorldRenderViews, views) && views != 0 && readAt(views, view0) &&
           view0 != 0 && view0 != rv;
}

void report(const flares::RenderKey& key) {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counts.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_counts.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    const auto take = [](std::atomic<std::uint64_t>& c) {
        return c.exchange(0);
    };
    const auto ull = [](std::uint64_t v) {
        return static_cast<unsigned long long>(v);
    };
    const flare_core::RecordCounts rec = flare_core::takeRecordCounts();
    const std::uint64_t frames = take(g_counts.frames);
    const std::uint64_t perView = take(g_counts.perViewFrames);
    const std::uint64_t rebuilt0 = take(g_counts.rebuilt0);
    const std::uint64_t rebuilt1 = take(g_counts.rebuilt1);
    const std::uint64_t shifted = take(g_counts.shifted);
    const std::uint64_t view0 = take(g_counts.view0Binds);
    const std::uint64_t unshifted = take(g_counts.unshifted);
    const std::uint64_t setups = take(g_counts.setups);
    const pe_flare_guard::Counts guard = pe_flare_guard::takeCounts();
    EVR_LOG(
        "%s: last 10 s: %llu frame(s) (%llu with each view's own queries), room for both views %llu, "
        "flares rebuilt %llu in view 0 and %llu in view 1 (%llu left as the engine wrote them); binds: "
        "view 1 on its half %llu (%llu not), view 0 %llu; set up %llu, view 1's queries %llu marked, %llu "
        "issued; copies %llu (%llu waited, %llu of them with no query pending, %llu not for a query its view "
        "did not mark; %llu view frame(s) waited on a query its view did not issue%s); pending words left "
        "set at view 0's latch %llu (in %llu frame(s)); recorded %llu (%llu over the cap, %llu late, %llu "
        "not written)",
        kTag, ull(frames), ull(perView), ull(take(g_counts.rooms)), ull(rebuilt0), ull(rebuilt1),
        ull(take(g_counts.left)), ull(shifted), ull(unshifted), ull(view0), ull(setups),
        ull(take(g_counts.marks)), ull(take(g_counts.issued)), ull(take(g_counts.copies)),
        ull(take(g_counts.waited)), ull(guard.emptyWaits), ull(guard.unmarked), ull(guard.unissued),
        guard.tripped ? "; waits off this session" : "", ull(guard.staleWords), ull(guard.staleFrames),
        ull(rec.recorded), ull(rec.full), ull(rec.late), ull(rec.unwritten));
    if (guard.testOn) {
        EVR_LOG(
            "%s: test: %llu pending bit(s) set with no mark and no issue (ETERNALVR_TEST_PE_FLARES_STALE=1)",
            kTag, ull(guard.testBits));
    }
    if (flares::drawsUnseen({rebuilt1, shifted + view0 + unshifted, setups})) {
        EVR_LOG("%s: %llu flare half(s) rebuilt for view 1 and no bind of their records at the hooked index "
                "binds (world frame %u)",
                kTag, ull(rebuilt1), key.frame);
        turnOff("the flares' draws are not at the hooked binds");
    }
}

// The views' command contexts are still the ones the install read from the game's table; else the flares
// go off (a bind, query or copy would be taken for the wrong view).
bool contextsUnchanged() {
    using pe_flare_contexts::kTableSlots;
    const int changed = pe_flare_contexts::changedCell(g_base);
    if (changed < 0) {
        return true;
    }
    char why[96];
    std::snprintf(why, sizeof(why), "view command context %d of category %d is not the one at the install",
                  changed % kTableSlots, changed / kTableSlots);
    turnOff(why);
    return false;
}

// View 0's latch starts the frame: the views' contexts checked, its queries split by view or not, the
// last frame's waits checked against its issues, the pending bits (also in the first frame after a split one)
// and the setups' marks cleared (then the rig's test bit set, ETERNALVR_TEST_PE_FLARES_STALE=1), the last
// frame's shifts dropped. Before the frame's surface setups, which run after their view's latch.
void startFrame() {
    const bool touch = mp_guard::allowsGameTouch() && parallelEyesTouch();
    const bool perView = on() && flare_core::live() && touch && contextsUnchanged();
    pe_flare_guard::startFrame(touch && (perView || g_lastPerView), perView);
    g_lastPerView = perView;
    if (perView) {
        add(g_counts.perViewFrames);
    }
    g_perView.store(perView, std::memory_order_release);
    g_shifts.reset();
    add(g_counts.frames);
}

void tally(const flare_core::HalfResult& r) {
    add(g_counts.left, r.skipped + r.changed + r.odd + r.noRoom);
    if (r.fault) {
        turnOff("a flare's rebuild faulted");
    }
}

} // namespace

void installParallelEyeFlares(const std::byte* base) {
    std::wstring value;
    std::string narrow;
    if (readEnv(L"ETERNALVR_PE_FLARES", value)) {
        for (const wchar_t c : value) {
            narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
    }
    if (!stereo_seq::switchValue(narrow, true)) {
        EVR_LOG("%s: off (ETERNALVR_PE_FLARES=0): %s", kTag, kHeldOff);
        return;
    }
    if (readEnv(L"ETERNALVR_TEST_PE_FLARE_WAIT", value) && value == L"0") {
        g_waitAllowed = false;
        EVR_LOG("%s: the occlusion copies do not wait (ETERNALVR_TEST_PE_FLARE_WAIT=0)", kTag);
    }
    for (const sites::Expected& e : sites::kExpected) {
        if (std::memcmp(base + e.rva, e.bytes, e.size) != 0) {
            EVR_LOG("%s: RVA 0x%X is not %s as known; %s", kTag, e.rva, e.what, kHeldOff);
            return;
        }
    }
    if (!pe_flare_contexts::capture(base)) {
        EVR_LOG("%s: a command context of a view is missing; %s", kTag, kHeldOff);
        return;
    }
    if (!flare_core::install(kTag, kHeldOff, "")) {
        return;
    }
    g_base = reinterpret_cast<std::uintptr_t>(base);
    pe_flare_guard::install(g_base);
    struct Site {
        std::uint32_t rva;
        MidHookEditCallback callback;
    };
    const Site hooks[] = {
        {sites::kRoomCall, &onRoom},
        {kBindSites[0].rva, kBindCallbacks[0]},
        {kBindSites[1].rva, kBindCallbacks[1]},
        {kBindSites[2].rva, kBindCallbacks[2]},
        {kBindSites[3].rva, kBindCallbacks[3]},
        {sites::kMarksLoad, &onMarks},
        {sites::kOcclusionLoadA, &onOcclusionA},
        {sites::kOcclusionLoadB, &onOcclusionB},
        {sites::kCopyLea, &onCopyScan},
    };
    // Each hook is inert until g_on (the room, the shifts and the split all start from it): one that fails
    // leaves the ones before it inert for good.
    for (const Site& h : hooks) {
        std::string error;
        if (!installMidHookEdit(const_cast<std::byte*>(base + h.rva), h.callback, error)) {
            EVR_LOG("%s: hook at RVA 0x%X failed: %s; %s", kTag, h.rva, error.c_str(), kHeldOff);
            return;
        }
    }
    flare_core::startTwoHalves();
    g_on.store(true, std::memory_order_release);
    EVR_LOG(
        "%s: on: each view's lens flares from its own eye: room for both views (RVA 0x%X), view 1's index "
        "binds on its half (RVAs 0x%X, 0x%X, 0x%X, 0x%X), view 1's occlusion queries in the pool's upper "
        "half (RVAs 0x%X, 0x%X, 0x%X), each view's copy of its own (RVA 0x%X); ETERNALVR_PE_FLARES=0 "
        "keeps them off",
        kTag, sites::kRoomCall, kBindSites[0].rva, kBindSites[1].rva, kBindSites[2].rva, kBindSites[3].rva,
        sites::kMarksLoad, sites::kOcclusionLoadA, sites::kOcclusionLoadB, sites::kCopyLea);
}

bool parallelEyeFlaresLive() {
    return on() && flare_core::live();
}

void parallelEyeFlaresLatched(std::byte* renderView, int viewIndex, bool poseWritten) {
    if (g_base == 0 || renderView == nullptr || viewIndex < 0 || viewIndex > 1) {
        return;
    }
    std::lock_guard lock(g_latchMutex);
    if (viewIndex == 0) {
        startFrame();
    }
    flares::RenderKey key;
    std::uintptr_t view0 = reinterpret_cast<std::uintptr_t>(renderView);
    if ((viewIndex == 1 && !viewZeroOf(renderView, view0)) || !flare_core::renderKeyOf(view0, key)) {
        return;
    }
    if (!poseWritten || !parallelEyeFlaresLive()) {
        report(key);
        return; // the engine's quads stay (and view 1 draws view 0's half)
    }
    // View 1's vertices carry the queries its occlusion pass issues: their twins in a frame split by view.
    const int slotView = viewIndex == 1 && g_perView.load(std::memory_order_acquire) ? 1 : 0;
    const flare_core::HalfResult r = flare_core::rebuild(renderView, key, viewIndex, slotView, &noteRebuilt);
    tally(r);
    if (viewIndex == 1 && r.done > 0 && !g_loggedFirst.exchange(true)) {
        EVR_LOG(
            "%s: first frame with view 1's own flares: %zu of %zu rebuilt from render view %p (world frame "
            "%u)",
            kTag, r.done, r.taken, static_cast<void*>(renderView), key.frame);
    }
    report(key);
}

bool parallelEyeFlaresCopyWaits() {
    return t_copyWaits;
}

} // namespace evr::vkcore
