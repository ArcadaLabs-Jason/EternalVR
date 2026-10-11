#include "vkcore/flare_core.hpp"

#include "features/flares/flare_book.hpp"
#include "features/flares/flare_draws.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/seh_filter.hpp"
#include "vkcore/stereo_hooks.hpp"
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
#include <vector>

namespace evr::vkcore {

namespace {

// ---- Code (Steam build 25216728) ----

// The flare job (RVA 0x18E1670), per entry of view 0's flare list: `mov r9, [r14+0x18]` (RVA 0x18E1704; rbx
// the model, r10 the render view, r14 the job's block), `mov r8, r10; mov rdx, r10; mov rcx, rbx; call
// UpdateInView` (RVA 0x18E1711), then `add rdi, 4; sub rsi, 1; jne` (RVA 0x18E1716, also where skipped
// entries go). The particle, beam and ribbon jobs have the same loop around their own update (four matches);
// the flare job's is the one whose call goes to the UpdateInView checked below. The first hook goes on the
// `mov r9`, the second after the call (rbx, saved by UpdateInView, is still the model).
constexpr const char* kLoopSignature =
    "4D 8B 4E 18 4D 8B C2 49 8B D2 48 8B CB E8 ?? ?? ?? ?? 48 83 C7 04 48 83 EE 01 75 B0";
constexpr std::size_t kLoopCall = 0x0D;
constexpr std::size_t kLoopAfterCall = 0x12;
constexpr std::size_t kBlockContext = 0x18; // the job block's fourth field: UpdateInView's fourth argument

// idRenderModelFlare::UpdateInView (RVA 0x1936650) allocates its two occlusion query slots (RVA 0x1936728,
// unique): `movaps [rsp+0x180], xmm15`, `call 0x1C343C0; mov r12, rax; call 0x1C343C0` (RVA 0x1936731,
// 0x1936739), then stores them in the model's render entity: `mov rcx, [rbx+0xA8]; mov r13, rax; mov esi,
// 0xF8; mov [rcx+0x110], rax; mov eax, 0x158; mov [rcx+0x108], r12`, then `mov ecx, esi` (RVA 0x1936760).
// The slot hook goes on the first call; for this layer's own calls it sets r12 and rax to the record's slots
// and resumes after the second call, so the stores put the engine's slots of this render back. With view 1's
// twins (Parallel Eye Rendering) it resumes at `mov ecx, esi` instead, with the registers the skipped
// instructions would leave (r12 and r13 the twins, esi 0xF8, eax 0x158; rcx is written again there, and
// `and dl, 4` sets the flags again before the next `cmove`): the twins never reach the render entity, which
// view 0's surface setup and occlusion pass read.
constexpr const char* kSlotsSignature =
    "44 0F 29 BC 24 80 01 00 00 E8 ?? ?? ?? ?? 4C 8B E0 E8 ?? ?? ?? ?? 48 8B 8B "
    "A8 00 00 00 4C 8B E8 BE F8 00 00 00 48 89 81 10 01 00 00 B8 58 01 00 00 4C "
    "89 A1 08 01 00 00";
constexpr std::size_t kSlotsInUpdate = 0xD8; // the signature's offset from UpdateInView's start
constexpr std::size_t kSlotsFirstCall = 0x09;
constexpr std::size_t kSlotsSecondCall = 0x11;
constexpr std::size_t kSlotsResume = 0x16;      // `mov rcx, [rbx+0xA8]`
constexpr std::size_t kSlotsAfterStores = 0x38; // `mov ecx, esi`, the signature's end
constexpr std::uintptr_t kStretchEsi = 0xF8;    // `mov esi, 0xF8`
constexpr std::uintptr_t kStretchEax = 0x158;   // `mov eax, 0x158`

// The slot allocator (RVA 0x1C343C0): `mov eax, 1; lock xadd [rip+...], eax`, a counter modulo 1024.
constexpr unsigned char kAllocatorBytes[] = {0xB8, 0x01, 0x00, 0x00, 0x00, 0xF0, 0x0F, 0xC1, 0x05};

// What else of UpdateInView this relies on, at offsets from its start. Its third and fourth arguments are
// never read (r8 and r9 are written before any use); they are passed as the engine passes them.
// The prologue: rbx keeps the model (rcx), rdi the render view (rdx).
constexpr unsigned char kPrologue[] = {0x40, 0x55, 0x53, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0xC0, 0xFE,
                                       0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x40, 0x02, 0x00, 0x00};
// `cmp qword [rbx+0x4F0], 0`: without a vertex block it writes nothing.
constexpr unsigned char kBlockTest[] = {0x48, 0x83, 0xBB, 0xF0, 0x04, 0x00, 0x00, 0x00};
// `movss xmm3, [rbx+0x480]; mulss xmm3, [rbx+0x4F8]; mov rax, [rbx+0x4D0]` ... `movss [rbx+0x4F8], xmm3`: the
// intensity the prepare left is scaled in place.
constexpr unsigned char kIntensityRead[] = {0xF3, 0x0F, 0x10, 0x9B, 0x80, 0x04, 0x00, 0x00,
                                            0xF3, 0x0F, 0x59, 0x9B, 0xF8, 0x04, 0x00, 0x00,
                                            0x48, 0x8B, 0x83, 0xD0, 0x04, 0x00, 0x00};
constexpr unsigned char kIntensityWrite[] = {0xF3, 0x0F, 0x11, 0x9B, 0xF8, 0x04, 0x00, 0x00};
// `movsxd rsi, [rax+0x90]` (rax still the declaration): the element count.
constexpr unsigned char kQuadCount[] = {0x48, 0x63, 0xB0, 0x90, 0x00, 0x00, 0x00};
// `add qword [rbx+0x4F0], 0xC0`: each element, 4 vertices of 0x30 bytes, through the write pointer.
constexpr unsigned char kAdvance[] = {0x48, 0x81, 0x83, 0xF0, 0x04, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00};

struct Check {
    std::size_t offset;
    const unsigned char* bytes;
    std::size_t size;
};
constexpr Check kUpdateChecks[] = {
    {0x000, kPrologue, sizeof(kPrologue)},           {0x05B, kBlockTest, sizeof(kBlockTest)},
    {0x2EA, kIntensityRead, sizeof(kIntensityRead)}, {0x311, kIntensityWrite, sizeof(kIntensityWrite)},
    {0x605, kQuadCount, sizeof(kQuadCount)},         {0xA0A, kAdvance, sizeof(kAdvance)},
};

// ---- Engine layouts ----

// idRenderModelFlare
constexpr std::size_t kModelEntity = 0xA8; // its render entity, with the query slots
constexpr std::size_t kEntitySlotA = 0x108;
constexpr std::size_t kEntitySlotB = 0x110;
constexpr std::size_t kModelDecl = 0x4D0; // the flare declaration, element count at +0x90
constexpr std::size_t kDeclQuads = 0x90;
constexpr std::size_t kModelVertices = 0x4F0; // the write pointer into its vertex block
constexpr std::size_t kModelIntensity = 0x4F8;
// The world's frame number, raised once per render by the world update (`inc [[world+0x71DAE0]+0xBFD8]`,
// RVA 0x18E50BB).
constexpr std::size_t kWorldFrameOwner = 0x71DAE0;
constexpr std::size_t kFrameNumber = 0xBFD8;

using UpdateInViewFn = void (*)(void* model, void* view, void* sameView, void* context);

// Set once every hook is in; cleared for good if a rebuild faults.
std::atomic<bool> g_live{false};
std::atomic<bool> g_twoHalves{false};
UpdateInViewFn g_update = nullptr;
std::uintptr_t g_slotsResume = 0;
std::uintptr_t g_slotsAfterStores = 0;
const char* g_tag = "flares"; // the installing caller's, for the rebuild's lines

std::mutex g_bookMutex;
flares::FlareBook g_book; // under g_bookMutex

// The eyes' rebuilds run one at a time (each in its render's screen-views job); the mutex keeps it so.
std::mutex g_rebuildMutex;
std::array<flares::FlareRecord, flares::FlareBook::kCapacity> g_records{}; // under g_rebuildMutex

struct Counters {
    std::atomic<std::uint64_t> recorded{0};
    std::atomic<std::uint64_t> duplicate{0};
    std::atomic<std::uint64_t> full{0};
    std::atomic<std::uint64_t> late{0};
    std::atomic<std::uint64_t> unwritten{0};
} g_counters;

std::atomic<bool> g_loggedOdd{false};

// The flare job's call in progress on this thread, from the hook before it to the one after it.
struct Pending {
    std::uintptr_t view = 0;
    flares::FlareRecord record;
};
thread_local Pending t_pending;
// Set around this layer's own UpdateInView calls: the slot hook then hands it the record's slots.
thread_local bool t_reuseSlots = false;
thread_local bool t_skipStores = false; // the slots are view 1's twins: not stored in the render entity
thread_local std::uintptr_t t_slotA = 0;
thread_local std::uintptr_t t_slotB = 0;
thread_local int t_slotsReused = 0;

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

bool active() {
    return g_live.load(std::memory_order_acquire) && mp_guard::allowsGameTouch();
}

// The prepare's room for two halves (pe_flares.cpp) and the records' halves follow this one predicate,
// which only goes from true to false once frames render: a record that says two halves has the room.
bool twoHalvesNow() {
    return g_twoHalves.load(std::memory_order_acquire) && mp_guard::allowsGameTouch() && parallelEyesTouch();
}

// Before the flare job's call: what the engine's update starts from.
void onUpdateCall(const HookRegisters& r) {
    Pending& p = t_pending;
    p.record.model = 0;
    if (r.rbx == 0 || r.r10 == 0 || !active()) {
        return;
    }
    flares::FlareRecord f;
    f.model = r.rbx;
    f.halves = twoHalvesNow() ? 2 : 1;
    std::uintptr_t decl = 0;
    if (!readAt(f.model + kModelVertices, f.vertices) || !readAt(f.model + kModelIntensity, f.intensity) ||
        !readAt(f.model + kModelDecl, decl) || decl == 0 || !readAt(decl + kDeclQuads, f.quads) ||
        !readAt(r.r14 + kBlockContext, f.context) || !flares::plausible(f)) {
        return; // no vertex block (r_skipFlares, or no ring room): the update writes nothing
    }
    p.view = r.r10;
    p.record = f;
}

// After it: recorded for the render once the engine has written every quad, so a rebuild always follows the
// engine's own write.
void onUpdateDone(const HookRegisters& r) {
    Pending& p = t_pending;
    flares::FlareRecord f = p.record;
    p.record.model = 0;
    if (f.model == 0 || r.rbx != f.model) {
        return; // a skipped list entry, or nothing recorded before the call
    }
    std::uintptr_t now = 0;
    if (!readAt(f.model + kModelVertices, now) || now != flares::vertexEnd(f)) {
        add(g_counters.unwritten);
        return;
    }
    // The slots this call took, kept with the flare: another model of the same render entity may take others
    // before the rebuild.
    std::uintptr_t entity = 0;
    if (!readAt(f.model + kModelEntity, entity) || entity == 0 || !readAt(entity + kEntitySlotA, f.slotA) ||
        !readAt(entity + kEntitySlotB, f.slotB)) {
        add(g_counters.unwritten);
        return;
    }
    flares::RenderKey key;
    if (!flare_core::renderKeyOf(p.view, key)) {
        return;
    }
    flares::AddResult result = flares::AddResult::Added;
    {
        std::lock_guard lock(g_bookMutex);
        result = g_book.add(key, f);
    }
    switch (result) {
    case flares::AddResult::Added:
        add(g_counters.recorded);
        break;
    case flares::AddResult::Duplicate:
        add(g_counters.duplicate);
        break;
    case flares::AddResult::Full:
        add(g_counters.full);
        break;
    case flares::AddResult::Late:
        add(g_counters.late);
        break;
    }
}

// UpdateInView's first query slot call: in this layer's own calls the slots are the record's (the ones the
// engine's call of this render took for this flare, or view 1's twins of them), so the queries a view
// issues still go with its vertices and the global slot counter does not move.
void onQuerySlots(HookRegisters& r) {
    if (!t_reuseSlots) {
        return;
    }
    r.r12 = t_slotA;
    if (t_skipStores) {
        r.r13 = t_slotB;
        r.rsi = kStretchEsi;
        r.rax = kStretchEax;
        r.resumeAt = g_slotsAfterStores;
    } else {
        r.rax = t_slotB; // `mov r13, rax` after the resume
        r.resumeAt = g_slotsResume;
    }
    ++t_slotsReused;
}

enum class Rebuild { Done, Skipped, Changed, Odd, NoRoom, Fault };

// One half of a flare's quads again, from `view`, in the block the engine wrote this render. Half 0 (Route
// S's eye, Parallel Eye Rendering's view 0) leaves the write pointer as Route S always has; after half 1 it
// goes back where the engine's update left it. No C++ objects here: the call into the game is guarded.
Rebuild rebuildOne(const flares::FlareRecord& f, std::byte* view, int half, int slotView) {
    if (half >= f.halves) {
        return Rebuild::NoRoom;
    }
    auto* model = reinterpret_cast<std::byte*>(f.model);
    const std::uintptr_t end = flares::vertexEnd(f);
    const std::uintptr_t start = flares::halfStart(f, half);
    Rebuild result = Rebuild::Done;
    t_reuseSlots = false;
    t_skipStores = slotView != 0;
    t_slotsReused = 0;
    t_slotA = static_cast<std::uintptr_t>(flares::viewQuery(static_cast<std::int64_t>(f.slotA), slotView));
    t_slotB = static_cast<std::uintptr_t>(flares::viewQuery(static_cast<std::int64_t>(f.slotB), slotView));
    __try {
        std::uintptr_t now = 0;
        float engineIntensity = 0.0f;
        std::uintptr_t decl = 0;
        std::int32_t quads = 0;
        std::memcpy(&now, model + kModelVertices, sizeof(now));
        std::memcpy(&engineIntensity, model + kModelIntensity, sizeof(engineIntensity));
        std::memcpy(&decl, model + kModelDecl, sizeof(decl));
        if (decl != 0) {
            std::memcpy(&quads, reinterpret_cast<const std::byte*>(decl) + kDeclQuads, sizeof(quads));
        }
        if (now != end || quads != f.quads) {
            // Not the block the engine wrote this render, or a flare with another quad count now (the update
            // would write past the half): left alone.
            result = Rebuild::Changed;
        } else {
            std::memcpy(model + kModelVertices, &start, sizeof(start));
            std::memcpy(model + kModelIntensity, &f.intensity, sizeof(f.intensity));
            t_reuseSlots = true;
            g_update(model, view, view, reinterpret_cast<void*>(f.context));
            t_reuseSlots = false;
            std::memcpy(&now, model + kModelVertices, sizeof(now));
            if (t_slotsReused == 0) {
                // It returned before its slots and wrote nothing: the engine's quads and state stay.
                std::memcpy(model + kModelVertices, &end, sizeof(end));
                std::memcpy(model + kModelIntensity, &engineIntensity, sizeof(engineIntensity));
                result = Rebuild::Skipped;
            } else {
                if (now != start + static_cast<std::uintptr_t>(f.quads) * flares::kQuadBytes) {
                    result = Rebuild::Odd;
                }
                if (half != 0) {
                    std::memcpy(model + kModelVertices, &end, sizeof(end));
                }
            }
        }
    } __except (accessViolationOnly(GetExceptionCode())) {
        t_reuseSlots = false;
        result = Rebuild::Fault;
    }
    t_skipStores = false;
    return result;
}

bool matches(const GameImage& image, const std::byte* at, const unsigned char* bytes, std::size_t size) {
    return image.inText(at, size) && std::memcmp(at, bytes, size) == 0;
}

// The target of the `call rel32` at `at`, or nullptr outside .text.
const std::byte* callTarget(const GameImage& image, const std::byte* at) {
    if (!image.inText(at, 5) || std::to_integer<unsigned>(at[0]) != 0xE8) {
        return nullptr;
    }
    const std::byte* target = at + 5 + readI32(at + 1);
    return image.inText(target) ? target : nullptr;
}

bool installCore(const char* tag, const char* notInstalled, const char* what) {
    if (!mp_guard::allowsGameTouch()) {
        EVR_LOG("%s: the multiplayer guard is not armed; not installed", tag);
        return false;
    }
    GameImage image;
    if (!locateGameImage(image, tag)) {
        return false;
    }
    const std::byte* slots = findUnique(image, tag, "flare UpdateInView's query slots", kSlotsSignature);
    if (!slots) {
        EVR_LOG("%s: %s", tag, notInstalled);
        return false;
    }
    const std::byte* update = slots - kSlotsInUpdate;
    for (const Check& c : kUpdateChecks) {
        if (!matches(image, update + c.offset, c.bytes, c.size)) {
            EVR_LOG("%s: UpdateInView at RVA 0x%X differs at +0x%zX from what this relies on; %s", tag,
                    image.rva(update), c.offset, notInstalled);
            return false;
        }
    }
    const std::byte* allocator = callTarget(image, slots + kSlotsFirstCall);
    if (!allocator || allocator != callTarget(image, slots + kSlotsSecondCall) ||
        !matches(image, allocator, kAllocatorBytes, sizeof(kAllocatorBytes))) {
        EVR_LOG("%s: UpdateInView's slot calls at RVA 0x%X do not both go to the slot counter; %s", tag,
                image.rva(slots + kSlotsFirstCall), notInstalled);
        return false;
    }
    std::vector<const std::byte*> loops;
    std::size_t candidates = 0;
    if (const auto pattern = resolver::Pattern::parse(kLoopSignature)) {
        for (const std::size_t offset : resolver::findAll(image.text, *pattern)) {
            const std::byte* loop = image.text.data() + offset;
            ++candidates;
            if (callTarget(image, loop + kLoopCall) == update) {
                loops.push_back(loop);
            }
        }
    }
    if (loops.size() != 1) {
        EVR_LOG("%s: %zu of %zu update loop(s) call UpdateInView at RVA 0x%X (one expected); %s", tag,
                loops.size(), candidates, image.rva(update), notInstalled);
        return false;
    }
    const std::byte* loop = loops.front();
    g_update = reinterpret_cast<UpdateInViewFn>(const_cast<std::byte*>(update));
    g_slotsResume = reinterpret_cast<std::uintptr_t>(slots + kSlotsResume);
    g_slotsAfterStores = reinterpret_cast<std::uintptr_t>(slots + kSlotsAfterStores);
    g_tag = tag;
    // The hooks do nothing until g_live, set once all three are in.
    std::string error;
    struct Site {
        const std::byte* at;
        MidHookCallback read;
        MidHookEditCallback edit;
    };
    const Site sites[] = {{loop, &onUpdateCall, nullptr},
                          {loop + kLoopAfterCall, &onUpdateDone, nullptr},
                          {slots + kSlotsFirstCall, nullptr, &onQuerySlots}};
    for (const Site& s : sites) {
        auto* at = const_cast<std::byte*>(s.at);
        const bool ok = s.read ? installMidHook(at, s.read, error) : installMidHookEdit(at, s.edit, error);
        if (!ok) {
            EVR_LOG("%s: hook at RVA 0x%X failed: %s; %s", tag, image.rva(s.at), error.c_str(), notInstalled);
            return false;
        }
    }
    g_live.store(true, std::memory_order_release);
    EVR_LOG("%s: hooks at RVA 0x%X and 0x%X (the flare job's UpdateInView call) and 0x%X (its query slots; "
            "UpdateInView at RVA 0x%X)%s",
            tag, image.rva(sites[0].at), image.rva(sites[1].at), image.rva(sites[2].at), image.rva(update),
            what);
    return true;
}

} // namespace

namespace flare_core {

bool install(const char* tag, const char* notInstalled, const char* what) {
    static std::once_flag once;
    static bool installed = false;
    std::call_once(once, [&] { installed = installCore(tag, notInstalled, what); });
    return installed;
}

bool live() {
    return g_live.load(std::memory_order_acquire);
}

void startTwoHalves() {
    g_twoHalves.store(true, std::memory_order_release);
}

void stopTwoHalves() {
    g_twoHalves.store(false, std::memory_order_release);
}

bool twoHalves() {
    return twoHalvesNow();
}

bool renderKeyOf(std::uintptr_t view, flares::RenderKey& key) {
    std::uintptr_t world = 0;
    std::uintptr_t owner = 0;
    std::uint32_t frame = 0;
    if (!readAt(view + render_view_object::kOwningWorld, world) || world == 0 ||
        !readAt(world + kWorldFrameOwner, owner) || owner == 0 || !readAt(owner + kFrameNumber, frame)) {
        return false;
    }
    key = {view, frame};
    return true;
}

HalfResult rebuild(std::byte* view, const flares::RenderKey& key, int half, int slotView, RebuildDone done) {
    HalfResult out;
    if (view == nullptr || !active()) {
        return out;
    }
    std::lock_guard rebuildLock(g_rebuildMutex);
    flares::TakeResult taken;
    {
        std::lock_guard lock(g_bookMutex);
        taken = g_book.take(key, half, g_records);
    }
    out.taken = taken.count;
    out.otherRender = taken.otherRender;
    for (std::size_t i = 0; i < taken.count; ++i) {
        const flares::FlareRecord& f = g_records[i];
        switch (rebuildOne(f, view, half, slotView)) {
        case Rebuild::Done:
            ++out.done;
            if (done) {
                done(f, half);
            }
            break;
        case Rebuild::Skipped:
            ++out.skipped;
            break;
        case Rebuild::Changed:
            ++out.changed;
            break;
        case Rebuild::NoRoom:
            ++out.noRoom;
            break;
        case Rebuild::Odd:
            ++out.odd;
            if (!g_loggedOdd.exchange(true)) {
                EVR_LOG("%s: flare model %p wrote another number of quads than the engine's update (%d)",
                        g_tag, reinterpret_cast<void*>(f.model), f.quads);
            }
            break;
        case Rebuild::Fault: {
            g_live.store(false, std::memory_order_release);
            g_twoHalves.store(false, std::memory_order_release);
            const std::uintptr_t end = flares::vertexEnd(f);
            guardedCopy(reinterpret_cast<std::byte*>(f.model) + kModelVertices, &end, sizeof(end));
            _mm_sfence();
            EVR_LOG("%s: rebuilding flare model %p faulted; flares per eye off for the rest of the session",
                    g_tag, reinterpret_cast<void*>(f.model));
            out.fault = true;
            return out;
        }
        }
    }
    // UpdateInView writes the vertices with non-temporal stores.
    _mm_sfence();
    return out;
}

RecordCounts takeRecordCounts() {
    const auto take = [](std::atomic<std::uint64_t>& c) {
        return c.exchange(0);
    };
    RecordCounts r;
    r.recorded = take(g_counters.recorded);
    r.duplicate = take(g_counters.duplicate);
    r.full = take(g_counters.full);
    r.late = take(g_counters.late);
    r.unwritten = take(g_counters.unwritten);
    return r;
}

std::uint64_t rendersNotTaken() {
    std::lock_guard lock(g_bookMutex);
    return g_book.rendersNotTaken();
}

} // namespace flare_core

} // namespace evr::vkcore
