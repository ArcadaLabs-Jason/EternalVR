#pragma once

// Moving entities' previous model matrix with alternate eyes (ETERNALVR_ALTERNATE_EYES=1 or auto;
// docs/rig-findings/alternate-eye.md section 3).
//
// The world's commit of an entity (0x1C8ADE0) copies its current model matrix over its previous one and
// then computes the new current one. Ordinary entities' motion vectors come from these two matrices (the
// render gathers both per visible entity, stereo-moved-flag.md section 5). Each render commits the entities
// of its own game tick, so the previous matrix is the one of the render just before. With alternate eyes that
// render is the other eye's, one tick back, while the eye's previous camera matrices and its TAA history are
// its own last render, two renders back: a moving entity's motion vector then covers half the time its
// history is behind, and TAA smears it.
//
// The render order is always L, R, L, R (Route S and alternation alike), so each eye's last render is the
// one two renders back. The previous matrix a commit in render n should leave is the entity's current
// matrix as it was at the end of render n - 2. The engine's copy gives the current matrix now, which is that
// one unless the entity was committed in render n - 1; so each commit keeps the current matrix it replaces
// (the matrix of the end of the render before), and a commit one render later puts that back as the
// previous one. A second commit of an entity in one render keeps the previous matrix the first one left.
//
// Under Route S's nested eye R this is what keep_prev.hpp does (eye R keeps eye L's previous matrix for an
// entity eye L committed in its tick); with alternate eyes on or auto this rule replaces it for every render.
//
// No engine here, so it is tested on every platform: the matrix storage is the caller's.

#include <cstdint>

namespace evr::stereo_seq {

// What the hook keeps per entity: the render of its latest commit (and, in the caller's storage, the current
// matrix that commit replaced).
struct PrevHold {
    bool valid = false;
    std::uint64_t render = 0;
};

enum class PrevAction : std::uint8_t {
    Engine,  // leave the engine's copy (previous = current now)
    Restore, // put the held matrix back as the previous one after the commit
    Keep,    // put back the previous matrix the entity has now (a second commit in the same render)
};

struct PrevPlan {
    PrevAction action = PrevAction::Engine;
    bool hold = false; // the hold takes this commit: its render and the current matrix it replaces
};

// A commit in render `render` (renders numbered one apart, in the order they run) of an entity holding
// `held`.
PrevPlan planPrevious(const PrevHold& held, std::uint64_t render);

} // namespace evr::stereo_seq
