#pragma once

// The ring of timestamp query pairs the layer brackets submit batches with (docs/VR_STEREO.md, "GPU
// timing"). Pair p owns queries 2p (written before the batch's work) and 2p + 1 (after it).
//
// Pairs are handed out strictly in order and come back when their results have been read (or given up).
// A busy pair is never skipped: when the next pair is still waiting for its results the ring is full and
// the batch goes untimed, so a pair is never reused while the GPU may still write it.

#include <cstdint>
#include <optional>
#include <vector>

namespace evr::gpu_timing {

class QueryRing {
public:
    explicit QueryRing(std::uint32_t pairs);

    // The next pair, or nullopt when it is still in use (the ring is full).
    std::optional<std::uint32_t> acquire();
    // Gives `pair` back; releasing a pair that is not in use does nothing.
    void release(std::uint32_t pair);

    [[nodiscard]] std::uint32_t pairs() const { return static_cast<std::uint32_t>(busy_.size()); }
    [[nodiscard]] std::uint32_t inUse() const { return inUse_; }
    [[nodiscard]] std::uint32_t queryCount() const { return pairs() * 2; }

    static constexpr std::uint32_t beginQuery(std::uint32_t pair) { return pair * 2; }
    static constexpr std::uint32_t endQuery(std::uint32_t pair) { return pair * 2 + 1; }

private:
    std::vector<bool> busy_;
    std::uint32_t next_ = 0;
    std::uint32_t inUse_ = 0;
};

} // namespace evr::gpu_timing
