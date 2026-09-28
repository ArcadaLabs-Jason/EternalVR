#pragma once

// The previous frame of animated and moving objects per eye (docs/rig-findings/stereo-object-motion.md).
//
// The engine keeps each render's object transforms (skinning joint offsets and model matrices) in a ring of
// three buffers, picked by the render counter: a render writes ring[counter % 3] and reads the previous
// frame's from ring[(counter + 2) % 3], the render just before. Under Route S the two renders of a tick are
// eye L then eye R, both for the same game frame: eye L's previous is eye R's render of the tick before (the
// right one), but eye R's is eye L's render of the same frame, so every moving object has zero motion in eye
// R and its TAA history smears the object. Eye R's own render of the tick before is two renders back and
// still in the ring (it is written over only by the render after eye R's), so eye R reads that one.

#include "stereo_seq/eye_tags.hpp"

#include <cstdint>

namespace evr::stereo_seq {

class ObjectPrevSlot {
public:
    // The counter to give the engine's pick of the previous frame's buffer for a render of `eye` with the
    // render counter `counter`: `counter + 2` (so it picks two renders back) for eye R when its last render
    // was two renders ago, else `counter`. The same render can ask more than once (one per buffer).
    std::uint32_t counterFor(Eye eye, std::uint32_t counter);

private:
    bool haveRight_ = false;
    std::uint32_t right_ = 0; // eye R's latest render counter
    bool twoBack_ = false;    // that render's answer
};

} // namespace evr::stereo_seq
