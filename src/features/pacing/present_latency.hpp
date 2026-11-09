#pragma once

// Release-to-present timing (docs/VR_STEREO.md "Release-to-present timing"): per image the game hands to the
// XR worker, where the time goes from the moment the game's render thread went on after the previous image
// to the xrEndFrame that submitted it to the runtime.
//
// The game side, on the render thread: the release (the present hook of the previous hand-over returned,
// after the hold's wait for a headset frame under ETERNALVR_PACE=headset), the first present after it that
// handed nothing over (under Route S eye L's), the hand-over (publishSlot, eye R's present) and that
// present's own return. The XR side, on the worker: each headset frame's xrWaitFrame call and return and the
// moment it chooses its image (the hold's signal), the slot taken (after any wait for the game's GPU work on
// the shared fence), the copy done, and the xrEndFrame call. An image is shown by the frame whose copy of it
// finished.
//
// Every sample is in milliseconds. While the game is held, the release that ended a wait comes right after a
// headset frame's choice (`wake`), and the image must be handed over before the next choice (`slack`, its
// deadline; negative: late, that frame repeats the previous image). No OpenXR or Windows here: times are the
// caller's seconds on one monotonic clock, and the caller serialises the calls.

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <vector>

namespace evr::pacing {

inline constexpr double kNoTime = std::numeric_limits<double>::quiet_NaN();

// One image, from the release before it to the xrEndFrame that showed it.
struct LatencyRecord {
    std::uint64_t value = 0;            // its timeline value
    double released = kNoTime;          // the render thread went on after the previous hand-over
    bool held = false;                  // ... at the end of a wait for a headset frame
    double heldChoice = kNoTime;        // ... the choice that ended that wait
    double firstPresent = kNoTime;      // the first present after the release that handed nothing over
    double handOver = kNoTime;          // publishSlot
    double presentReturned = kNoTime;   // the hand-over's present hook returned (after any hold)
    double deadline = kNoTime;          // the first headset frame chosen after the release
    std::uint64_t framesAtRelease = 0;  // headset frames chosen by the release ...
    std::uint64_t framesAtHandOver = 0; // ... and by the hand-over
    // The headset frame whose copy of it finished (its choice count; 0: never shown) and its times.
    std::uint64_t shownFrame = 0;
    double waitCalled = kNoTime;   // its xrWaitFrame call
    double waitReturned = kNoTime; // ... and return
    double chosen = kNoTime;       // it chose its image
    double taken = kNoTime;        // the slot was taken (after any wait for the GPU work)
    double copied = kNoTime;       // the copy to the runtime's image finished
    double endCalled = kNoTime;    // xrEndFrame was called
};

// The samples of a window (ms), and its counts.
struct LatencyWindow {
    // Game side, per image handed over after a known release.
    std::vector<double> releaseToHandOver;      // the game's own time for the image
    std::vector<double> releaseToFirstPresent;  // ... up to the first present (eye L's)
    std::vector<double> firstPresentToHandOver; // ... from there to the hand-over (eye R's)
    std::vector<double> handOverToReturn;       // the present after publishSlot, and the hold's wait
    std::vector<double> wake;                   // held: the choice that ended the wait to the release
    std::vector<double> slack;                  // held: the next choice after the release less the hand-over
    // XR side, per image shown.
    std::vector<double> handOverToEnd;   // hand-over to the xrEndFrame call of the frame that showed it
    std::vector<double> beforeWaitFrame; // ... spent before that frame's xrWaitFrame call
    std::vector<double> inWaitFrame;     // ... in its xrWaitFrame
    std::vector<double> beginToChoice;   // ... from xrWaitFrame's return to the choice
    std::vector<double> choiceToTaken;   // ... the choice to the slot taken (the GPU fence)
    std::vector<double> takenToCopied;   // ... the copy
    std::vector<double> copiedToEnd;     // ... the copy to xrEndFrame (the layers)
    std::vector<double> choiceInterval;  // between two headset frames' choices
    std::uint64_t handOvers = 0;
    std::uint64_t heldReleases = 0;     // images whose release ended a wait for a headset frame
    std::uint64_t late = 0;             // ... handed over after the next choice
    std::uint64_t shownFirst = 0;       // shown by the first headset frame chosen after the hand-over
    std::uint64_t shownLater = 0;       // ... by a later one (a frame in between showed an older image)
    std::uint64_t neverShown = 0;       // a newer image was shown first
    std::uint64_t unfollowed = 0;       // more images open than kOpen: the oldest dropped unfinished
    std::vector<LatencyRecord> records; // every image finished in the window, shown or not
};

class PresentLatency {
public:
    // Images followed at once from hand-over to shown.
    static constexpr std::size_t kOpen = 8;

    // Game side. publishSlot handed image `value` over.
    void handOver(std::uint64_t value, double now);
    // Every present of the game returned (after the hold); `held`: it waited for a headset frame, which
    // began.
    void presentReturned(double now, bool held);

    // XR worker. A headset frame chose its image; its xrWaitFrame was called and returned at the times
    // given.
    void frameChosen(double waitCalled, double waitReturned, double now);
    // The slot of image `value` was taken for the copy.
    void slotTaken(std::uint64_t value, double now);
    // The copy of image `value` finished: the current headset frame shows it.
    void copied(std::uint64_t value, double now);
    // The current headset frame calls xrEndFrame.
    void endCalled(double now);

    // Keep every finished image in LatencyWindow::records (a rig test's table); off by default.
    void keepRecords(bool keep) { keepRecords_ = keep; }
    // The window since the last call, and starts a new one.
    LatencyWindow take();

private:
    struct Frame {
        std::uint64_t index = 0;
        double waitCalled = kNoTime;
        double waitReturned = kNoTime;
        double chosen = kNoTime;
    };

    LatencyRecord* find(std::uint64_t value);
    void finish(const LatencyRecord& r, bool shown);
    double choiceOf(std::uint64_t frame) const;

    // The release the next hand-over follows.
    double released_ = kNoTime;
    bool releasedHeld_ = false;
    double releasedChoice_ = kNoTime;
    double firstPresent_ = kNoTime;
    std::uint64_t framesAtRelease_ = 0;
    bool handedOver_ = false; // a hand-over since the last present returned
    double lastHandOver_ = kNoTime;
    std::uint64_t lastValue_ = 0;
    // Headset frames.
    std::uint64_t frames_ = 0;
    std::array<double, kOpen> choices_{}; // by frame index modulo kOpen
    Frame frame_;
    std::deque<LatencyRecord> open_;
    std::vector<std::uint64_t> copiedThisFrame_;
    LatencyWindow window_;
    bool keepRecords_ = false;
};

} // namespace evr::pacing
