#pragma once

// Parallel Eye Rendering's eye 0 copies (view_snapshot.hpp) read the swapchain image view 0's command buffer
// leaves in PRESENT_SRC, moving it to TRANSFER_SRC and back, in a batch appended after the game's batches.
// Whatever the game orders after its batches (the present of that image, the next write after the image is
// acquired again) must wait for that copy as well, so the signals of the game's batches from the first one
// that carries a copied command buffer on move onto the appended batch: a semaphore signal covers every
// command before it in submission order, the copy included. Pure (no Vulkan), unit tested
// (tests/vkcore/submit_order_tests.cpp).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace evr::vkcore::submit_order {

// One of the game's batches: the semaphores it waits on and signals (handles), whether its pNext chain is
// empty or a timeline semaphore submit info alone (anything else is left as it is), and whether it carries a
// command buffer whose image the appended batch copies.
struct Batch {
    std::vector<std::uint64_t> waits;
    std::vector<std::uint64_t> signals;
    bool plainChain = true;
    bool carries = false;
};

struct Move {
    bool carried = false; // a batch carries a copied command buffer
    bool movable = false; // ... and the signals from `from` on can move onto the appended batch
    std::size_t from = 0;
};

// From the first batch that carries a copied command buffer on, every batch's signals move onto the appended
// batch. Not when one of those batches has another pNext chain (its values could not be moved with them), or
// when one of them waits on a semaphore a batch from `from` on signals: it would wait for the appended batch,
// which runs after it (a deadlock).
inline Move moveSignals(const std::vector<Batch>& batches) {
    Move m;
    for (std::size_t b = 0; b < batches.size() && !m.carried; ++b) {
        if (batches[b].carries) {
            m.carried = true;
            m.from = b;
        }
    }
    if (!m.carried) {
        return m;
    }
    for (std::size_t b = m.from; b < batches.size(); ++b) {
        if (!batches[b].plainChain) {
            return m;
        }
        for (std::size_t earlier = m.from; earlier < b; ++earlier) {
            for (const std::uint64_t w : batches[b].waits) {
                for (const std::uint64_t s : batches[earlier].signals) {
                    if (w == s) {
                        return m;
                    }
                }
            }
        }
    }
    m.movable = true;
    return m;
}

} // namespace evr::vkcore::submit_order
