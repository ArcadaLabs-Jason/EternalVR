#include "vkcore/seq_locate.hpp"

#include "vkcore/log.hpp"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace evr::vkcore {

namespace {

// ---- Signatures (docs/rig-findings/stereo-routes.md sections 2.2 and 2.11; unique in build 25216728) ----

// Frame middle job (RVA 0x1CBA160): ... render-thread preparation, then `mov rdx, [rip + slot]` (RVA
// 0x1CBA17B) queues the frame-end job from the .data slot.
constexpr const char* kQueueSiteSignature =
    "48 8D 54 24 20 E8 ?? ?? ?? ?? 48 8B 8B 38 0F 00 00 48 8D 54 24 20 E8 ?? ?? ?? ?? 48 8B 15 ?? ?? ?? ?? "
    "48 8D 4C 24 20 4C 8B C7 E8 ?? ?? ?? ?? 48 8D 4C 24 20 E8 ?? ?? ?? ??";
constexpr std::size_t kQueueSlotDisp = 0x1E;
constexpr std::size_t kQueueSlotEnd = 0x22;

// Frame-end job (RVA 0x1CBA1C0): rsi = packet, rbx = [packet] = render system.
constexpr const char* kFrameEndSignature =
    "48 89 5C 24 20 56 B8 C0 38 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 B0 "
    "38 00 00 48 8B F1 33 D2 48 8D 4C 24 70 E8 ?? ?? ?? ?? 48 8B 1E 48 8B 83 38 0F 00 00";

// Its guard clear, `mov byte [rbx + 8], 0` (RVA 0x1CBA3AA), just before its job list ends.
constexpr const char* kGuardClearSignature =
    "48 8D 4C 24 70 C6 43 08 00 E8 ?? ?? ?? ?? 48 8B 8C 24 B0 38 00 00 48 33 CC E8 ?? ?? ?? ??";
constexpr std::ptrdiff_t kGuardClearMaxDistance = 0x400;

// Render one frame synchronously (RVA 0x1CBEA30, render system vtable slot 0x50): (renderSystem, arg,
// frameInfo, flag). It queues the render-frame job from its own .data slot (`mov rdx, [rip + slot]` at
// +0x89).
constexpr const char* kRenderOneSignature =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 B8 70 38 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 8B 05 ?? ?? "
    "?? ?? 48 33 C4 48 89 84 24 60 38 00 00 48 8B FA 48 8B D9 BA 01 00 00 00 48 8D 4C 24 20 41 0F B6 F1 49 "
    "8B "
    "E8 E8";
constexpr std::size_t kRenderOneJobLoad = 0x89;
constexpr std::size_t kRenderOneVtableSlot = 0x50;

// Render-frame job (RVA 0x1CB9EE0): the first job of every render frame's chain.
constexpr const char* kRenderFrameJobSignature =
    "48 89 5C 24 10 57 B8 70 38 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 60 "
    "38 00 00 48 8B F9 33 D2 48 8D 4C 24 20 E8 ?? ?? ?? ?? 48 8B 1F 80 7B 08 00";

// World-views pass (RVA 0x1C75CF0) and, in it, the previous-matrix store call (RVA 0x1C75D7C): the hook
// goes on the next instruction (RVA 0x1C75D81), where rdi = the idRenderView.
constexpr const char* kWorldViewsSignature =
    "4C 89 44 24 18 55 41 55 41 56 41 57 48 83 EC 28 48 8B E9 45 8B E9 48 8B 0D ?? ?? ?? ?? 4D 8B F8 4C 8B "
    "F2 E8 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 83 78 08 00 0F 85 ?? ?? ?? ?? 8B 85 28 15 00 00";
constexpr const char* kPrevStoreCallSignature = "48 8D 56 30 48 8B F8 E8 ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ?? "
                                                "48 8D 56 10 45 33 C0 48 8B CF E8 ?? ?? ?? ??";
constexpr std::size_t kPrevStoreCall = 0x0F;
constexpr std::size_t kPrevStoreHook = 0x14;
constexpr std::ptrdiff_t kPrevStoreMaxDistance = 0x200;
// Store previous matrices (RVA 0x1CE2340).
constexpr const char* kPrevStoreSignature =
    "0F 10 81 40 94 02 00 8B 81 6C 8A 02 00 0F 10 89 50 94 02 00 0F 11 81 80 94 02 00 0F 10 81 60 94 02 00 "
    "0F 11 89 90 94 02 00 0F 10 89 70 94 02 00 0F 11 81 A0 94 02 00";

// The render system object (`lea rcx, [rip + ...]` at RVA 0x17E8837 in the screen-view build).
constexpr const char* kRenderSystemSignature =
    "41 8B D4 48 8B CF E8 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ??";
// The render thread's swap (RVA 0x1CD868D): backend pointer, `inc [backend + 0xB0]`, the r_swapInterval
// cvar, then the swap call.
constexpr const char* kSwapSignature =
    "48 8B 05 ?? ?? ?? ?? B1 01 FF 80 B0 00 00 00 48 8B 05 ?? ?? ?? ?? 8B 50 08 E8";

// The section of the game module holding `address`: its characteristics, or 0 outside every section.
DWORD sectionCharacteristics(const GameText& text, const void* address, char (&name)[9]) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(text.base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(text.base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
    const std::uint32_t rva = rvaOf(text, address);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s) {
        if (rva >= s->VirtualAddress && rva < s->VirtualAddress + s->Misc.VirtualSize) {
            std::memcpy(name, s->Name, 8);
            name[8] = '\0';
            return s->Characteristics;
        }
    }
    name[0] = '\0';
    return 0;
}

} // namespace

bool locateSeqEngine(const GameText& text, SeqEngine& out) {
    const std::byte* queue =
        findUniqueInText(text, "stereo", "seq frame-end queue site", kQueueSiteSignature);
    const std::byte* frameEnd = findUniqueInText(text, "stereo", "seq frame-end job", kFrameEndSignature);
    const std::byte* guard = findUniqueInText(text, "stereo", "seq guard clear", kGuardClearSignature);
    const std::byte* renderOne =
        findUniqueInText(text, "stereo", "seq render one frame", kRenderOneSignature);
    const std::byte* renderJob =
        findUniqueInText(text, "stereo", "seq render-frame job", kRenderFrameJobSignature);
    const std::byte* worldViews =
        findUniqueInText(text, "stereo", "seq world-views pass", kWorldViewsSignature);
    const std::byte* storeCall =
        findUniqueInText(text, "stereo", "seq previous-matrix store call", kPrevStoreCallSignature);
    const std::byte* store =
        findUniqueInText(text, "stereo", "seq previous-matrix store", kPrevStoreSignature);
    const std::byte* rsSite = findUniqueInText(text, "stereo", "seq render system", kRenderSystemSignature);
    const std::byte* swap = findUniqueInText(text, "stereo", "seq render-thread swap", kSwapSignature);
    if (!queue || !frameEnd || !guard || !renderOne || !renderJob || !worldViews || !storeCall || !store ||
        !rsSite || !swap) {
        EVR_LOG("seq: a Route S signature is missing or not unique (another game build?); stereo off");
        return false;
    }
    // The .data slot the frame middle job reads must hold the frame-end job, in a writable data section.
    auto** slot = reinterpret_cast<void**>(
        const_cast<std::byte*>(ripTarget(queue + kQueueSlotDisp, queue + kQueueSlotEnd)));
    char section[9];
    const DWORD flags = sectionCharacteristics(text, slot, section);
    if (!(flags & IMAGE_SCN_MEM_WRITE) || (flags & IMAGE_SCN_MEM_EXECUTE)) {
        EVR_LOG(
            "seq: the frame-end slot (RVA 0x%X) is in section '%s' (0x%08lX), not writable data; stereo off",
            rvaOf(text, slot), section, flags);
        return false;
    }
    if (*slot != static_cast<const void*>(frameEnd)) {
        EVR_LOG("seq: the frame-end slot (RVA 0x%X) holds %p, not the frame-end job (RVA 0x%X); stereo off",
                rvaOf(text, slot), *slot, rvaOf(text, frameEnd));
        return false;
    }
    if (guard <= frameEnd || guard - frameEnd > kGuardClearMaxDistance) {
        EVR_LOG("seq: the guard clear (RVA 0x%X) is not inside the frame-end job; stereo off",
                rvaOf(text, guard));
        return false;
    }
    // The synchronous render must queue the same render-frame job the engine's own frames start with.
    const std::byte* load = renderOne + kRenderOneJobLoad;
    if (load[0] != std::byte{0x48} || load[1] != std::byte{0x8B} || load[2] != std::byte{0x15}) {
        EVR_LOG("seq: render-one's job load is not `mov rdx, [rip + x]`; stereo off");
        return false;
    }
    auto* const* jobSlot = reinterpret_cast<void* const*>(ripTarget(load + 3, load + 7));
    if (*jobSlot != static_cast<const void*>(renderJob)) {
        EVR_LOG("seq: render-one queues %p, not the render-frame job (RVA 0x%X); stereo off", *jobSlot,
                rvaOf(text, renderJob));
        return false;
    }
    // The render system object, and its vtable's render-one slot.
    auto* renderSystem = const_cast<std::byte*>(ripTarget(rsSite + 14, rsSite + 18));
    void** vtable = nullptr;
    std::memcpy(&vtable, renderSystem, sizeof(vtable));
    if (!vtable || vtable[kRenderOneVtableSlot / sizeof(void*)] != static_cast<const void*>(renderOne)) {
        EVR_LOG("seq: render system %p vtable %p slot 0x%zX is not render-one (RVA 0x%X); stereo off",
                static_cast<void*>(renderSystem), static_cast<void*>(vtable), kRenderOneVtableSlot,
                rvaOf(text, renderOne));
        return false;
    }
    // The previous-matrix store call inside the world-views pass, calling the store.
    if (storeCall <= worldViews || storeCall - worldViews > kPrevStoreMaxDistance ||
        relativeTarget(storeCall + kPrevStoreCall) != store) {
        EVR_LOG("seq: the previous-matrix store call (RVA 0x%X) does not call the store (RVA 0x%X) from the "
                "world-views pass; stereo off",
                rvaOf(text, storeCall), rvaOf(text, store));
        return false;
    }
    out.slot = slot;
    out.frameEnd = frameEnd;
    out.renderOne = renderOne;
    out.renderSystem = renderSystem;
    out.backend = reinterpret_cast<std::byte* const*>(ripTarget(swap + 3, swap + 7));
    out.swapIntervalCvar = reinterpret_cast<const std::byte* const*>(ripTarget(swap + 18, swap + 22));
    out.prevHookSite = storeCall + kPrevStoreHook;
    EVR_LOG(
        "seq: frame-end slot RVA 0x%X -> RVA 0x%X, render-one RVA 0x%X, render system %p, backend pointer "
        "RVA 0x%X",
        rvaOf(text, slot), rvaOf(text, frameEnd), rvaOf(text, renderOne), static_cast<void*>(renderSystem),
        rvaOf(text, out.backend));
    return true;
}

} // namespace evr::vkcore
