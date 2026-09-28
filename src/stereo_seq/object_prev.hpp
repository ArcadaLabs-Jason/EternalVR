#pragma once

// The object-transform ring per game tick (docs/rig-findings/stereo-object-motion.md).
//
// The engine keeps each render's object transforms (skinning joint offsets and model matrices) in a ring of
// three buffers picked by the render counter: a render uploads ring[counter % 3], binds it as the current
// frame's and binds ring[(counter + 2) % 3], the render just before, as the previous frame's. The upload is
// not ordered after the GPU work of earlier renders: in mono a slot is written again two renders after the
// last render that read it, which the engine's frames in flight allow.
//
// Under Route S a tick is two renders of one game frame, eye L then eye R. Eye R's previous frame must be the
// tick before, not eye L's render of the same frame (that gives moving objects no motion in eye R). Reading
// eye R's own render of the tick before (two back) left one render between that read and the slot's next
// upload: with the headset's frames in flight the upload often came first, eye R read the NEXT tick's
// transforms and its motion vectors pointed backwards (the owner's Quest 3, same-view captures, 2026-09-28).
//
// Sharing a slot within the tick does not work either: eye R's upload lands while eye L's GPU work still
// reads the slot, and the two renders' transforms are not identical (the world strobed in the headset).
//
// So the ring's slots are handed out here, for every render (mono ones too, so the engine's own picks never
// mix in): the previous frame is the slot of the right earlier render (eye R: the tick before's eye R, else
// the tick before's latest render; eye L: the tick before's latest render; mono: the render just before),
// and the render writes the least recently used other slot. The previous frame is always a slot the render
// just before wrote or read (otherwise that render's previous frame), so the slot written is never one that
// render uses, whatever renders come between ticks. In steady stereo eye L keeps one slot and eye R
// alternates between the other two; every slot is written again at least two renders after its last read,
// as in mono, and no slot is written while a render of the same tick reads it.

#include "stereo_seq/eye_tags.hpp"

#include <cstdint>

namespace evr::stereo_seq {

class ObjectRing {
public:
    // The counters to hand the engine's picks for a render: `current` for the upload and the current
    // frame's buffers (the engine uses counter % 3), `previous` for the previous frame's buffers (the engine
    // adds 2). `remapped` is false when both are the render's own counter. A render is asked once per pick;
    // the answer is the same each time. A render without a tag is asked as Eye::Mono with tick 0. Renders
    // must be first asked in counter order; `inOrder`, when given, is set false when this one was not (a
    // counter other than the last first-asked one + 1).
    struct Picks {
        std::uint32_t current = 0;
        std::uint32_t previous = 0;
        bool remapped = false;
    };
    Picks picksFor(Eye eye, std::uint64_t tick, std::uint32_t counter, bool* inOrder = nullptr);

private:
    struct Answer {
        bool valid = false;
        std::uint32_t counter = 0;
        Picks picks;
    };
    struct Slot {
        bool written = false;
        Eye eye = Eye::Mono;
        std::uint64_t tick = 0;
        std::uint32_t writer = 0; // the counter of the render that wrote it
        bool used = false;
        std::uint32_t lastUse = 0; // the counter of the latest render that wrote or read it
    };
    Picks decide(Eye eye, std::uint64_t tick, std::uint32_t counter);
    int previousSlot(Eye eye, std::uint64_t tick) const;

    // The latest answers, so a render asked again (a later pick, its upload) gets the same one. Eight: a
    // render whose answer was dropped would be decided again as a new render (the out-of-order count shows
    // it).
    static constexpr int kAnswers = 8;
    Answer answers_[kAnswers];
    int nextAnswer_ = 0;
    bool asked_ = false;
    std::uint32_t lastAsked_ = 0; // the counter of the latest render first asked

    Slot slots_[3];
    int lastSlot_ = -1; // the slot the latest render wrote
    int lastRead_ = -1; // the slot the latest render read as its previous frame
};

} // namespace evr::stereo_seq
