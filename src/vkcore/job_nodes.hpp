#pragma once

// The engine's job graph nodes as Parallel Eye Rendering reads them (build 25216728), and the bounded wait
// its binning edges use. Pure logic, unit-tested (tests/vkcore/job_nodes_tests.cpp).
//
// The scheduler ([base + 0x5BF1270], vtable 0x2E60F00) keeps its nodes in an array at [scheduler + 0xC0690],
// 0x80 bytes each; a node's first qword is its state: bits 2-13 the dependency count (4 per predecessor),
// bits 14-25 the references (0x4000 each: a running job, an edge being added), bits 26-63 the generation. A
// node handle carries the node index in bits 0-17 and the generation it was made with in bits 26-63.
// AddEdge (vtable +0x68, 0x18713D0) compares the generations and returns false for a node that has finished
// (its generation moved on); otherwise it holds a reference on the predecessor while it appends the edge, and
// the last reference dropped (0x1870780) runs the predecessor's completion, so an edge added while the
// predecessor finishes still fires: the engine's AddEdge has no lost wakeup.

#include <cstdint>

namespace evr::vkcore::job_nodes {

inline constexpr std::uint64_t kGenerationMask = 0xFFFFFFFFFC000000ull;
inline constexpr std::uint64_t kIndexMask = 0x3FFFF;
inline constexpr std::uint64_t kNodeStride = 0x80;
inline constexpr std::uint32_t kNodesField = 0xC0690; // in the scheduler: the node array

// The node's state qword, from the node array (the engine reads [array + index * 0x80 - 0x80]).
inline constexpr std::int64_t stateOffset(std::uint64_t handle) {
    return static_cast<std::int64_t>((handle & kIndexMask) * kNodeStride) -
           static_cast<std::int64_t>(kNodeStride);
}

// True once the node the handle names has finished: its generation is no longer the handle's.
inline constexpr bool finished(std::uint64_t handle, std::uint64_t state) {
    return (state & kGenerationMask) != (handle & kGenerationMask);
}

// A wait for another thread's mark, at most `limit` ticks, given up for the session after `maxStreak` waits
// in a row that ran out: then the caller stops waiting (a frame shape in which the mark never comes would
// otherwise cost the limit every frame).
class BoundedWait {
public:
    BoundedWait(std::int64_t limit, int maxStreak) : limit_(limit), maxStreak_(maxStreak) {}

    enum class Result { Ready, Waited, TimedOut, Off };

    // `ready()` says whether the mark is there; `now()` reads the clock (ticks). Spins on `pause()`.
    template <typename Ready, typename Now, typename Pause>
    Result wait(Ready ready, Now now, Pause pause) {
        if (ready()) {
            if (!off()) {
                streak_ = 0; // a mark there at once breaks a run of waits that ran out too
            }
            return Result::Ready;
        }
        if (off()) {
            return Result::Off;
        }
        const std::int64_t start = now();
        while (now() - start < limit_) {
            pause();
            if (ready()) {
                streak_ = 0;
                return Result::Waited;
            }
        }
        if (ready()) {
            streak_ = 0;
            return Result::Waited;
        }
        ++streak_;
        return Result::TimedOut;
    }

    bool off() const { return streak_ >= maxStreak_; }

private:
    std::int64_t limit_;
    int maxStreak_;
    int streak_ = 0;
};

} // namespace evr::vkcore::job_nodes
