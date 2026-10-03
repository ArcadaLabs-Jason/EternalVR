#pragma once

// Per-eye previous-frame matrices for synchronized sequential stereo (docs/VR_STEREO.md,
// docs/rig-findings/stereo-routes.md section 2.4).
//
// At the start of every render frame the engine copies each idRenderView's current matrices (what the
// last latch built) into its "previous frame" fields (RVA 0x1CE2340, called once per world view in the
// world-views pass, before this frame's latch). With both eyes rendered each tick, the last latch before
// eye L is eye R's of the previous tick and the last before eye R is eye L's, so each eye would get the
// other eye's matrices as its previous ones (wrong motion vectors).
//
// What the engine stores at eye R's frame is exactly what eye L needs next tick (the state after eye L's
// latch), and what it stores at eye L's frame after a stereo tick is what eye R needs this tick. So after
// each store (hook at RVA 0x1C75D81) the book keeps the stored bytes for the other eye and writes the
// bytes kept for this eye. It needs only the ranges the store writes, not which field each comes from.
//
// Eye L's frame cannot know yet whether its tick will be a stereo one (the per-eye hook runs later in
// the frame); it swaps only when the render frame right before it was this view's eye R frame, which is
// right for every tick after the first stereo tick. The first eye R after mono frames keeps what the
// engine stored (eye L's matrices, one frame of wrong motion vectors). Kept bytes are only used by the
// render frame right after the one that kept them (the render frame counter, renderSystem + 0x10): any
// frame in between (a mono frame, a loading screen, a map change that gives a new view the address of an
// old one) makes them stale.
//
// With alternate eyes an eye R render frame can turn mono after the store (the tags take a new base, or no
// view is located): the per-eye hook then writes no view, and the frame renders the game's view with eye
// L's accumulation. undoRewrite puts back what the engine stored (eye L's matrices) for that frame.

#include "stereo_seq/eye_tags.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace evr::stereo_seq {

struct ByteRange {
    std::size_t offset = 0;
    std::size_t size = 0;
};

// The ranges RVA 0x1CE2340 writes in an idRenderView of build 25216728, in ascending order (the
// r_lockView 0 branch included; the camera-cut counter at +0x29944 is not a matrix and is left alone).
std::vector<ByteRange> previousMatrixRanges();

class PrevMatrixBook {
public:
    explicit PrevMatrixBook(std::vector<ByteRange> ranges, std::size_t maxViews = 8);

    // The engine just stored the previous-frame matrices of `view` in render frame `renderFrame`, which
    // draws `eye` (Left for any frame of the engine's own chain, Right inside the eye R chain). Returns
    // true when it rewrote them.
    bool afterStore(std::byte* view, Eye eye, std::uint32_t renderFrame);

    // The eye R render frame whose store of `view` was just rewritten stays mono after all: puts back the
    // bytes the engine stored and counts the frame as a mono one. Returns true when it undid a rewrite.
    bool undoRewrite(std::byte* view);

    void clear();

    struct Stats {
        std::uint64_t stores = 0;
        std::uint64_t rewrites = 0; // stores replaced by this eye's own previous matrices
        std::uint64_t kept = 0;     // stores left as the engine made them (no bytes kept for this eye)
        std::uint64_t undone = 0;   // rewrites put back: the eye R frame stayed mono
    };
    const Stats& stats() const { return stats_; }

private:
    struct Entry {
        const std::byte* view = nullptr;
        Eye lastEye = Eye::Mono;
        std::array<std::vector<std::byte>, 2> saved; // [0] for eye L, [1] for eye R
        std::array<bool, 2> valid{};
        bool stored = false;         // lastFrame is set
        std::uint32_t lastFrame = 0; // render frame of the last store
        std::uint64_t lastUse = 0;
        bool undoable = false; // the last store was an eye R rewrite: saved[0] holds the engine's bytes
    };
    Entry& entryFor(const std::byte* view);
    void save(const std::byte* view, std::vector<std::byte>& into) const;
    void restore(std::byte* view, const std::vector<std::byte>& from) const;

    std::vector<ByteRange> ranges_;
    std::size_t bytes_ = 0;
    std::size_t maxViews_;
    std::vector<Entry> entries_;
    std::uint64_t useCounter_ = 0;
    Stats stats_;
};

} // namespace evr::stereo_seq
