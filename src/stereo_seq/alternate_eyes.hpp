#pragma once

// Alternate-eye rendering (ETERNALVR_ALTERNATE_EYES=1, off by default; docs/rig-findings/alternate-eye.md).
// With =auto (adaptive_eyes.hpp) a pair may also render both eyes in its tick, as Route S does.
//
// Route S renders eye L and then eye R in every game tick. With alternate eyes each game tick renders one
// eye only, in the engine's own chain: eye L, then eye R in the next tick, and so on. The renderer sees the
// same order of renders as under Route S (L, R, L, R), with a game tick before every render instead of every
// second one, so the per-eye history that follows the render order (the previous-frame matrices, the TAA and
// scattering images picked by the eye tag, the exposure pair) stays per eye unchanged. What changes is here:
//
// - EyeAlternator: which eye a render of the engine's own chain draws;
// - AlternatePairing: the present hook shows every fresh eye image together with the other eye's newest
//   image (held from the tick before), each with the pose it was rendered with;
// - composeHalves: the view record of such a pair (the fresh tick's record, the held eye taken from its own);
// - AlternateTaaReset and alternateSubSample: the TAA reset and jitter phase when each eye renders every
//   other tick.
//
// No engine, Vulkan or OpenXR here, so it is tested on every platform.

#include "stereo_seq/eye_tags.hpp"

#include <cstddef>
#include <cstdint>

namespace evr::stereo_seq {

// The eye each render of the engine's own chain draws. Asked several times per render (the previous-matrix
// store for each view, the per-eye hook, the post-latch hook, the frame end), always with that render's
// frame counter (renderSystem + 0x10, raised before the world-views pass): the first ask decides, later asks
// get the same answer.
class EyeAlternator {
public:
    // Right when the render frame right before this one was drawn as eye L in stereo, else Left. So eye R
    // only ever follows its eye L (as under Route S, which the per-eye previous matrices rely on), and any
    // mono render (a menu, a loading screen, a tick that could not be stereo) starts again with eye L.
    Eye eyeFor(std::uint32_t renderFrame);

    // The frame end of `renderFrame`: `stereo` when it was handed over as `eye` with that eye's view written.
    // `pairDone`: eye L's frame end of a tick that renders eye R nested as well (Route S under
    // ETERNALVR_ALTERNATE_EYES=auto), so the next render of the engine's chain starts a new pair with eye L.
    void rendered(std::uint32_t renderFrame, Eye eye, bool stereo, bool pairDone = false);

    struct Stats {
        std::uint64_t renders[2] = {}; // stereo renders per eye index
        std::uint64_t mono = 0;        // frame ends not drawn as an eye
    };
    const Stats& stats() const { return stats_; }

private:
    bool decided_ = false;
    std::uint32_t decidedFrame_ = 0;
    Eye decidedEye_ = Eye::Left;
    bool lastStereo_ = false;   // the last frame end was a stereo render
    bool lastPairDone_ = false; // and its tick rendered both eyes
    std::uint32_t lastFrame_ = 0;
    Eye lastEye_ = Eye::Left;
    Stats stats_;
};

enum class AltAction : std::uint8_t {
    ShowMono, // the same image in both halves, with the head pose (menus, loading screens, mono ticks)
    Hold,     // copy the image into its half of a free slot and hold the slot: no partner yet
    Publish,  // copy the image into its half of the held slot (the other half holds the other eye's newest
              // image) and show it; the image also goes into its half of a new held slot for the next present
    Drop,     // not shown (the per-eye hook did not write the eye's view)
};

const char* altActionName(AltAction action);

struct AltStep {
    AltAction action = AltAction::Drop;
    bool abandoned = false; // the held image (and its slot) was given up
    int freshIndex = 0;     // Hold, Publish: the half this present's image goes to (eyeIndex)
    std::uint64_t freshTick = 0;
    std::uint64_t heldTick = 0; // Publish: the game frame of the other half's image
};

// Pairs each fresh eye with the other eye's newest image. The held image must be of the other eye and at most
// `maxHeldAge` game frames older than the fresh one (1 in steady play); otherwise it is given up and the
// fresh image is held instead (a single eye's image is never shown to both eyes, and two images far apart in
// time would not fuse).
//
// With ETERNALVR_ALTERNATE_EYES=auto a tick may render both eyes (Route S; its tags carry pairInTick): its
// eye L is held, not shown beside the other eye's older image, and its eye R is shown with that eye L (the
// same game frame, age 0), as Route S shows a pair. Without such tags nothing changes.
class AlternatePairing {
public:
    explicit AlternatePairing(std::uint64_t maxHeldAge = 2) : maxHeldAge_(maxHeldAge) {}

    AltStep onPresent(const PresentMatch& present);

    // Hold or Publish: the image could not be stored for the next present (no free slot, or the copy failed):
    // nothing is held, the next present starts again.
    void carryNotStored();
    // Publish: the copy into the held slot failed; the pair is not shown and nothing is held.
    void publishNotStored();

    bool holding() const { return holding_; }
    Eye heldEye() const { return heldEye_; }
    std::uint64_t heldTick() const { return heldTick_; }

    struct Stats {
        std::uint64_t presents = 0;
        std::uint64_t mono = 0;
        std::uint64_t held = 0;        // Hold: an image stored without a partner
        std::uint64_t published = 0;   // fresh + held images shown
        std::uint64_t withoutView = 0; // eye presents whose view was not written (dropped)
        std::uint64_t abandoned = 0;   // held images given up (a mono present, the same eye again, too old)
        std::uint64_t notStored = 0;   // images that could not be copied into the ring
        std::uint64_t heldAgeSum = 0; // game frames between the held and the fresh image, summed over Publish
        std::uint64_t heldAgeMax = 0;
        std::uint64_t sameTick = 0;   // of those shown, both eyes of one tick (pairInTick)
        std::uint64_t pairStarts = 0; // eye L images of such ticks, held for their own eye R
    };
    const Stats& stats() const { return stats_; }

private:
    bool abandon();

    std::uint64_t maxHeldAge_;
    bool holding_ = false;
    Eye heldEye_ = Eye::Left;
    std::uint64_t heldTick_ = 0;
    Stats stats_;
};

// The view record a published pair is shown with: the fresh tick's record, with the held half's eye (pose,
// FOV, offset, axis) taken from the record of the tick it was rendered in. Each eye is then submitted with
// the pose it was rendered with, and the compositor's reprojection corrects the held eye for the head's
// motion since. `Record` has an `eyes` array indexed by eyeIndex.
template <class Record>
Record composeHalves(const Record& fresh, const Record& held, int heldIndex) {
    Record out = fresh;
    const auto i = static_cast<std::size_t>(heldIndex == 1 ? 1 : 0);
    out.eyes[i] = held.eyes[i];
    return out;
}

// The TAA reset with alternate eyes (renderView_t.disableTssaaNextFewFrames; the engine then ignores the
// history for its next three renders, which covers both eyes). An eye's history is valid when the renders
// alternated without a break since its last render: the render before was the other eye of the game frame
// before, and the one before that this eye. Called by the per-eye hook for each stereo render; true: set the
// flag.
class AlternateTaaReset {
public:
    // `sameTickPairs` (ETERNALVR_ALTERNATE_EYES=auto): eye R of eye L's own game frame (a Route S tick)
    // continues the run as well.
    explicit AlternateTaaReset(bool sameTickPairs = false) : sameTickPairs_(sameTickPairs) {}

    bool onEye(Eye eye, std::uint64_t gameFrame);

private:
    bool sameTickPairs_;
    std::uint64_t lastFrame_ = 0; // 0: none yet
    Eye lastEye_ = Eye::Mono;
    int run_ = 0; // renders in the current unbroken alternation
};

// The TAA jitter phase of game frame `gameFrame` when each eye renders every other game frame: consecutive
// renders of one eye get consecutive phases (half the game frame), so each eye goes through every phase.
std::uint8_t alternateSubSample(std::uint64_t gameFrame, int numSubSamples);

// Game frames between two renders of the same eye in steady play: 2 with alternate eyes, 1 under Route S (eye
// R's DLSS twin resets its history when the gap differs).
constexpr std::uint64_t eyeFrameStep(bool alternate) {
    return alternate ? 2 : 1;
}

// The shortest such gap: with ETERNALVR_ALTERNATE_EYES=auto a tick may render both eyes, so an eye's last
// render is one or two game frames back.
constexpr std::uint64_t eyeFrameMinStep(bool alternate, bool adaptive) {
    return alternate && !adaptive ? 2 : 1;
}

} // namespace evr::stereo_seq
