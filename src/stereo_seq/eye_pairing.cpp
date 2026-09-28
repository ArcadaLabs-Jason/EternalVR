#include "stereo_seq/eye_pairing.hpp"

namespace evr::stereo_seq {

const char* pairActionName(PairAction action) {
    switch (action) {
    case PairAction::ShowMono:
        return "show mono";
    case PairAction::StartPair:
        return "start pair";
    case PairAction::CompletePair:
        return "complete pair";
    case PairAction::Drop:
        break;
    }
    return "drop";
}

bool EyePairing::abandonPending() {
    if (!pending_) {
        return false;
    }
    pending_ = false;
    ++stats_.leftDropped;
    return true;
}

PairStep EyePairing::onPresent(const PresentMatch& present) {
    ++stats_.presents;
    PairStep step;
    if (!present.tagged || present.tag.eye == Eye::Mono) {
        step.abandoned = abandonPending();
        step.action = PairAction::ShowMono;
        ++stats_.mono;
        return step;
    }
    const RenderTag& tag = present.tag;
    if (tag.eye == Eye::Left) {
        step.abandoned = abandonPending();
        if (!tag.viewApplied) {
            ++stats_.withoutView;
            step.action = PairAction::Drop;
            return step;
        }
        pending_ = true;
        pendingTick_ = tag.tick;
        ++stats_.pairsStarted;
        step.action = PairAction::StartPair;
        return step;
    }
    // Right eye.
    step.action = PairAction::Drop;
    if (!pending_) {
        ++stats_.rightDropped;
        return step;
    }
    if (pendingTick_ != tag.tick || !tag.viewApplied) {
        if (!tag.viewApplied) {
            ++stats_.withoutView;
        }
        step.abandoned = abandonPending();
        ++stats_.rightDropped;
        return step;
    }
    pending_ = false;
    ++stats_.pairsCompleted;
    step.action = PairAction::CompletePair;
    return step;
}

void EyePairing::leftNotStored() {
    if (pending_) {
        pending_ = false;
        ++stats_.leftDropped;
        ++stats_.notStored;
    }
}

void EyePairing::rightNotStored() {
    // CompletePair already cleared the pending state and counted the pair; undo the count.
    if (stats_.pairsCompleted > 0) {
        --stats_.pairsCompleted;
    }
    ++stats_.leftDropped;
    ++stats_.rightDropped;
    ++stats_.notStored;
}

} // namespace evr::stereo_seq
