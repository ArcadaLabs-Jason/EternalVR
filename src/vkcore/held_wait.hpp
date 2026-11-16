#pragma once

// The held game's wait for the headset's next frame under ETERNALVR_PACE=headset (T-232; docs/VR_STEREO.md
// "Frame pacing", The held wait; docs/rig-findings/pace-hold.md). Carries out pacing::planHeldWait
// (features/pacing/held_spin.hpp): sleep on the pacing condition variable, or, once a hold has fallen behind
// in play twice within the latch window, sleep on the thread's high-resolution waitable timer until the
// spin's length before the frame is due, spin (yielding the core at each turn) at most the spin's length plus
// 1 ms, and sleep what is left of the timeout. Rig tests: ETERNALVR_TEST_HOLD_SPIN=0 never spins, =1 spins
// through the whole wait in play; ETERNALVR_TEST_HOLD_SPIN_MS sets the spin.

#include "features/pacing/pace_policy.hpp"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

namespace evr::vkcore::held_wait {

// frame_pacing::configure, once: whether the pace is `headset` (the rig tests apply only then), and
// ETERNALVR_TEST_HOLD_SPIN and ETERNALVR_TEST_HOLD_SPIN_MS as text (empty when unset). Logs the rig tests.
void configure(bool headset, const std::string& spin, const std::string& spinMs);

// XR worker, at each headset frame begun, under the pacing lock: the loop's frame count, which a spinning
// wait reads without the lock.
void frameBegun(std::uint64_t frames);

// The game's render thread, held: waits until `loop.frames` reaches `step.untilFrame`, at most
// `step.timeoutSeconds`; returns whether it did. `lock` holds the pacing mutex that guards `loop`, on entry
// and on return; `begun` is notified at each headset frame. It spins only in play (`inPlay`: not a menu or a
// loading screen), not in the low hold while nobody looks (`low`, T-228), and once `fellBehind`
// (pacing::HoldSwitch::hasFallenBehind; the first spin logs one line); else it sleeps on `begun`.
bool wait(std::unique_lock<std::mutex>& lock,
          std::condition_variable& begun,
          const pacing::HeadsetLoop& loop,
          const pacing::PaceStep& step,
          bool fellBehind,
          bool low,
          bool inPlay);

// XR worker, every 10 s with frame_pacing's lines: the spin of the last 10 s, once the spin is on.
void logSummary();

} // namespace evr::vkcore::held_wait
