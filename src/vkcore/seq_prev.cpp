#include "vkcore/seq_prev.hpp"

#include <mutex>

namespace evr::vkcore::seq_prev {

namespace {

std::mutex g_mutex;
stereo_seq::PrevMatrixBook g_book(stereo_seq::previousMatrixRanges());

} // namespace

void afterStore(std::byte* view, stereo_seq::Eye eye, std::uint32_t renderFrame) {
    std::lock_guard lock(g_mutex);
    g_book.afterStore(view, eye, renderFrame);
}

bool undoRewrite(std::byte* view) {
    std::lock_guard lock(g_mutex);
    return g_book.undoRewrite(view);
}

stereo_seq::PrevMatrixBook::Stats stats() {
    std::lock_guard lock(g_mutex);
    return g_book.stats();
}

} // namespace evr::vkcore::seq_prev
