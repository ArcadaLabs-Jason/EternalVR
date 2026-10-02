#pragma once

// Head aim's start-up check (docs/VR_HEAD_TRACKED.md): before head aim writes anything, the game's view
// angles must be its command angles plus one of the two deltaViewAngles (physics or state) in 54 of 60
// frames. Frames the game drives (a cutscene, a forced view, a menu) are not counted: there the view angles
// can come from somewhere else, and a check that overlapped a level's opening cutscene once turned head aim
// off for the whole session. A failed try is not final: once the player has had their own view for a while
// (longer after each failure) the check runs again, up to five more times. Pure logic, one call per frame.

#include "xr_math/head_aim.hpp"

namespace evr::xr_math {

class AimCheck {
public:
    static constexpr int kFrames = 60; // frames counted per try
    static constexpr int kNeeded = 54; // matching frames a try needs to pass
    static constexpr int kRetries = 5; // tries after the first one fails
    // Undriven frames in a row before the next try, times the tries so far (120, 240, ... 600).
    static constexpr int kSettleFrames = 120;

    enum class Event {
        Counted, // the frame was counted; the try goes on
        Skipped, // a driven frame during a try: not counted
        Waiting, // between tries
        Retry,   // the next try starts with the next frame
        Passed,  // this frame ended a try that passed
        Failed,  // this frame ended a try that failed; another follows
        GaveUp,  // this frame ended the last try, which failed
        Over,    // the check had already passed or given up
    };

    struct Step {
        Event event = Event::Over;
        bool counted = false;      // the frame was counted (Counted, Passed, Failed, GaveUp)
        bool physicsMatch = false; // a counted frame: view = command + physics delta
        bool stateMatch = false;   // a counted frame: view = command + state delta
    };

    // `driven`: a cutscene plays, the game forces the view, or a menu is up.
    Step update(const IdAngles& view,
                const IdAngles& command,
                const IdAngles& delta,
                const IdAngles& stateDelta,
                bool driven);

    [[nodiscard]] bool passed() const { return phase_ == Phase::Passed; }
    [[nodiscard]] bool gaveUp() const { return phase_ == Phase::GaveUp; }
    [[nodiscard]] bool physics() const { return physics_; } // passed through the physics delta
    [[nodiscard]] int tryNumber() const { return retries_ + 1; }
    [[nodiscard]] int settleFrames() const { return kSettleFrames * tryNumber(); }
    // This try's counts (kept after its verdict until the next try starts).
    [[nodiscard]] int checks() const { return checks_; }
    [[nodiscard]] int physicsMatches() const { return physicsMatches_; }
    [[nodiscard]] int stateMatches() const { return stateMatches_; }
    [[nodiscard]] int mismatches() const { return mismatches_; } // counted frames matching neither delta
    [[nodiscard]] int skipped() const { return skipped_; }

private:
    enum class Phase { Checking, Settling, Passed, GaveUp };
    Phase phase_ = Phase::Checking;
    bool physics_ = false;
    int retries_ = 0;
    int settled_ = 0;
    int checks_ = 0;
    int physicsMatches_ = 0;
    int stateMatches_ = 0;
    int mismatches_ = 0;
    int skipped_ = 0;
};

} // namespace evr::xr_math
