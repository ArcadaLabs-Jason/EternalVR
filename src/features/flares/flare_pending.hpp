#pragma once

// Lens flares under Parallel Eye Rendering (docs/VR_STEREO.md, Parallel Eye Rendering, "Lens flares per
// view"): the flare occlusion pool's pending bits, which each view's copy scans and, while the queries are
// split by view, waits for (src/vkcore/pe_flares.cpp).
//
// A copy that waits on a query nobody issued since the pool's reset stalls the queue until Windows resets the
// GPU (issue #18's class). A bit can outlive its frame: a view's copy that did not run after its setup, a
// frame whose copy loop was skipped, or the copy loop's plain read-modify-write putting back a bit the other
// view's copy cleared (more than 256 flares). So the bits are cleared at the frame's start (view 0's latch),
// and a copy waits only when every bit it scans is one its own view's surface setup marked since then and
// since that view's last copy (a copy uses up its view's marks, so a frame whose view 0 latch did not run
// cannot wait on the frame before's, unless that frame's copy loop was skipped: `jle` at 0x1C32F67, before
// the hook).
//
// A mark is not an issue, but the issue cannot decide the wait: each view's copy (category 10) is recorded
// before its occlusion pass (category 7) on the processor in all but 1 of about 14,000 copies (rig run
// pf4-a-pon), though the pass runs first on the GPU. So the queries each pass begins are noted, and at the
// next frame's start every query a copy waited on is checked against its view's issues of that frame.
//
// No engine, Windows or Vulkan here: the bit words are plain memory.

#include "features/flares/flare_draws.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <span>

namespace evr::flares {

// The words found set by a clear, and the first of them.
struct PendingCleared {
    std::int32_t words = 0;       // words that held a set bit
    std::int32_t firstWord = -1;  // the first such word's index
    std::uint32_t firstValue = 0; // its bits
};

// Zeroes every word of `words` with an atomic exchange (each view's setup sets bits with plain
// read-modify-writes on other threads); what was left set before.
PendingCleared clearPending(std::span<std::uint32_t> words);

// Queries per view since the frame's start: its surface setup's marks, its occlusion pass's issues, or the
// bits its copies waited on (one ViewMarks each). mark() and note() run on the views' threads, reset() at
// view 0's latch, covers() and clear() at a view's copy.
class ViewMarks {
public:
    void reset();
    // View `view`'s queries used up (its copy decided); another view's stay.
    void clear(int view);
    // View `view`'s queries in pending word `index` (0 outside the pool or for another view than 0, 1).
    [[nodiscard]] std::uint32_t noted(int view, std::int64_t index) const;
    // `query` noted for view `view` (another view, or a query outside the pool: ignored).
    void mark(int view, std::int64_t query);
    // Every bit of `words` (the pool's words from `firstWord` on) noted for view `view`.
    void note(int view, std::span<const std::uint32_t> words, std::int32_t firstWord);
    // Every bit set in `pending` (the pool's words from `firstWord` on) is one noted for view `view` since
    // the last reset. False for another view than 0 or 1, or words outside the pool.
    [[nodiscard]] bool covers(int view, std::span<const std::uint32_t> pending, std::int32_t firstWord) const;
    // The first word of `pending` with a bit not noted for view `view`: its index (or -1) and those bits.
    [[nodiscard]] std::int32_t firstUnmarked(int view,
                                             std::span<const std::uint32_t> pending,
                                             std::int32_t firstWord,
                                             std::uint32_t& bits) const;

private:
    std::array<std::array<std::atomic<std::uint32_t>, kPendingWords>, 2> marks_{};
};

// The first pool word in which view `view`'s copies waited on a query its occlusion pass did not issue (or
// -1), and those bits.
std::int32_t
firstWaitedUnissued(const ViewMarks& waited, const ViewMarks& issued, int view, std::uint32_t& bits);

// Rig test (ETERNALVR_TEST_PE_FLARES_STALE=1): in every kStaleTestPeriod-th frame split by view, after the
// clear, the last query of the pool (view 1's twin of slot 0x1FF, which only a frame with 256 flares takes)
// is set pending with no mark and no issue, so view 1's copy must fall back to no wait. True when it was set.
inline constexpr std::int64_t kStaleTestQuery = kPoolQueries - 1;
inline constexpr std::uint64_t kStaleTestPeriod = 64;
bool setStaleTestBit(std::span<std::uint32_t> words, std::uint64_t splitFrame);

// Rig test (ETERNALVR_TEST_PE_FLARES_STALE=2): view 1's kTripTestCopy-th copy that waits notes
// kStaleTestQuery as waited on, though the copy does not wait on it and no pass begins it, so view 0's next
// latch finds a waited query nobody issued and the copies stop waiting. True when it was noted
// (`viewOneWaits` counts view 1's copies that waited, this one included).
inline constexpr std::uint64_t kTripTestCopy = 1000;
bool noteTripTest(ViewMarks& waited, int view, std::uint64_t viewOneWaits);

} // namespace evr::flares
