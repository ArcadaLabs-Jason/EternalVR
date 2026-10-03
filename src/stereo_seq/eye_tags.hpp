#pragma once

// Eye tags for synchronized sequential stereo (Route S, docs/VR_STEREO.md).
//
// Each engine render frame ends in one present on the render thread. The frame-end job wrapper knows which
// eye a render frame draws (eye L runs in the engine's own chain, eye R in the chain the wrapper starts)
// and queues a tag for it before the frame is handed to the render thread; the present hook takes one tag
// per present. The render thread presents frames in order, so a FIFO would do, except that a frame can
// fail to reach a present (a skipped backend frame, a present the swap code did not issue) and frames
// already in flight when the tags start have none.
//
// So every tag also carries the value the engine's backend frame counter (renderBackend + 0xB0, raised
// by the render thread just before each swap) will have at that frame's present. The value is assigned
// on push from a base taken while the render thread was idle (`rebase`); the present hook passes the
// counter it reads, and any disagreement (a tag whose present never came, a backend frame nobody tagged)
// drops the queue out of sync. Out of sync, no tags are queued and every present is untagged (shown
// mono) until the wrapper rebases again.

#include <cstddef>
#include <cstdint>
#include <deque>

namespace evr::stereo_seq {

enum class Eye : std::uint8_t { Mono = 0, Left = 1, Right = 2 };

const char* eyeName(Eye eye);

// Index of an eye in per-eye arrays: 0 for Left (and Mono), 1 for Right.
constexpr int eyeIndex(Eye eye) {
    return eye == Eye::Right ? 1 : 0;
}

struct RenderTag {
    Eye eye = Eye::Mono;
    std::uint64_t tick = 0;         // game frame whose view both eyes were made from (0: unknown)
    bool viewApplied = false;       // the per-eye hook wrote this eye's view into the render view
    bool pairInTick = false;        // ETERNALVR_ALTERNATE_EYES=auto: both eyes of this tick render (Route S)
    std::uint32_t renderFrame = 0;  // renderSystem + 0x10 when the frame's chain ended (diagnostics)
    std::uint32_t backendFrame = 0; // assigned by push: the backend counter expected at its present
    std::uint32_t eyeSeq = 0;       // assigned by push: the count of frames pushed before for this eye
                                    // (eye L and mono count together; never reset)
};

// Whether the tagged frame drew one eye's own view: an eye L or R frame the per-eye hook wrote the view into.
// A mono frame (eyeIndex 0) and an eye frame without its view drew the game's own view, not eye L's.
constexpr bool drawsEyeView(const RenderTag& tag) {
    return tag.eye != Eye::Mono && tag.viewApplied;
}

struct PresentMatch {
    bool tagged = false; // false: no tag belongs to this present (show it mono)
    RenderTag tag;
};

enum class DesyncReason : std::uint8_t {
    None = 0,
    MissingPresent, // a tagged frame's present never came (its backend value was passed)
    UntaggedFrame,  // a present with a backend value no tag was waiting for
    Overflow,       // more tags queued than frames can be in flight
    Requested,      // the wrapper asked for a fresh base (stereo resuming after mono frames)
};

const char* desyncReasonName(DesyncReason reason);

class EyeTagQueue {
public:
    explicit EyeTagQueue(std::size_t capacity = 8) : capacity_(capacity) {}

    bool synced() const { return synced_; }

    // The render thread is idle and its backend counter is `idleBackendFrame`: every frame kicked before
    // has been presented, so the next tagged frame presents at idleBackendFrame + 1.
    void rebase(std::uint32_t idleBackendFrame);

    // Drops the queue out of sync (counted under `reason`).
    void desync(DesyncReason reason);

    // Queues the tag of a frame about to be handed to the render thread. False (and nothing queued) while
    // out of sync.
    bool push(RenderTag tag);

    // The tag of the present the render thread issues with backend counter `backendFrame`.
    PresentMatch pop(std::uint32_t backendFrame);

    // The queued tag whose present will carry backend counter `backendFrame`, without taking it: the
    // render jobs of a backend frame run before its present (they see the counter one below), so this is
    // how they learn their frame's eye. nullptr when no queued tag has that value.
    const RenderTag* peek(std::uint32_t backendFrame) const;

    std::size_t size() const { return queue_.size(); }

    struct Stats {
        std::uint64_t rebases = 0;
        std::uint64_t pushed = 0;
        std::uint64_t matched = 0;
        std::uint64_t untagged = 0;       // presents with no tag (out of sync, or frames before the base)
        std::uint64_t missingPresent = 0; // desyncs by reason
        std::uint64_t untaggedFrame = 0;
        std::uint64_t overflow = 0;
        std::uint64_t requested = 0;
    };
    const Stats& stats() const { return stats_; }
    DesyncReason lastDesync() const { return lastDesync_; }

private:
    std::size_t capacity_;
    std::deque<RenderTag> queue_;
    bool synced_ = false;
    std::uint32_t base_ = 0;        // backend value of the idle point
    std::uint32_t nextBackend_ = 0; // value the next pushed tag gets
    std::uint32_t lastPresent_ = 0; // backend value of the last present seen while in sync
    std::uint32_t eyeSeq_[2] = {};  // frames pushed per eye index (eyeIndex)
    Stats stats_;
    DesyncReason lastDesync_ = DesyncReason::None;
};

} // namespace evr::stereo_seq
