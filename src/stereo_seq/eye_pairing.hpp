#pragma once

// Pairing the two presents of a stereo tick into one ring record (Route S, docs/VR_STEREO.md).
//
// A stereo tick presents eye L, then eye R; menus and loading screens present mono frames. The present
// hook asks the pairing what to do with each present:
//
// - ShowMono: copy the image into both halves of a free slot and show it with the head pose.
// - StartPair: copy the image into the left half of a free slot and hold the slot for eye R.
// - CompletePair: copy the image into the right half of the held slot and show the pair.
// - Drop: the image is not shown (a half whose partner is missing).
//
// `abandoned` says that a held left half was given up (eye R never came): its slot goes back to the
// free list. Halves are dropped rather than shown alone: a single eye's image shown to both eyes, or two
// halves from different ticks, is worse than repeating the last good pair.

#include "stereo_seq/eye_tags.hpp"

#include <cstdint>

namespace evr::stereo_seq {

enum class PairAction : std::uint8_t { ShowMono, StartPair, CompletePair, Drop };

const char* pairActionName(PairAction action);

struct PairStep {
    PairAction action = PairAction::Drop;
    bool abandoned = false;
};

class EyePairing {
public:
    PairStep onPresent(const PresentMatch& present);

    // The left half of the pending pair could not be stored (no free slot, or the copy failed).
    void leftNotStored();
    // The right half could not be stored into the held slot; the pair is given up.
    void rightNotStored();

    bool pending() const { return pending_; }
    std::uint64_t pendingTick() const { return pendingTick_; }

    struct Stats {
        std::uint64_t presents = 0;
        std::uint64_t mono = 0;
        std::uint64_t pairsStarted = 0;
        std::uint64_t pairsCompleted = 0;
        std::uint64_t leftDropped = 0;  // left halves whose right half never came (or was not stored)
        std::uint64_t rightDropped = 0; // right halves without their left half
        std::uint64_t withoutView = 0;  // halves the per-eye hook did not write a view for
        std::uint64_t notStored = 0;    // halves that could not be copied into the ring
    };
    const Stats& stats() const { return stats_; }

private:
    bool abandonPending();

    bool pending_ = false;
    std::uint64_t pendingTick_ = 0;
    Stats stats_;
};

} // namespace evr::stereo_seq
