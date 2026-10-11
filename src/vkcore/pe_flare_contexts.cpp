#include "vkcore/pe_flare_contexts.hpp"

#include "vkcore/pe_flare_sites.hpp"

#include <array>
#include <iterator>

namespace evr::vkcore::pe_flare_contexts {

namespace {

// The command contexts by view: the per-view categories with one context (slot 1 is view 1's) and the
// three with four (slots 2 and 3 are view 1's), as view_redirects.cpp splits them.
constexpr int kOneContext[] = {1, 2, 3, 4, 6, 8, 11};
constexpr int kFourContexts[] = {7, 9, 10};
constexpr int kOcclusionCategory = 7;
constexpr int kEmissiveCategory = 10;
constexpr std::size_t kViewContexts = std::size(kOneContext) + 2 * std::size(kFourContexts);
std::array<std::array<std::uintptr_t, kViewContexts>, 2> g_contexts{};
std::array<std::array<std::uintptr_t, 2>, 2> g_occlusionContexts{};
std::array<std::array<std::uintptr_t, 2>, 2> g_copyContexts{};

// Each view's command context cells of the game's table: (view, its index in g_contexts, category, slot).
template <typename F>
void forEachViewCell(F&& f) {
    for (int view = 0; view < 2; ++view) {
        std::size_t n = 0;
        for (const int c : kOneContext) {
            f(view, n++, c, view);
        }
        for (const int c : kFourContexts) {
            f(view, n++, c, 2 * view);
            f(view, n++, c, 2 * view + 1);
        }
    }
}

// The view whose command context `context` is (0 or 1), or -1.
template <std::size_t N>
int viewOf(const std::array<std::array<std::uintptr_t, N>, 2>& contexts, std::uintptr_t context) {
    for (int view = 0; view < 2; ++view) {
        for (const std::uintptr_t c : contexts[static_cast<std::size_t>(view)]) {
            if (c != 0 && c == context) {
                return view;
            }
        }
    }
    return -1;
}

std::uintptr_t cellAt(std::uintptr_t base, int category, int slot) {
    auto* const* table = reinterpret_cast<void* const*>(base + pe_flare_sites::kContextTable);
    return reinterpret_cast<std::uintptr_t>(table[category * kTableSlots + slot]);
}

} // namespace

bool capture(const std::byte* base) {
    const auto b = reinterpret_cast<std::uintptr_t>(base);
    forEachViewCell([&](int view, std::size_t n, int category, int slot) {
        g_contexts[static_cast<std::size_t>(view)][n] = cellAt(b, category, slot);
    });
    for (int view = 0; view < 2; ++view) {
        for (int i = 0; i < 2; ++i) {
            g_occlusionContexts[static_cast<std::size_t>(view)][static_cast<std::size_t>(i)] =
                cellAt(b, kOcclusionCategory, 2 * view + i);
            g_copyContexts[static_cast<std::size_t>(view)][static_cast<std::size_t>(i)] =
                cellAt(b, kEmissiveCategory, 2 * view + i);
        }
    }
    for (const auto& contexts : g_contexts) {
        for (const std::uintptr_t c : contexts) {
            if (c == 0) {
                return false;
            }
        }
    }
    return true;
}

int bindView(std::uintptr_t context) {
    return viewOf(g_contexts, context);
}

int occlusionView(std::uintptr_t context) {
    return viewOf(g_occlusionContexts, context);
}

int copyView(std::uintptr_t context) {
    return viewOf(g_copyContexts, context);
}

int changedCell(std::uintptr_t base) {
    int changed = -1;
    forEachViewCell([&](int view, std::size_t n, int category, int slot) {
        if (changed < 0 && cellAt(base, category, slot) != g_contexts[static_cast<std::size_t>(view)][n]) {
            changed = category * kTableSlots + slot;
        }
    });
    return changed;
}

} // namespace evr::vkcore::pe_flare_contexts
