#include "stereo_seq/alternate_eyes.hpp"

#include "stereo_seq/stereo_taa.hpp"

#include <algorithm>

namespace evr::stereo_seq {

Eye EyeAlternator::eyeFor(std::uint32_t renderFrame) {
    if (decided_ && decidedFrame_ == renderFrame) {
        return decidedEye_;
    }
    const bool followsLeft = lastStereo_ && !lastPairDone_ && lastEye_ == Eye::Left &&
                             static_cast<std::uint32_t>(renderFrame - lastFrame_) == 1;
    decided_ = true;
    decidedFrame_ = renderFrame;
    decidedEye_ = followsLeft ? Eye::Right : Eye::Left;
    return decidedEye_;
}

void EyeAlternator::rendered(std::uint32_t renderFrame, Eye eye, bool stereo, bool pairDone) {
    lastStereo_ = stereo && eye != Eye::Mono;
    lastPairDone_ = pairDone;
    lastFrame_ = renderFrame;
    lastEye_ = eye;
    if (lastStereo_) {
        ++stats_.renders[eyeIndex(eye)];
    } else {
        ++stats_.mono;
    }
}

const char* altActionName(AltAction action) {
    switch (action) {
    case AltAction::ShowMono:
        return "show mono";
    case AltAction::Hold:
        return "hold";
    case AltAction::Publish:
        return "publish";
    case AltAction::Drop:
        break;
    }
    return "drop";
}

bool AlternatePairing::abandon() {
    if (!holding_) {
        return false;
    }
    holding_ = false;
    ++stats_.abandoned;
    return true;
}

AltStep AlternatePairing::onPresent(const PresentMatch& present) {
    ++stats_.presents;
    AltStep step;
    if (!present.tagged || present.tag.eye == Eye::Mono) {
        step.abandoned = abandon();
        step.action = AltAction::ShowMono;
        ++stats_.mono;
        return step;
    }
    const RenderTag& tag = present.tag;
    if (!tag.viewApplied) {
        step.abandoned = abandon();
        ++stats_.withoutView;
        return step; // Drop
    }
    step.freshIndex = eyeIndex(tag.eye);
    step.freshTick = tag.tick;
    // A tick that renders both eyes (pairInTick): its eye L waits for its own eye R, which pairs with it at
    // age 0.
    const bool sameTick =
        tag.pairInTick && tag.eye == Eye::Right && heldEye_ == Eye::Left && tag.tick == heldTick_;
    const bool partner = holding_ && heldEye_ != tag.eye && !(tag.pairInTick && tag.eye == Eye::Left) &&
                         (sameTick || (tag.tick > heldTick_ && tag.tick - heldTick_ <= maxHeldAge_));
    if (partner) {
        const std::uint64_t age = tag.tick - heldTick_;
        step.action = AltAction::Publish;
        step.heldTick = heldTick_;
        ++stats_.published;
        stats_.heldAgeSum += age;
        stats_.heldAgeMax = std::max(stats_.heldAgeMax, age);
        if (sameTick) {
            ++stats_.sameTick;
        }
    } else if (tag.pairInTick && tag.eye == Eye::Left) {
        // Eye L of a tick that renders both eyes: held for its own eye R; the other eye's older image, if
        // one is held, is released (not counted as given up).
        step.abandoned = holding_;
        holding_ = false;
        step.action = AltAction::Hold;
        ++stats_.pairStarts;
    } else {
        step.abandoned = abandon();
        step.action = AltAction::Hold;
        ++stats_.held;
    }
    // Either way this image is the one the next present of the other eye pairs with.
    holding_ = true;
    heldEye_ = tag.eye;
    heldTick_ = tag.tick;
    return step;
}

void AlternatePairing::carryNotStored() {
    if (holding_) {
        holding_ = false;
        ++stats_.notStored;
    }
}

void AlternatePairing::publishNotStored() {
    if (stats_.published > 0) {
        --stats_.published;
    }
    holding_ = false;
    ++stats_.notStored;
}

bool AlternateTaaReset::onEye(Eye eye, std::uint64_t gameFrame) {
    const bool sameTick =
        sameTickPairs_ && eye == Eye::Right && lastEye_ == Eye::Left && gameFrame == lastFrame_;
    const bool continues =
        lastFrame_ != 0 && eye != Eye::Mono && lastEye_ != eye && (gameFrame == lastFrame_ + 1 || sameTick);
    run_ = continues ? run_ + 1 : 1;
    lastFrame_ = gameFrame;
    lastEye_ = eye;
    // The first render of a run has no history of its own, nor has the second (the other eye's last render
    // lies before the break); from the third on, each eye's last render is the one two renders back.
    return run_ < 3;
}

std::uint8_t alternateSubSample(std::uint64_t gameFrame, int numSubSamples) {
    return taaSubSample(gameFrame / 2, numSubSamples);
}

} // namespace evr::stereo_seq
