#include "vkcore/view_block.hpp"

#include "vkcore/log.hpp"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <utility>
#include <vector>

namespace evr::vkcore {

namespace {

// The renderer's per-view state block (docs/VR_STEREO.md, "Per-view state"): one 0xAF8-byte entry per
// render view, indexed by idRenderView::viewIndex and initialised for r_maxRenderViews entries by RVA
// 0x1CFD310. Build 25216728 reserves static storage for one entry only (RVA 0x66EF4F0; the next global
// starts at +0xAF8), so a second view would write over other renderer globals. The init loop (RVA
// 0x1CFD321) names the block:
//   mov rax, [rip + r_maxRenderViews]; mov [rsp+40h], rbx; xor ebx, ebx; cmp [rax+8], ebx; jle ...;
//   mov [rsp+30h], rdi; lea rdi, [rip + block]; mov eax, ebx; imul rcx, rax, 0AF8h
constexpr const char* kInitLoopSignature = "48 8B 05 ?? ?? ?? ?? 48 89 5C 24 40 33 DB 39 58 08 7E ?? 48 89 "
                                           "7C 24 30 48 8D 3D ?? ?? ?? ?? 8B C3 48 69 C8 F8 0A 00 00";
constexpr std::size_t kInitLoopLeaDisp = 0x1B;
constexpr std::size_t kInitLoopLeaEnd = 0x1F;
constexpr std::size_t kEntrySize = 0xAF8;
// Code references to the block in this build: 14 RIP-relative LEAs (the static constructor and its
// atexit destructor, the init loop and eleven per-view accessors) (docs/rig-findings/stereo-reentry.md, live
// section), and 7 image-relative displacements in the light binning (0x1CEF930, 0x1CEFCB0, 0x1D015D0), which
// address the block as [image base register + disp32]: the displacements' RVAs below.
constexpr std::size_t kExpectedReferences = 14;
constexpr std::uint32_t kImageRelativeDisps[] = {0x1CEFC5E, 0x1CF01F9, 0x1D016DA, 0x1D0197A,
                                                 0x1D019A0, 0x1D019D5, 0x1D01A0C};

using EntryCtorFn = void* (*)(void* entry);

struct LeaRef {
    std::byte* instruction;
    std::uint32_t offset; // target - block
};

std::int32_t read32(const std::byte* at) {
    std::int32_t v = 0;
    std::memcpy(&v, at, sizeof(v));
    return v;
}

// Every REX.W LEA with a RIP-relative operand whose target lies inside [block, block + size).
std::vector<LeaRef> findLeas(const GameText& text, const std::byte* block, std::size_t size) {
    std::vector<LeaRef> refs;
    const std::byte* data = text.bytes.data();
    const std::size_t n = text.bytes.size();
    for (std::size_t i = 0; i + 7 <= n; ++i) {
        const auto rex = std::to_integer<std::uint8_t>(data[i]);
        if ((rex != 0x48 && rex != 0x4C) || std::to_integer<std::uint8_t>(data[i + 1]) != 0x8D ||
            (std::to_integer<std::uint8_t>(data[i + 2]) & 0xC7) != 0x05) {
            continue;
        }
        const std::byte* target = data + i + 7 + read32(data + i + 3);
        if (target >= block && target < block + size) {
            refs.push_back({const_cast<std::byte*>(data + i), static_cast<std::uint32_t>(target - block)});
        }
    }
    return refs;
}

// Memory for the new block within +-2 GB of every referencing instruction.
std::byte* allocateNear(const GameText& text, std::size_t size) {
    const auto* start = text.bytes.data();
    const auto* end = start + text.bytes.size();
    const std::uintptr_t low = reinterpret_cast<std::uintptr_t>(end) > 0x7FFF0000u
                                   ? reinterpret_cast<std::uintptr_t>(end) - 0x7FFF0000u
                                   : 0x10000u;
    const std::uintptr_t high = reinterpret_cast<std::uintptr_t>(start) + 0x7FFF0000u - size;
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const std::uintptr_t step = info.dwAllocationGranularity;
    // Upward from the end of the module first (the module's own data follows its code), then downward.
    for (std::uintptr_t at = (reinterpret_cast<std::uintptr_t>(end) + step) & ~(step - 1); at < high;
         at += step) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<void*>(at), &mbi, sizeof(mbi))) {
            break;
        }
        if (mbi.State == MEM_FREE) {
            if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), size, MEM_RESERVE | MEM_COMMIT,
                                       PAGE_READWRITE)) {
                return static_cast<std::byte*>(p);
            }
        }
    }
    for (std::uintptr_t at = (reinterpret_cast<std::uintptr_t>(start) - step) & ~(step - 1); at > low;
         at -= step) {
        if (void* p =
                VirtualAlloc(reinterpret_cast<void*>(at), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
            return static_cast<std::byte*>(p);
        }
    }
    return nullptr;
}

// Prepared by preparePerViewBlock for movePerViewBlock.
struct Prepared {
    std::vector<LeaRef> refs;
    std::byte* fresh = nullptr;
    std::intptr_t freshRva = 0;
    std::uint32_t blockRva = 0;
    int views = 0;
    EntryCtorFn ctor = nullptr;
    const std::byte* base = nullptr;
};
Prepared g_prepared;

} // namespace

bool preparePerViewBlock(const GameText& text, int views, std::vector<CodeRange>& writes) {
    const std::byte* loop =
        findUniqueInText(text, "view-slots", "per-view block init loop", kInitLoopSignature);
    if (!loop || views < 2) {
        return false;
    }
    const std::byte* block = loop + kInitLoopLeaEnd + read32(loop + kInitLoopLeaDisp);
    std::vector<LeaRef> refs = findLeas(text, block, kEntrySize);
    // The entry constructor: the static initializer's "lea rcx, [rip + block]; call ctor".
    EntryCtorFn ctor = nullptr;
    for (const LeaRef& r : refs) {
        if (r.offset == 0 && std::to_integer<std::uint8_t>(r.instruction[2]) == 0x0D &&
            std::to_integer<std::uint8_t>(r.instruction[7]) == 0xE8) {
            ctor = reinterpret_cast<EntryCtorFn>(r.instruction + 12 + read32(r.instruction + 8));
        }
    }
    EVR_LOG("stereo: per-view block at RVA 0x%X, %zu reference(s), entry constructor %s", rvaOf(text, block),
            refs.size(), ctor ? "found" : "not found");
    if (refs.size() != kExpectedReferences || !ctor) {
        EVR_LOG("stereo: the per-view block's references are not the known %zu; one view",
                kExpectedReferences);
        return false;
    }
    const std::uint32_t blockRva = rvaOf(text, block);
    for (const std::uint32_t rva : kImageRelativeDisps) {
        const auto value = static_cast<std::uint32_t>(read32(text.base + rva));
        if (value < blockRva || value >= blockRva + kEntrySize) {
            EVR_LOG("stereo: RVA 0x%X does not address the per-view block (0x%X); one view", rva, value);
            return false;
        }
    }
    const std::size_t size = kEntrySize * static_cast<std::size_t>(views);
    std::byte* fresh = allocateNear(text, size);
    if (!fresh) {
        EVR_LOG("stereo: no memory near the game module for %d per-view entries; one view", views);
        return false;
    }
    for (const LeaRef& r : refs) {
        const std::intptr_t distance = (fresh + r.offset) - (r.instruction + 7);
        if (distance < INT32_MIN || distance > INT32_MAX) {
            EVR_LOG("stereo: the new per-view block is out of reach of RVA 0x%X; one view",
                    rvaOf(text, r.instruction));
            VirtualFree(fresh, 0, MEM_RELEASE);
            return false;
        }
    }
    const std::intptr_t freshRva = fresh - text.base;
    if (freshRva < 0 || freshRva + static_cast<std::intptr_t>(size) > INT32_MAX) {
        EVR_LOG("stereo: the new per-view block is not above the game module within 2 GB; one view");
        VirtualFree(fresh, 0, MEM_RELEASE);
        return false;
    }
    for (const LeaRef& r : refs) {
        writes.push_back(CodeRange{r.instruction + 3, 4});
    }
    for (const std::uint32_t rva : kImageRelativeDisps) {
        writes.push_back(CodeRange{const_cast<std::byte*>(text.base + rva), 4});
    }
    g_prepared = Prepared{std::move(refs), fresh, freshRva, blockRva, views, ctor, text.base};
    return true;
}

void movePerViewBlock() {
    const Prepared& p = g_prepared;
    // Every entry constructed as the static initializer constructed the first; the renderer initialises
    // them (the loop above) when it starts, after this.
    for (int i = 0; i < p.views; ++i) {
        p.ctor(p.fresh + kEntrySize * static_cast<std::size_t>(i));
    }
    for (const LeaRef& r : p.refs) {
        const auto disp = static_cast<std::int32_t>((p.fresh + r.offset) - (r.instruction + 7));
        std::memcpy(r.instruction + 3, &disp, sizeof(disp));
    }
    for (const std::uint32_t rva : kImageRelativeDisps) {
        auto* at = const_cast<std::byte*>(p.base + rva);
        const auto disp =
            static_cast<std::int32_t>(p.freshRva + (static_cast<std::uint32_t>(read32(at)) - p.blockRva));
        std::memcpy(at, &disp, sizeof(disp));
    }
    EVR_LOG("stereo: per-view block moved to %p for %d views (%zu references repointed, %zu image-relative)",
            static_cast<void*>(p.fresh), p.views, p.refs.size(), std::size(kImageRelativeDisps));
}

void abandonPerViewBlock() {
    if (g_prepared.fresh) {
        VirtualFree(g_prepared.fresh, 0, MEM_RELEASE);
    }
    g_prepared = Prepared{};
}

} // namespace evr::vkcore
