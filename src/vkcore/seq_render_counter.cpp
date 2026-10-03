// The backend frame counter the render-view job read for the latest render (seq_hooks.hpp).

#include "vkcore/seq_hooks.hpp"

#include <atomic>
#include <cstdint>
#include <optional>

namespace evr::vkcore {

namespace {

// The counter in the low 32 bits; kNoted once one was noted.
constexpr std::uint64_t kNoted = std::uint64_t{1} << 32;
std::atomic<std::uint64_t> g_renderView{0};

} // namespace

void seqNoteRenderViewCounter(std::uint32_t counter) {
    g_renderView.store(kNoted | counter, std::memory_order_release);
}

std::optional<std::uint32_t> seqRenderViewCounter() {
    const std::uint64_t value = g_renderView.load(std::memory_order_acquire);
    if ((value & kNoted) == 0) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

} // namespace evr::vkcore
