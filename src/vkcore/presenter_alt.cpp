// Alternate eyes in the present hook (presenter_alt.hpp, docs/rig-findings/alternate-eye.md).

#include "vkcore/presenter_impl.hpp"

#include "vkcore/presenter_alt.hpp"
#include "vkcore/seq_alternate.hpp"

namespace evr::vkcore {

using stereo_seq::AltAction;

namespace {

stereo_seq::PresentKind kindOf(int eyeIndex) {
    return eyeIndex == 1 ? stereo_seq::PresentKind::EyeR : stereo_seq::PresentKind::EyeL;
}

// The fresh eye into its half of a new slot, held for the next present (no partner yet).
VkSemaphore hold(XrPresenter::Impl& p,
                 VkQueue queue,
                 std::uint32_t family,
                 SwapchainState& sc,
                 std::uint32_t imageIndex,
                 const VkPresentInfoKHR* info,
                 FamilyCommands& fc,
                 std::uint64_t completed,
                 const stereo_seq::AltStep& step) {
    const bool left = step.freshIndex == 0;
    if (!left) {
        p.capture.cancel(); // a captured pair starts with eye L
    }
    const std::uint32_t slotIndex = p.acquireFreeSlot(fc, completed);
    if (slotIndex == kRingSize) {
        p.alt.pairing.carryNotStored();
        ++p.framesDropped;
        return VK_NULL_HANDLE;
    }
    const auto eye = static_cast<std::uint32_t>(step.freshIndex);
    const VkBuffer buffer =
        left ? p.pairCaptureBuffer(sc, completed, p.alt.pairing.stats().published, step.freshTick)
             : VK_NULL_HANDLE;
    const stereo_seq::PresentKind kind = kindOf(step.freshIndex);
    const bool toWindow = p.decideWindow(info, kind);
    const std::uint64_t value = p.submitCopy(
        queue, family, info, sc, imageIndex, fc, slotIndex,
        CopyTarget{eye, 1, false, false, p.windowMirrorStep(eye, toWindow), toWindow, kind}, buffer);
    if (value == 0) {
        p.ring[slotIndex].state.store(kSlotFree);
        p.alt.pairing.carryNotStored();
        p.capture.cancel();
        ++p.framesDropped;
        return VK_NULL_HANDLE;
    }
    if (left) {
        p.captureCopied(buffer, value, false);
    }
    p.pendingSlot = slotIndex; // stays kSlotWriting until the other eye's present completes it
    return sc.presentSemaphores[imageIndex];
}

// The fresh eye into its half of the held slot, which is then shown, and into its half of a new held slot.
VkSemaphore publish(XrPresenter::Impl& p,
                    VkQueue queue,
                    std::uint32_t family,
                    SwapchainState& sc,
                    std::uint32_t imageIndex,
                    const VkPresentInfoKHR* info,
                    FamilyCommands& fc,
                    std::uint64_t completed,
                    const stereo_seq::AltStep& step) {
    const std::uint32_t slotIndex = p.pendingSlot;
    p.pendingSlot = kRingSize;
    if (slotIndex >= kRingSize) {
        p.alt.pairing.publishNotStored(); // not expected: a held image always has its slot
        p.capture.cancel();
        ++p.framesDropped;
        return VK_NULL_HANDLE;
    }
    const bool left = step.freshIndex == 0;
    const auto eye = static_cast<std::uint32_t>(step.freshIndex);
    // Never the held slot (kSlotWriting) nor the newest published one.
    const std::uint32_t carry = p.acquireFreeSlot(fc, completed);
    if (carry < kRingSize) {
        p.ring[carry].ui.written = false; // its GUI image comes with the present that shows it
    }
    const VkBuffer buffer =
        left ? p.pairCaptureBuffer(sc, completed, p.alt.pairing.stats().published, step.freshTick)
             : p.capture.bufferFor(p.dev, 1, 0, sc.format, sc.extent, completed);
    const stereo_seq::PresentKind kind = kindOf(step.freshIndex);
    const bool toWindow = p.decideWindow(info, kind);
    CopyTarget target{eye, 1, true, p.settings.ui.enabled, p.windowMirrorStep(eye, toWindow), toWindow, kind};
    target.carrySlot = carry;
    const std::uint64_t value =
        p.submitCopy(queue, family, info, sc, imageIndex, fc, slotIndex, target, buffer);
    if (value == 0) {
        p.ring[slotIndex].state.store(kSlotFree);
        if (carry < kRingSize) {
            p.ring[carry].state.store(kSlotFree);
        }
        p.alt.pairing.publishNotStored();
        p.capture.cancel();
        ++p.framesDropped;
        return VK_NULL_HANDLE;
    }
    if (left) {
        p.captureCopied(buffer, value, false);
    } else {
        if (buffer) {
            p.capture.copySubmitted(value);
        }
        p.capture.submitted(value, step.freshTick);
    }
    if (carry < kRingSize) {
        p.pendingSlot = carry;
    } else {
        p.alt.pairing.carryNotStored(); // the next present starts again (the headset repeats this one)
    }
    ViewRecord fresh;
    ViewRecord held;
    RingSlot& slot = p.ring[slotIndex];
    if (!p.viewBySeq(step.freshTick, fresh) || !p.viewBySeq(step.heldTick, held)) {
        // Without both records the halves' poses are unknown: not shown (the copy still runs before the
        // present).
        ++p.pairsWithoutRecord;
        slot.state.store(kSlotFree);
        ++p.framesDropped;
        return sc.presentSemaphores[imageIndex];
    }
    slot.view = stereo_seq::composeHalves(fresh, held, 1 - step.freshIndex);
    slot.hasView = true;
    slot.view.showEyes = fresh.stereo && held.stereo && !p.settings.stereo.seqView.sameView;
    p.publishSlot(slotIndex, value);
    p.pairsPublished.fetch_add(1, std::memory_order_relaxed);
    return sc.presentSemaphores[imageIndex];
}

} // namespace

std::optional<VkSemaphore> altCopyForPresent(XrPresenter::Impl& p,
                                             VkQueue queue,
                                             std::uint32_t family,
                                             SwapchainState& sc,
                                             std::uint32_t imageIndex,
                                             const VkPresentInfoKHR* info,
                                             FamilyCommands& fc,
                                             std::uint64_t completed,
                                             const stereo_seq::PresentMatch& match) {
    seq_alternate::noteDisplayPeriodNs(p.displayPeriod.load(std::memory_order_relaxed)); // auto's target
    const stereo_seq::AltStep step = p.alt.pairing.onPresent(match);
    if (step.abandoned) {
        p.releasePendingSlot(); // frees the held slot and gives up a captured pair
    }
    p.logSeqStats();
    switch (step.action) {
    case AltAction::Drop:
        return VK_NULL_HANDLE; // the present goes out unchanged, nothing reaches the headset
    case AltAction::Hold:
        return hold(p, queue, family, sc, imageIndex, info, fc, completed, step);
    case AltAction::Publish:
        return publish(p, queue, family, sc, imageIndex, info, fc, completed, step);
    case AltAction::ShowMono:
        break;
    }
    return std::nullopt;
}

void altPresentNotCopied(XrPresenter::Impl& p, const stereo_seq::PresentMatch& match) {
    const stereo_seq::AltStep step = p.alt.pairing.onPresent(match);
    if (step.abandoned) {
        p.releasePendingSlot();
    }
    if (step.action == AltAction::Publish) {
        p.alt.pairing.publishNotStored();
        p.releasePendingSlot();
    } else if (step.action == AltAction::Hold) {
        p.alt.pairing.carryNotStored();
    }
}

void logAltStats(XrPresenter::Impl& p, const SeqCounters& now, const SeqCounters& last) {
    if (!seqAlternateEyes()) {
        return;
    }
    const stereo_seq::AlternatePairing::Stats& a = p.alt.pairing.stats();
    const stereo_seq::AlternatePairing::Stats& l = p.alt.last;
    const auto d = [](std::uint64_t x, std::uint64_t y) {
        return static_cast<unsigned long long>(x - y);
    };
    const std::uint64_t published = a.published - l.published;
    EVR_LOG(
        "seq: alternate eyes: %llu eye L / %llu eye R render(s); %llu shown (a fresh eye beside the other "
        "eye's newest), %llu held without a partner, %llu mono; %llu dropped without a view, %llu held "
        "image(s) given up, %llu not stored; the held eye %.2f game frame(s) older on average (at most %llu "
        "since the start)",
        d(now.altRenders[0], last.altRenders[0]), d(now.altRenders[1], last.altRenders[1]),
        static_cast<unsigned long long>(published), d(a.held, l.held), d(a.mono, l.mono),
        d(a.withoutView, l.withoutView), d(a.abandoned, l.abandoned), d(a.notStored, l.notStored),
        published ? static_cast<double>(a.heldAgeSum - l.heldAgeSum) / static_cast<double>(published) : 0.0,
        static_cast<unsigned long long>(a.heldAgeMax));
    if (seqAdaptiveEyes()) {
        EVR_LOG(
            "seq: adaptive eyes: %llu stereo tick(s) with both eyes (eye R nested); %llu eye L image(s) of "
            "them held for their eye R, %llu shown with it (the rest of those shown: a fresh eye beside the "
            "other eye's image of the tick before)",
            d(now.stereoTicks, last.stereoTicks), d(a.pairStarts, l.pairStarts), d(a.sameTick, l.sameTick));
    }
    p.alt.last = a;
}

} // namespace evr::vkcore
