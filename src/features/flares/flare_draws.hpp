#pragma once

// Lens flares under Parallel Eye Rendering (docs/VR_STEREO.md, Parallel Eye Rendering, "Lens flares"): what
// view 1 draws and tests of each flare (src/vkcore/pe_flares.cpp).
//
// Both views draw a flare from one geometry record per element (its index buffer offset at +0x6C), and the
// prepare took room for two halves of quads: view 1's binds of a flare's record get the offset of its half.
// Each flare's two occlusion queries are view 0's in the pool's lower half and view 1's in the upper half,
// so each view issues, copies and reads its own, as one view does.
//
// No engine, Windows or Vulkan here: geometry records are addresses.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace evr::flares {

// A quad's 6 indices of 4 bytes in the transparency quads' index buffer (quad q uses vertices 4q to 4q+3):
// half 1 of a flare of n quads is n * 24 bytes further on.
inline constexpr std::int32_t kIndexBytesPerQuad = 24;
std::int32_t halfIndexShift(std::int32_t quads);

// The flare occlusion pool: 0x400 queries, reset whole every frame, the results 8 bytes each; the pending
// bits one per query.
inline constexpr std::int64_t kPoolQueries = 0x400;
inline constexpr std::int64_t kViewOneQueries = kPoolQueries / 2; // view 1's half starts here
inline constexpr std::int32_t kPendingWords = static_cast<std::int32_t>(kPoolQueries / 32);

// The query `view` issues and reads for the engine's slot: view 1 the slot's twin in the upper half. A slot
// in the upper half already (more than 256 flares in a frame) stays as it is in both views.
std::int64_t viewQuery(std::int64_t slot, int view);

// A frame's queries are split by view while the engine took no more than the lower half.
bool queriesSplit(std::int64_t taken);

// One view's copy of its pending queries (the engine's copy scans the bit words from 0 to the counter's):
// the words it scans, the first word's slot, and whether it may wait for the results. View 0 keeps the
// engine's scan, view 1 scans the upper half. `wait` says the split holds, so each view's scan holds only the
// queries its own surface setup marks and its own occlusion pass issues before the copy; the copy waits only
// if every bit it scans was marked by its own view this frame too (flare_pending.hpp).
struct CopyScan {
    std::int32_t firstWord = 0;
    std::int32_t words = 0;
    std::int64_t firstSlot = 0;
    bool wait = false;
};
CopyScan copyScan(int view, std::int32_t engineWords, std::int64_t taken);

// Which geometry records view 1 draws shifted to its half, kept for one frame. One writer (the latches);
// readers on the draw threads (find). A record whose offset is not the one written is someone else's or
// another frame's: not shifted.
class DrawShifts {
public:
    static constexpr std::size_t kSlots = 4096;

    // Drops the last frame's records (the writer, at view 0's latch, before the frame's draws).
    void reset();
    // A record drawn at `offset` this frame, and the shift view 1 adds to it. False: no room (not shifted).
    bool put(std::uintptr_t geometry, std::int32_t offset, std::int32_t shift);
    // The shift for `geometry` bound at `offset`; nullopt when it is not a flare record of this frame.
    [[nodiscard]] std::optional<std::int32_t> find(std::uintptr_t geometry, std::int32_t offset) const;
    [[nodiscard]] bool empty() const { return used_.load(std::memory_order_relaxed) == 0; }

private:
    static std::size_t home(std::uintptr_t geometry);

    std::array<std::atomic<std::uintptr_t>, kSlots> keys_{};
    std::array<std::atomic<std::uint64_t>, kSlots> values_{}; // offset (low 32 bits), shift (high 32)
    std::array<std::uint16_t, kSlots> taken_{};               // the writer's list of slots in use
    std::atomic<std::size_t> used_{0};
};

// The draw watch: view 1's halves are rebuilt and the views' surface setups set flares up to be drawn, but no
// bind of a flare record shows at the hooked binds in a whole period: the flares are drawn elsewhere (view 1
// would draw view 0's half), so they go off. A flare the update keeps building but neither view sets up (out
// of sight for the period) is not drawn at all: no trip (rig run pf3-f-wdef tripped there before).
inline constexpr std::uint64_t kUnseenRebuilds = 600;
struct DrawCounts {
    std::uint64_t rebuiltViewOne = 0; // flares whose view 1 half was rebuilt
    std::uint64_t binds = 0;          // binds of a registered flare record, either view
    std::uint64_t setups = 0;         // flares the views' surface setups marked for their occlusion queries
};
bool drawsUnseen(const DrawCounts& counts);

} // namespace evr::flares
