#pragma once

// Alternate eyes in the present hook (ETERNALVR_ALTERNATE_EYES=1, docs/rig-findings/alternate-eye.md): each
// game tick presents one eye. The ring slot the next present completes always holds the other eye's newest
// image: a fresh eye's image goes into its half of that slot, which is then shown, and in the same copy into
// its half of a new slot, held for the next present (the other eye's). Each half is shown with the pose it
// was rendered with (stereo_seq::composeHalves). The pairing rules are stereo_seq::AlternatePairing.

#include "stereo_seq/alternate_eyes.hpp"
#include "stereo_seq/eye_pairing.hpp"
#include "vkcore/dispatch.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/xr_presenter.hpp"

#include <cstdint>
#include <optional>

namespace evr::vkcore {

struct SwapchainState;
struct FamilyCommands;

// The presenter's alternate-eye state (present hook, under the presenter's mutex).
struct AltPresentState {
    stereo_seq::AlternatePairing pairing;
    stereo_seq::AlternatePairing::Stats last; // at the last 10 s line
};

// Present hook, under the presenter's mutex, instead of Route S's pairing: the semaphore the present waits on
// (VK_NULL_HANDLE: the present goes out unchanged), or nullopt for a mono frame, which the caller copies into
// both halves as under Route S.
std::optional<VkSemaphore> altCopyForPresent(XrPresenter::Impl& p,
                                             VkQueue queue,
                                             std::uint32_t family,
                                             SwapchainState& sc,
                                             std::uint32_t imageIndex,
                                             const VkPresentInfoKHR* info,
                                             FamilyCommands& fc,
                                             std::uint64_t completed,
                                             const stereo_seq::PresentMatch& match);

// A tagged present that is not copied still moves the pairing on.
void altPresentNotCopied(XrPresenter::Impl& p, const stereo_seq::PresentMatch& match);

// The 10 s line (with the seq: block): renders per eye from the counters, the pairing's counts.
void logAltStats(XrPresenter::Impl& p, const SeqCounters& now, const SeqCounters& last);

} // namespace evr::vkcore
