#include "vkcore/view_contexts.hpp"

#include "vkcore/log.hpp"

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <vector>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "view-contexts";

// The category counts, one table per loop over the context table (build 25216728): build, pools and
// parameter state; begin, end, frame flip and submit; resource-state resolve; the barrier chain's
// predecessor; the end-frame flush; resize. The Begin Frame jobs' table (0x2E9F490) stays as it is: view 1's
// contexts are reset by the view that records into them.
constexpr std::uint32_t kCountTables[] = {0x2EB3DB0, 0x2E99930, 0x2E9BBB8, 0x2EA17B8, 0x2EB5998, 0x2EBB4D0};
constexpr int kCategories = 13;
constexpr std::int32_t kEngineCounts[kCategories] = {1, 1, 1, 1, 1, 1, 1, 4, 1, 4, 4, 1, 1};

// The per-view categories with one context: VIEW_MAIN, MVP_CULLING, OFFSCREEN_WORLD_GUI,
// BUILD_ACCELERATION_STRUCTURES (the view's ray tracing structures), CLUSTER_SETUP_SHADOWS,
// CLUSTER_SETUP_TILES, POST_PROCESS_GUI. MAIN, STREAMING and END_FRAME are the frame's; DEPTH_OCCLUSION,
// OPAQUE_FOG_DEFERRED_LIGHTATLAS and EMISSIVE_BLEND already use all four slots.
constexpr int kSecondSlotCategories[] = {1, 2, 3, 4, 6, 8, 11};

// The context table (13 categories of 4 slots) and how the renderer's start-up (RVA 0x1CCE780) fills it,
// which happens before the game's vkCreateInstance; the command pools and buffers (RVA 0x1C663A0) come after
// it.
constexpr std::uint32_t kTable = 0x667F018;
constexpr std::size_t kSlots = 4;
constexpr std::uint32_t kCategoryNames = 0x39AB150; // const char*[13]
constexpr std::uint32_t kAllocate = 0x357240;       // (size, memory tag)
constexpr std::uint32_t kContextCtor = 0x1C65670;   // (memory, name, category, slot, category)
constexpr std::size_t kContextSize = 0x698;
constexpr int kMemoryTag = 0x45;

using AllocateFn = void* (*)(std::size_t size, int tag);
using ContextCtorFn = void* (*)(void* memory, const char* name, int category, int slot, int sameCategory);

// Prepared by prepareViewContexts: the memory of view 1's contexts, one per category of
// kSecondSlotCategories.
const std::byte* g_base = nullptr;
std::array<void*, std::size(kSecondSlotCategories)> g_memory{};

} // namespace

bool prepareViewContexts(const std::byte* base, std::vector<CodeRange>& writes) {
    for (const std::uint32_t rva : kCountTables) {
        if (std::memcmp(base + rva, kEngineCounts, sizeof(kEngineCounts)) != 0) {
            EVR_LOG("%s: category counts at RVA 0x%X are not the engine's; off", kTag, rva);
            return false;
        }
    }
    auto* const* table = reinterpret_cast<void* const*>(base + kTable);
    for (const int category : kSecondSlotCategories) {
        void* const* slot = table + category * kSlots;
        if (!slot[0] || slot[1]) {
            EVR_LOG("%s: category %d's slots are not as expected (%p, %p); off", kTag, category, slot[0],
                    slot[1]);
            return false;
        }
    }
    // The engine's allocator has no free the layer knows: memory taken for an install that stops before
    // installViewContexts stays unused (7 x 0x698 bytes).
    const auto allocate = reinterpret_cast<AllocateFn>(const_cast<std::byte*>(base + kAllocate));
    for (void*& memory : g_memory) {
        memory = allocate(kContextSize, kMemoryTag);
        if (!memory) {
            EVR_LOG("%s: no memory for a context", kTag);
            return false;
        }
    }
    for (const std::uint32_t rva : kCountTables) {
        writes.push_back(
            CodeRange{const_cast<std::byte*>(base + rva), sizeof(kEngineCounts), PAGE_READWRITE});
    }
    g_base = base;
    return true;
}

void installViewContexts() {
    auto** table = reinterpret_cast<void**>(const_cast<std::byte*>(g_base + kTable));
    auto* const* names = reinterpret_cast<const char* const*>(g_base + kCategoryNames);
    const auto construct = reinterpret_cast<ContextCtorFn>(const_cast<std::byte*>(g_base + kContextCtor));
    for (std::size_t i = 0; i < std::size(kSecondSlotCategories); ++i) {
        const int category = kSecondSlotCategories[i];
        char name[96];
        std::snprintf(name, sizeof(name), "%s 1", names[category]);
        table[category * kSlots + 1] = construct(g_memory[i], name, category, 1, category);
    }
    for (const std::uint32_t rva : kCountTables) {
        auto* counts = reinterpret_cast<std::int32_t*>(const_cast<std::byte*>(g_base + rva));
        for (const int category : kSecondSlotCategories) {
            counts[category] = 2;
        }
    }
    EVR_LOG("%s: a second context in %zu categories", kTag, std::size(kSecondSlotCategories));
}

} // namespace evr::vkcore
