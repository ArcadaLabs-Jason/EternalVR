#include "features/pacing/present_latency.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace evr::pacing {

namespace {

bool known(double t) {
    return !std::isnan(t);
}

double ms(double seconds) {
    return seconds * 1000.0;
}

} // namespace

void PresentLatency::handOver(std::uint64_t value, double now) {
    ++window_.handOvers;
    LatencyRecord r;
    r.value = value;
    r.released = released_;
    r.held = releasedHeld_;
    r.heldChoice = releasedChoice_;
    r.firstPresent = firstPresent_;
    r.handOver = now;
    r.framesAtRelease = framesAtRelease_;
    r.framesAtHandOver = frames_;
    if (known(released_) && frames_ > framesAtRelease_) {
        r.deadline = choiceOf(framesAtRelease_ + 1); // a headset frame chose before this hand-over
    }
    if (open_.size() >= kOpen) {
        ++window_.unfollowed;
        open_.pop_front();
    }
    open_.push_back(r);
    handedOver_ = true;
    lastHandOver_ = now;
    lastValue_ = value;
}

void PresentLatency::presentReturned(double now, bool held) {
    if (!handedOver_) {
        // A present that handed nothing over (under Route S eye L's): the first after the release splits the
        // game's time for the image.
        if (!known(firstPresent_) && known(released_)) {
            firstPresent_ = now;
        }
        return;
    }
    handedOver_ = false;
    window_.handOverToReturn.push_back(ms(now - lastHandOver_));
    if (!open_.empty() && open_.back().value == lastValue_) {
        open_.back().presentReturned = now; // still open (the frame that shows it may have finished first)
    }
    released_ = now;
    releasedHeld_ = held;
    releasedChoice_ = held ? choiceOf(frames_) : kNoTime;
    framesAtRelease_ = frames_;
    firstPresent_ = kNoTime;
}

void PresentLatency::frameChosen(double waitCalled, double waitReturned, double now) {
    if (known(frame_.chosen)) {
        window_.choiceInterval.push_back(ms(now - frame_.chosen));
    }
    ++frames_;
    choices_[frames_ % kOpen] = now;
    frame_ = {frames_, waitCalled, waitReturned, now};
    for (LatencyRecord& r : open_) {
        if (!known(r.deadline) && known(r.released) && r.framesAtRelease + 1 == frames_) {
            r.deadline = now;
        }
    }
}

void PresentLatency::slotTaken(std::uint64_t value, double now) {
    if (LatencyRecord* r = find(value)) {
        r->taken = now;
    }
}

void PresentLatency::copied(std::uint64_t value, double now) {
    // The worker never goes back to an older image than one it showed: those still open never will be.
    for (auto it = open_.begin(); it != open_.end();) {
        if (it->value < value && !known(it->copied)) {
            finish(*it, false);
            it = open_.erase(it);
        } else {
            ++it;
        }
    }
    LatencyRecord* r = find(value);
    if (!r) {
        return;
    }
    r->copied = now;
    r->shownFrame = frame_.index;
    r->waitCalled = frame_.waitCalled;
    r->waitReturned = frame_.waitReturned;
    r->chosen = frame_.chosen;
    copiedThisFrame_.push_back(value);
}

void PresentLatency::endCalled(double now) {
    for (const std::uint64_t value : copiedThisFrame_) {
        for (auto it = open_.begin(); it != open_.end(); ++it) {
            if (it->value == value) {
                it->endCalled = now;
                finish(*it, true);
                open_.erase(it);
                break;
            }
        }
    }
    copiedThisFrame_.clear();
}

LatencyWindow PresentLatency::take() {
    return std::exchange(window_, LatencyWindow{});
}

LatencyRecord* PresentLatency::find(std::uint64_t value) {
    for (LatencyRecord& r : open_) {
        if (r.value == value) {
            return &r;
        }
    }
    return nullptr;
}

double PresentLatency::choiceOf(std::uint64_t frame) const {
    if (frame == 0 || frame > frames_ || frames_ - frame >= kOpen) {
        return kNoTime;
    }
    return choices_[frame % kOpen];
}

void PresentLatency::finish(const LatencyRecord& r, bool shown) {
    LatencyWindow& w = window_;
    if (keepRecords_) {
        w.records.push_back(r);
    }
    if (known(r.released)) {
        w.releaseToHandOver.push_back(ms(r.handOver - r.released));
        if (known(r.firstPresent)) {
            w.releaseToFirstPresent.push_back(ms(r.firstPresent - r.released));
            w.firstPresentToHandOver.push_back(ms(r.handOver - r.firstPresent));
        }
        if (r.held) {
            ++w.heldReleases;
            if (known(r.heldChoice)) {
                w.wake.push_back(ms(r.released - r.heldChoice));
            }
            if (known(r.deadline)) {
                w.slack.push_back(ms(r.deadline - r.handOver));
                if (r.deadline < r.handOver) {
                    ++w.late;
                }
            }
        }
    }
    if (!shown) {
        ++w.neverShown;
        return;
    }
    ++(r.shownFrame <= r.framesAtHandOver + 1 ? w.shownFirst : w.shownLater);
    const double h = r.handOver;
    w.handOverToEnd.push_back(ms(r.endCalled - h));
    if (!known(r.chosen) || !known(r.taken)) {
        return;
    }
    // Each part counts from the later of the hand-over and the part's start, so the parts add up to the
    // whole.
    const double called = known(r.waitCalled) ? r.waitCalled : r.chosen;
    const double returned = known(r.waitReturned) ? r.waitReturned : r.chosen;
    w.beforeWaitFrame.push_back(ms(std::max(0.0, called - h)));
    w.inWaitFrame.push_back(ms(std::max(0.0, returned - std::max(h, called))));
    w.beginToChoice.push_back(ms(std::max(0.0, r.chosen - std::max(h, returned))));
    w.choiceToTaken.push_back(ms(r.taken - std::max(h, r.chosen)));
    w.takenToCopied.push_back(ms(r.copied - r.taken));
    w.copiedToEnd.push_back(ms(r.endCalled - r.copied));
}

} // namespace evr::pacing
