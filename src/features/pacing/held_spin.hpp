#pragma once

// How the held game's render thread waits for the headset's next frame (T-232; docs/VR_STEREO.md "Frame
// pacing", The held wait; docs/rig-findings/pace-hold.md; vkcore/held_wait.* carries the plan out). Sleeping
// through the whole wait let the CPU's clock fall on the test PC (an i7-9700K on Windows' Balanced plan), so
// the held tick no longer fitted in the period. Once a hold has fallen behind in play twice within
// HoldSwitch::kLatchWindowSeconds (one hitch falls once and latches nothing), the thread sleeps
// until the spin's length before the frame is due and spins the rest, at most the spin's length plus
// kHeldSpinOverSeconds (a late or stalled headset frame does not keep a core busy for the whole timeout);
// what is left of the timeout it sleeps. Never in a menu or a loading screen (the loader keeps the core),
// never in the low hold while nobody looks (T-228). Once latched it stays so for the session: a hold
// that sleeps again lets the clock fall again, and the switch would let go and hold again over and over.
//
// No Windows here: times are seconds on the pacer's clock.

#include "features/pacing/pace_policy.hpp"

#include <cstdint>

namespace evr::pacing {

// ETERNALVR_TEST_HOLD_SPIN: unset OnceBehind, `0` Sleep (never spins), `1` Always (spins the whole wait).
enum class HeldSpin { Sleep, OnceBehind, Always };

inline constexpr double kHeldSpinSeconds = 0.005;
inline constexpr double kHeldSpinOverSeconds = 0.001;

struct HeldWaitInput {
    HeldSpin mode = HeldSpin::OnceBehind;
    double spinSeconds = kHeldSpinSeconds; // ETERNALVR_TEST_HOLD_SPIN_MS
    bool fellBehind = false;               // HoldSwitch::hasFallenBehind
    bool low = false;                      // the low hold while nobody looks
    bool inPlay = true;                    // not a menu or a loading screen
    HeadsetLoop loop;
    PaceStep step;
    double nowSeconds = 0.0;
};

struct HeldWaitPlan {
    bool spin = false;
    double sleepSeconds = 0.0;     // spinning: sleep first this long (0: none); not spinning: the whole wait
    double spinLimitSeconds = 0.0; // then spin at most this long; the rest of the timeout is slept
};

HeldWaitPlan planHeldWait(const HeldWaitInput& in);

} // namespace evr::pacing
