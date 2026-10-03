#include "vkcore/view_redirects.hpp"

#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/view_binning.hpp"
#include "vkcore/view_slots.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "view-redirects";

// ---- The command context table (RVA 0x667F018): 13 categories of 4 slots ----

constexpr std::uint32_t kTable = 0x667F018;
constexpr int kSlots = 4;
enum Category : int {
    kViewMain = 1,
    kMvpCulling = 2,
    kOffscreenWorldGui = 3,
    kBuildAccelerationStructures = 4,
    kClusterSetupShadows = 6,
    kDepthOcclusion = 7,
    kClusterSetupTiles = 8,
    kOpaque = 9,
    kEmissiveBlend = 10,
    kPostProcessGui = 11,
    kEndFrame = 12,
};

void** g_table = nullptr;
std::uintptr_t g_base = 0;
// The code bytes below are changed (applyViewRedirectPatches): from then on the split-category hooks keep
// view 0 on its two slots, guard or not, as the changed bytes expect.
std::atomic<bool> g_patched{false};

bool patched() {
    return g_patched.load(std::memory_order_acquire);
}

std::uintptr_t cell(int category, int slot) {
    return reinterpret_cast<std::uintptr_t>(g_table[category * kSlots + slot]);
}

std::uintptr_t cellAddress(int category, int slot) {
    return reinterpret_cast<std::uintptr_t>(&g_table[category * kSlots + slot]);
}

// The work belongs to view 1 when its render context is view 1's.
bool isView1(std::uintptr_t renderContext) {
    const auto context1 = reinterpret_cast<std::uintptr_t>(viewSlotsContext1());
    return context1 && renderContext == context1;
}

// View 1's work, and the guard allows changing it (view_slots.hpp, parallelEyesTouch).
bool view1Touch(std::uintptr_t renderContext) {
    return isView1(renderContext) && parallelEyesTouch();
}

// The first of a view's two slots in the categories with four.
int splitBase(std::uintptr_t renderContext) {
    return isView1(renderContext) ? 2 : 0;
}

// ---- Loads of a one-context category: view 1 gets slot 1 ----

using Reg = std::uintptr_t HookRegisters::*;
enum class Key : std::uint8_t {
    Context,     // the register holds the render context
    HolderEntry, // the register points at the holder's entry for the view
};
struct SingleSite {
    std::uint32_t rva; // the instruction after the load
    Reg loaded;
    Reg key;
    Key kind;
    int category;
};
constexpr SingleSite kSingleSites[] = {
    {0x1C5CEF8, &HookRegisters::rcx, &HookRegisters::rdi, Key::Context, kViewMain}, // render-view job
    {0x1C5D63F, &HookRegisters::rdx, &HookRegisters::r15, Key::Context, kBuildAccelerationStructures},
    {0x1C5D69F, &HookRegisters::rax, &HookRegisters::r15, Key::Context, kOffscreenWorldGui},
    {0x1C5DD29, &HookRegisters::rax, &HookRegisters::rdx, Key::Context, kClusterSetupShadows},
    {0x1C5C378, &HookRegisters::r15, &HookRegisters::rbp, Key::Context, kViewMain}, // 0x1C5C2F0
    {0x1C572E6, &HookRegisters::rbp, &HookRegisters::rax, Key::Context, kViewMain}, // no-world-view setup
    {0x1C5755D, &HookRegisters::rdx, &HookRegisters::rcx, Key::Context, kPostProcessGui},
    {0x1C57652, &HookRegisters::rax, &HookRegisters::rcx, Key::HolderEntry, kViewMain},   // 0x1C575F0
    {0x1C57A9E, &HookRegisters::rdx, &HookRegisters::rdi, Key::Context, kMvpCulling},     // Begin Frame setup
    {0x1C5A794, &HookRegisters::rbp, &HookRegisters::rcx, Key::HolderEntry, kMvpCulling}, // compute prepass
    {0x1C5B11F, &HookRegisters::rax, &HookRegisters::rsi, Key::Context, kOffscreenWorldGui},
    {0x1C5F1F1, &HookRegisters::rbx, &HookRegisters::rdi, Key::Context, kClusterSetupShadows},
    {0x1C5F1FF, &HookRegisters::r13, &HookRegisters::rdi, Key::Context, kClusterSetupTiles},
    {0x1C60C3D, &HookRegisters::r15, &HookRegisters::rdi, Key::Context, kPostProcessGui},
    {0x1C546F7, &HookRegisters::rcx, &HookRegisters::r12, Key::HolderEntry, kViewMain}, // 0x1C54650
};

template <std::size_t I>
void onSingleSite(HookRegisters& r) {
    constexpr SingleSite s = kSingleSites[I];
    const std::uintptr_t key = r.*s.key;
    const bool view1 =
        s.kind == Key::Context ? isView1(key) : key && isView1(*reinterpret_cast<const std::uintptr_t*>(key));
    if (view1 && parallelEyesTouch()) {
        r.*s.loaded = cell(s.category, 1);
    }
}

template <std::size_t... I>
constexpr std::array<MidHookEditCallback, sizeof...(I)> singleCallbacks(std::index_sequence<I...>) {
    return {&onSingleSite<I>...};
}
constexpr auto kSingleCallbacks = singleCallbacks(std::make_index_sequence<std::size(kSingleSites)>{});

// ---- The four-context categories: two slots per view ----
// These run for view 0 too, once the code bytes are changed (fan-out and last slot), guard or not: the
// changed bytes and these hooks together keep each view on its two slots.

// Depth and occlusion setup (0x1C5F810, rbx = render context): the last slot and the job's table.
void onDepthLast(HookRegisters& r) {
    if (!patched()) {
        return;
    }
    r.rbp = cell(kDepthOcclusion, splitBase(r.rbx) + 1);
}
void onDepthTable(HookRegisters& r) {
    if (!patched()) {
        return;
    }
    const std::uintptr_t table = cellAddress(kDepthOcclusion, splitBase(r.rbx));
    std::memcpy(reinterpret_cast<std::byte*>(r.rbx) + 0x6A7758, &table, sizeof(table));
}
// Opaque setup (0x1C60450, rbx = render context): the stage-tag loop's range, first and last slot, the table
// the jobs read, and the context handed on at 0x1C6065B.
void onOpaqueRange(HookRegisters& r) {
    if (!patched()) {
        return;
    }
    const int b = splitBase(r.rbx);
    r.rdi = cellAddress(kOpaque, b);
    r.rax = cell(kOpaque, b);
    r.r13 = cellAddress(kOpaque, b + 2);
    r.r15 = cell(kOpaque, b + 1);
}
void onOpaqueTable(HookRegisters& r) {
    if (!patched()) {
        return;
    }
    const std::uintptr_t table = cellAddress(kOpaque, splitBase(r.rbx));
    std::memcpy(reinterpret_cast<std::byte*>(r.rbx) + 0x6A7980, &table, sizeof(table));
}
void onOpaqueFirst(HookRegisters& r) {
    if (!patched()) {
        return;
    }
    r.rcx = cell(kOpaque, splitBase(r.rbx));
}
// Emissive and blend setup (0x1C60050, rbx = render context).
void onEmissiveRange(HookRegisters& r) {
    if (!patched()) {
        return;
    }
    const int b = splitBase(r.rbx);
    r.rdi = cellAddress(kEmissiveBlend, b);
    r.r15 = cell(kEmissiveBlend, b + 1);
    r.r13 = cellAddress(kEmissiveBlend, b + 2);
}
void onEmissiveTable(HookRegisters& r) {
    if (!patched()) {
        return;
    }
    const std::uintptr_t table = cellAddress(kEmissiveBlend, splitBase(r.rbx));
    std::memcpy(reinterpret_cast<std::byte*>(r.rbx) + 0x706CB0, &table, sizeof(table));
}
// The opaque dispatcher (0x1C5AF10, rsi = render context) and 0x1C575F0 (r13 = render context): the last
// slot.
void onOpaqueLastDispatcher(HookRegisters& r) {
    if (!patched()) {
        return;
    }
    r.rax = cell(kOpaque, splitBase(r.rsi) + 1);
}
void onOpaqueLastSetup(HookRegisters& r) {
    if (!patched()) {
        return;
    }
    r.rax = cell(kOpaque, splitBase(r.r13) + 1);
}
// 0x1C91430 (rcx = the view's opaque table pointer, rc + 0x6A7980): the last slot of that view's two.
void onOpaqueLastFromTable(HookRegisters& r) {
    if (!patched()) {
        return;
    }
    const auto* table = *reinterpret_cast<const std::uintptr_t* const*>(r.rcx);
    r.rdi = table[1];
}

// ---- The render target manager (object at [0x39B28A0]) in job 0x1C575F0 ----

// Each view's job 0x1C575F0 calls the manager's +0x38 (its frame update: from cvars and the render size it
// takes the frame's images, its fields are empty until then) and +0x40 (writes those images into the view's
// parameter state). Both views at once rebuilt the images under each other (null samplers in the states);
// the section from 0x1C5780C to 0x1C5782E now runs under a lock, one view at a time. Both views get the same
// images from it.
constexpr std::uint32_t kManagerUpdate = 0x1C5780C;
constexpr std::uint32_t kManagerDone = 0x1C5782E;
std::mutex g_managerMutex;

// Uncontended with one view. The section is straight-line in build 25216728 (two virtual calls, no branch),
// so a thread that passes 0x1C5780C reaches 0x1C5782E. The unlock is still only for the thread that took the
// lock (and a second update on it takes none), so a path into or out of the section elsewhere can never
// unlock a mutex its thread does not own or lock it twice.
thread_local bool t_managerHeld = false;

void onManagerUpdate(HookRegisters&) {
    if (viewSlotsActive() && !t_managerHeld) {
        g_managerMutex.lock();
        t_managerHeld = true;
    }
}

void onManagerDone(HookRegisters&) {
    if (t_managerHeld) {
        t_managerHeld = false;
        g_managerMutex.unlock();
    }
}

// ---- The texture streamer's gather (0x1D36D60: streamer, render views, count) ----

// Called once per frame with every render view; with two, the gather jobs' per-view items (static job data)
// corrupt the heap. Both eyes need the same textures: it gathers for view 0 only.
constexpr std::uint32_t kStreamerGather = 0x1D36D60;

void onStreamerGather(HookRegisters& r) {
    if (r.r8 > 1 && viewSlotsActive()) { // a no-op for one view
        r.r8 = 1;
    }
}

struct CustomSite {
    std::uint32_t rva;
    MidHookEditCallback callback;
};
constexpr CustomSite kCustomSites[] = {
    {0x1C5F852, &onDepthLast},          {0x1C5F87F, &onDepthTable},
    {0x1C604A7, &onOpaqueRange},        {0x1C6062A, &onOpaqueTable},
    {0x1C60662, &onOpaqueFirst},        {0x1C600A3, &onEmissiveRange},
    {0x1C600ED, &onEmissiveTable},      {0x1C5AF51, &onOpaqueLastDispatcher},
    {0x1C57880, &onOpaqueLastSetup},    {0x1C91459, &onOpaqueLastFromTable},
    {kManagerUpdate, &onManagerUpdate}, {kStreamerGather, &onStreamerGather},
    {kManagerDone, &onManagerDone},
};

// The jobs' fan-out (4 -> 2), their draw list split (ceil(n / 4) -> ceil(n / 2)), their last-slot checks
// (3 -> 1), and the finishing jobs' reads of the last slot ([table + 0x18] -> [table + 8]).
struct BytePatch {
    std::uint32_t rva;
    std::uint8_t from;
    std::uint8_t to;
};
constexpr BytePatch kBytePatches[] = {
    {0x1C59B3F, 4, 2}, {0x1C59D0A, 4, 2}, {0x1C5B3A0, 4, 2},       {0x1C5B3E7, 4, 2},
    {0x1C5A405, 4, 2}, {0x1C5A4DE, 4, 2}, {0x1C7308D, 3, 1},       {0x1C91101, 3, 1},
    {0x1C628FE, 3, 1}, {0x1C73090, 2, 1}, {0x1C91104, 2, 1},       {0x1C62901, 2, 1},
    {0x1C730BA, 3, 1}, {0x1C9113E, 3, 1}, {0x1C7334E, 0x18, 0x08}, {0x1C62CFC, 0x18, 0x08},
};

// ---- Begin Frame (job 0x1C57A20): each view resets its own slots ----

// At 0x1C57C87 in a category's reset loop: rbp = category, rcx = the table, rsi = first slot index, r13 = the
// state source (VIEW_MAIN), r15 its state block; [rsp + 0x70] holds the job data whose +0x30 is the view.
constexpr std::uint32_t kBeginFrameLoop = 0x1C57C87;

void onBeginFrameLoop(HookRegisters& r) {
    const int category = static_cast<int>(r.rbp);
    std::uintptr_t data = 0;
    std::memcpy(&data, reinterpret_cast<const std::byte*>(r.rsp) + 0x70, sizeof(data));
    std::uintptr_t view = 0;
    if (data) {
        std::memcpy(&view, reinterpret_cast<const std::byte*>(data) + 0x30, sizeof(view));
    }
    std::int32_t index = 0;
    if (view) {
        std::memcpy(&index, reinterpret_cast<const std::byte*>(view) + 0x28990, sizeof(index));
    }
    const bool view1 = index == 1 && viewSlotsContext1() && parallelEyesTouch();
    const bool split = category == kDepthOcclusion || category == kOpaque || category == kEmissiveBlend;
    if (split && patched()) {
        r.rsi = 2; // two slots per view
    }
    if (!view1) {
        return;
    }
    r.r13 = cell(kViewMain, 1);
    std::memcpy(&r.r15, reinterpret_cast<const std::byte*>(r.r13) + 0x100, sizeof(r.r15));
    if (category == kEndFrame) {
        r.rbp = kViewMain; // the frame's contexts are view 0's to reset: only view 1's VIEW_MAIN marker
        r.rcx = cellAddress(0, 1);
        return;
    }
    r.rcx = cellAddress(0, split ? 2 : 1);
}

// ---- The no-world-view Begin Frame (job 0x1C572A0): the same per-view slots ----

// Its reset loop (categories 2 to 12 but 5) at 0x1C5736A: r14 = category, r15 = the category's slot 0, rsi =
// the slot count; [rsp + 0xA8] holds the render context. The hook sets the first slot (rdi) and count (rsi)
// and resumes at the loop body (0x1C57370), or past the loop (0x1C573B4) for a category view 1 leaves alone.
constexpr std::uint32_t kNoWorldResetLoop = 0x1C5736A;

void onNoWorldResetLoop(HookRegisters& r) {
    const int category = static_cast<int>(r.r14);
    std::uintptr_t context = 0;
    std::memcpy(&context, reinterpret_cast<const std::byte*>(r.rsp) + 0xA8, sizeof(context));
    const bool view1 = view1Touch(context);
    const bool split = category == kDepthOcclusion || category == kOpaque || category == kEmissiveBlend;
    if (!patched()) {
        return;
    }
    if (view1 && (category == kEndFrame)) {
        r.resumeAt = g_base + 0x1C573B4;
        return;
    }
    if (!view1 && !split) {
        return;
    }
    r.rdi = cellAddress(category, split ? (view1 ? 2 : 0) : 1);
    r.rsi = split ? 2 : 1;
    r.resumeAt = g_base + 0x1C57370;
}

// ---- A one-entry per-view array (RVA 0x66E2E50, 16 bytes per view) ----

// The render-view job (0x1C5DDDB: movups [rax + rcx*8], rcx = index * 2) and the no-world-view setup
// (0x1C575D3: movups [rcx + rax*8 + 0x66E2E50], rcx = image base, rax = index * 2) store a view's 16 bytes
// there; view 1's would land on the renderer pointers at 0x66E2E60/68. Nothing reads the array by index, so
// view 1's go to a cell of ours.
alignas(16) std::byte g_viewCell1[16];

void onViewCellByRcx(HookRegisters& r) {
    if (r.rcx == 2 && viewSlotsActive()) {
        r.rax = reinterpret_cast<std::uintptr_t>(g_viewCell1) - 16;
    }
}
void onViewCellByRax(HookRegisters& r) {
    if (r.rax == 2 && viewSlotsActive()) {
        r.rcx = reinterpret_cast<std::uintptr_t>(g_viewCell1) - 16 - 0x66E2E50;
    }
}

// ---- Jobs for both views at once: view 0 only ----

// Skinning (0x1C58C40, 0x1C59130 -> 0x1BF2680 on the skinning manager 0x5BF1F10): both views draw the same
// tick, so view 0's skinning serves both (view 1's consumers do not wait for it). Ray
// tracing: 0x1C58D30 builds the blended BLASes in the engine's one static pool (0x5D63F20, no lock) into
// the shared async GPU Particles context; two views at once freed the same geometry lists, destroyed the same
// structures twice and recorded one command buffer from two threads. Its TLAS jobs 0x1C58EC0 and 0x1C59160
// build into the render world, which both views trace. View 1's jobs return at once.
struct View0Job {
    std::uint32_t rva;
    std::size_t block; // the job's parameter: render context + block
};
constexpr View0Job kView0Jobs[] = {
    {0x1C58C40, 0x6FC858}, {0x1C59130, 0x6FC858}, // skinning
    {0x1C58D30, 0x701898},                        // blended BLAS
    {0x1C58EC0, 0x7038E0}, {0x1C59160, 0x7038C0}, // blended and opaque TLAS
};
using JobFn = void (*)(void* param);
JobFn g_view0Jobs[std::size(kView0Jobs)] = {};
std::atomic<std::uint64_t> g_view1Skips[std::size(kView0Jobs)] = {};

template <std::size_t I>
void view0Job(void* param) {
    const auto* context1 = viewSlotsContext1();
    if (context1 && param == context1 + kView0Jobs[I].block && parallelEyesTouch()) {
        g_view1Skips[I].fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_view0Jobs[I](param);
}
constexpr JobFn kView0Wrappers[] = {&view0Job<0>, &view0Job<1>, &view0Job<2>, &view0Job<3>, &view0Job<4>};
static_assert(std::size(kView0Wrappers) == std::size(kView0Jobs));

// ---- Jobs with the renderer's scratch: one view at a time ----

// Job 0x1C58050 (queued by the render-view job with the record at 0x39A1D50) builds its surfaces in a static
// list (RVA 0x39A3640..0x39A3658). Job 0x1D2D3B0 copies the view's parameters into one global object whose
// list at +0x178 it reassigns (0x6031D0): two views at once freed its old buffer twice when a new world's
// first frames resized it (heap failure 'block not busy', run fh10). The two views' jobs take turns.
// Installed means locked (uncontended with one view).
constexpr std::uint32_t kSerialJobs[] = {0x1C58050, 0x1D2D3B0};
JobFn g_serialJobs[std::size(kSerialJobs)] = {};
std::mutex g_serialJobMutex[std::size(kSerialJobs)];

template <std::size_t I>
void serialJob(void* param) {
    if (!viewSlotsActive()) {
        g_serialJobs[I](param);
        return;
    }
    std::lock_guard lock(g_serialJobMutex[I]);
    g_serialJobs[I](param);
}
constexpr JobFn kSerialWrappers[] = {&serialJob<0>, &serialJob<1>};
static_assert(std::size(kSerialWrappers) == std::size(kSerialJobs));

// ---- The frame parity counter (RVA 0x66E2EA4) ----

// 0x1C54650 (r12 = render context) reads it (`mov eax, [counter]; and eax, 1` at 0x1C570E0, the view's
// ping-pong parity) and increments it (0x1C570F7) once per view: with two views each would keep one parity
// for good. View 1 counts on its own.
std::uint32_t g_parity1 = 0;

void onParityRead(HookRegisters& r) {
    if (view1Touch(r.r12)) {
        r.rax = g_parity1 & 1;
        r.resumeAt = g_base + 0x1C570E9;
    }
}
void onParityIncrement(HookRegisters& r) {
    if (view1Touch(r.r12)) {
        ++g_parity1;
        r.resumeAt = g_base + 0x1C570FD;
    }
}

// ---- Light and decal binning (view 1's waits for view 0's: view_binning.cpp) ----

constexpr std::size_t kLightBinningBlock = 0x5226F8; // the view's light block X = render context + this

// The binning's float table (RVA 0x66F0090, 8 entries of 0x18 bytes): written by 0x1CFC050 from the view's
// light block fill and read by nodes N3 (0x1D008E0) and N4 (0x1CFF580). Four `lea` of it; view 1's get a
// copy.
alignas(16) std::byte g_floatTable1[0xC0];

std::uintptr_t floatTable1(std::size_t offset) {
    return reinterpret_cast<std::uintptr_t>(g_floatTable1) + offset;
}
bool viewIndexIs1(std::uintptr_t view) {
    std::int32_t index = 0;
    std::memcpy(&index, reinterpret_cast<const std::byte*>(view) + 0x28990, sizeof(index));
    return index == 1 && viewSlotsContext1() && parallelEyesTouch();
}
void onFloatTableWriter(HookRegisters& r) { // 0x1CFC536: rbx = the view
    if (r.rbx && viewIndexIs1(r.rbx)) {
        r.rdi = floatTable1(8);
    }
}
void onFloatTableReaderA(HookRegisters& r) { // 0x1CFF709: r14 = render context + 0x4D9C70
    if (view1Touch(r.r14 - 0x4D9C70)) {
        r.r12 = floatTable1(8);
    }
}
void onFloatTableReaderB(HookRegisters& r) { // 0x1CFF718
    if (view1Touch(r.r14 - 0x4D9C70)) {
        r.r13 = floatTable1(0x10);
    }
}
void onFloatTableReaderC(HookRegisters& r) { // 0x1D00E44: r13 = X
    if (view1Touch(r.r13 - kLightBinningBlock)) {
        r.r9 = floatTable1(0);
    }
}

// ---- Install ----

bool hook(const std::byte* base, std::uint32_t rva, MidHookEditCallback callback) {
    std::string error;
    if (!installMidHookEdit(const_cast<std::byte*>(base + rva), callback, error)) {
        EVR_LOG("%s: hook at RVA 0x%X failed: %s", kTag, rva, error.c_str());
        return false;
    }
    return true;
}

} // namespace

bool installViewRedirects(const std::byte* base) {
    g_table = reinterpret_cast<void**>(const_cast<std::byte*>(base + kTable));
    g_base = reinterpret_cast<std::uintptr_t>(base);
    for (std::size_t i = 0; i < std::size(kSingleSites); ++i) {
        if (!hook(base, kSingleSites[i].rva, kSingleCallbacks[i])) {
            return false;
        }
    }
    for (const CustomSite& s : kCustomSites) {
        if (!hook(base, s.rva, s.callback)) {
            return false;
        }
    }
    if (!hook(base, kBeginFrameLoop, &onBeginFrameLoop) || !hook(base, 0x1C5DDDB, &onViewCellByRcx) ||
        !hook(base, 0x1C575D3, &onViewCellByRax) || !hook(base, 0x1C570E0, &onParityRead) ||
        !hook(base, 0x1C570F7, &onParityIncrement) || !installViewBinning(base) ||
        !hook(base, 0x1CFC536, &onFloatTableWriter) || !hook(base, 0x1CFF709, &onFloatTableReaderA) ||
        !hook(base, 0x1CFF718, &onFloatTableReaderB) || !hook(base, 0x1D00E44, &onFloatTableReaderC) ||
        !hook(base, kNoWorldResetLoop, &onNoWorldResetLoop)) {
        return false;
    }
    // The job wrappers: serial jobs first, then view 0's.
    for (std::size_t i = 0; i < std::size(kSerialJobs) + std::size(kView0Jobs); ++i) {
        const bool serial = i < std::size(kSerialJobs);
        const std::size_t j = serial ? i : i - std::size(kSerialJobs);
        const std::uint32_t rva = serial ? kSerialJobs[j] : kView0Jobs[j].rva;
        std::string error;
        if (!installInlineHook(const_cast<std::byte*>(base + rva),
                               reinterpret_cast<void*>(serial ? kSerialWrappers[j] : kView0Wrappers[j]),
                               reinterpret_cast<void**>(serial ? &g_serialJobs[j] : &g_view0Jobs[j]),
                               error)) {
            EVR_LOG("%s: job hook at RVA 0x%X failed: %s", kTag, rva, error.c_str());
            return false;
        }
    }
    if (parallelEyesSettings().testFail == parallel_eyes::TestFail::Redirects) {
        // As a failed redirect hook would: the block moved, view 1's contexts in with their counts raised,
        // the code bytes not changed and the hooks installed inert.
        EVR_LOG("%s: a hook fails here, before the code bytes (ETERNALVR_TEST_INSTALL_FAIL=redirects)", kTag);
        return false;
    }
    EVR_LOG("%s: %zu one-context loads, %zu split sites, Begin Frame per view, view 0's jobs, binning edges",
            kTag, std::size(kSingleSites), std::size(kCustomSites));
    return true;
}

bool prepareViewRedirectPatches(const std::byte* base, std::vector<CodeRange>& writes) {
    for (const BytePatch& b : kBytePatches) {
        if (std::to_integer<std::uint8_t>(base[b.rva]) != b.from) {
            EVR_LOG("%s: RVA 0x%X is not the expected byte; not changed", kTag, b.rva);
            return false;
        }
    }
    for (const BytePatch& b : kBytePatches) {
        writes.push_back(CodeRange{const_cast<std::byte*>(base + b.rva), 1});
    }
    return true;
}

void applyViewRedirectPatches(const std::byte* base) {
    for (const BytePatch& b : kBytePatches) {
        *const_cast<std::byte*>(base + b.rva) = std::byte{b.to};
    }
    g_patched.store(true, std::memory_order_release);
    EVR_LOG("%s: %zu fan-out bytes changed: two command contexts per view in the split categories", kTag,
            std::size(kBytePatches));
}

void viewRedirectsDispatchStart() {
    viewBinningFrameStart();
}

void viewRedirectsLogCounts() {
    viewBinningLogCounts();
    EVR_LOG(
        "%s: view 1 skipped: skinning %llu + %llu, blended BLAS %llu, blended TLAS %llu, opaque TLAS %llu",
        kTag, static_cast<unsigned long long>(g_view1Skips[0].load()),
        static_cast<unsigned long long>(g_view1Skips[1].load()),
        static_cast<unsigned long long>(g_view1Skips[2].load()),
        static_cast<unsigned long long>(g_view1Skips[3].load()),
        static_cast<unsigned long long>(g_view1Skips[4].load()));
}

} // namespace evr::vkcore
