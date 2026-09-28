#pragma once

// The Vulkan image layout of a few game images, followed through the game's own barriers
// (docs/rig-findings/ui-layer.md section 5).
//
// The game moves its images between layouts with explicit vkCmdPipelineBarrier calls only (its render
// passes keep each attachment's layout, and it never transfers queue family ownership). Copying one of
// them from our own command buffer needs the layout it is in once the game's submitted work has run: the
// newLayout of the last barrier on it, in submission order. Recording order is not enough (the game
// records command buffers on several threads), so each command buffer's last layout per image is kept
// until it is submitted, and a submit applies its command buffers in order. Secondary command buffers
// pass theirs to the primary that executes them.
//
// Only images marked as candidates are followed. Handles are the Vulkan handles as integers, so this
// module needs no Vulkan headers. Every call is thread-safe.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace evr::ui_layer {

class LayoutTracker {
public:
    using Handle = std::uint64_t;

    // Follows `image` from now on (layout unknown until a barrier on it is submitted). False when
    // kMaxCandidates images are followed already (the image is then not followed).
    static constexpr std::size_t kMaxCandidates = 64;
    bool addCandidate(Handle image);
    void removeCandidate(Handle image);
    bool isCandidate(Handle image) const;

    // A barrier recorded into `commandBuffer` moves `image` to `newLayout`.
    void onBarrier(Handle commandBuffer, Handle image, std::int32_t newLayout);
    // `commandBuffer` is being recorded again: what it held is gone.
    void onBegin(Handle commandBuffer);
    // `primary` executes `secondaries` in this order.
    void onExecute(Handle primary, const Handle* secondaries, std::size_t count);
    // `commandBuffers` were submitted in this order on `queue`.
    void onSubmit(Handle queue, const Handle* commandBuffers, std::size_t count);

    struct State {
        std::int32_t layout = 0;
        Handle queue = 0; // the queue whose submit set it
    };
    // The layout `image` is in once everything submitted so far has run; nullopt when no barrier on it
    // has been submitted since it became a candidate.
    std::optional<State> stateOf(Handle image) const;

    // Pending command buffer entries are dropped beyond this many (command buffers freed without being
    // submitted or recorded again would otherwise pile up).
    static constexpr std::size_t kMaxPending = 4096;
    std::size_t pendingCount() const;

private:
    struct PairHash {
        std::size_t operator()(const std::pair<Handle, Handle>& p) const noexcept {
            return std::hash<Handle>{}(p.first) ^ (std::hash<Handle>{}(p.second) * 0x9E3779B97F4A7C15ull);
        }
    };
    // Lock-free membership test for the barrier hook, which runs for every barrier the game records.
    bool listed(Handle image) const;

    std::array<std::atomic<Handle>, kMaxCandidates> fast_{};
    mutable std::mutex mutex_;
    std::unordered_set<Handle> candidates_;
    // (command buffer, image) -> the last layout recorded in that command buffer.
    std::unordered_map<std::pair<Handle, Handle>, std::int32_t, PairHash> pending_;
    // command buffer -> images it holds a pending layout for (for begin, execute and submit).
    std::unordered_map<Handle, std::unordered_set<Handle>> byCommandBuffer_;
    std::unordered_map<Handle, State> current_;
};

} // namespace evr::ui_layer
