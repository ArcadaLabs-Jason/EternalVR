#include "vkcore/seq_stack.hpp"

#include "stereo_seq/stack_budget.hpp"
#include "vkcore/seq_hooks.hpp"

#include <windows.h>

#include <intrin.h>

#include <atomic>

namespace evr::vkcore {

namespace {

std::atomic<std::uintptr_t> g_outerSp{0};
std::atomic<DWORD> g_outerThread{0};
std::atomic<std::size_t> g_deepestNested{0};
std::atomic<std::size_t> g_leastHeadroom{0};

} // namespace

namespace seq_stack {

void enter(std::uintptr_t sp) {
    g_outerSp.store(sp);
    g_outerThread.store(GetCurrentThreadId());
}

void leave() {
    g_outerThread.store(0);
}

void noteHeadroom(std::size_t headroom) {
    std::size_t least = g_leastHeadroom.load();
    while ((least == 0 || headroom < least) && !g_leastHeadroom.compare_exchange_weak(least, headroom)) {
    }
}

std::size_t deepestNested() {
    return g_deepestNested.load();
}

std::size_t leastHeadroom() {
    return g_leastHeadroom.load();
}

} // namespace seq_stack

void seqNoteNestedStack() {
    if (g_outerThread.load() != GetCurrentThreadId()) {
        return; // eye R's chain moved to another thread here: nothing nested on the wrapper's stack
    }
    const std::size_t depth = stereo_seq::nestedDepth(
        g_outerSp.load(), reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress()));
    std::size_t deepest = g_deepestNested.load();
    while (depth > deepest && !g_deepestNested.compare_exchange_weak(deepest, depth)) {
    }
}

} // namespace evr::vkcore
