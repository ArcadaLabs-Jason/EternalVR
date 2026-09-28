#include "stereo_seq/object_prev.hpp"

namespace evr::stereo_seq {

namespace {

// A counter the engine turns into `slot`: it uses counter % 3 for the current frame's buffers and
// (counter + 2) % 3 for the previous frame's.
std::uint32_t currentFor(std::uint32_t counter, int slot) {
    return counter + static_cast<std::uint32_t>((slot - static_cast<int>(counter % 3u) + 3) % 3);
}
std::uint32_t previousFor(std::uint32_t counter, int slot) {
    return counter + static_cast<std::uint32_t>((slot - 2 - static_cast<int>(counter % 3u) + 6) % 3);
}

} // namespace

ObjectRing::Picks ObjectRing::picksFor(Eye eye, std::uint64_t tick, std::uint32_t counter, bool* inOrder) {
    for (const Answer& a : answers_) {
        if (a.valid && a.counter == counter) {
            return a.picks;
        }
    }
    if (inOrder) {
        *inOrder = !asked_ || counter == lastAsked_ + 1u;
    }
    asked_ = true;
    lastAsked_ = counter;
    const Picks picks = decide(eye, tick, counter);
    answers_[nextAnswer_] = Answer{true, counter, picks};
    nextAnswer_ = (nextAnswer_ + 1) % kAnswers;
    return picks;
}

int ObjectRing::previousSlot(Eye eye, std::uint64_t tick) const {
    if (eye != Eye::Mono && tick > 0) {
        int latest = -1;
        for (int i = 0; i < 3; ++i) {
            const Slot& s = slots_[i];
            if (!s.written || s.tick != tick - 1) {
                continue;
            }
            // Eye R reads the tick before's eye R when it is still there; otherwise the tick before's latest.
            if (eye == Eye::Right && s.eye == Eye::Right) {
                return i;
            }
            if (latest < 0 || s.writer - slots_[latest].writer < 0x80000000u) {
                latest = i;
            }
        }
        if (latest >= 0) {
            return latest;
        }
    }
    return lastSlot_; // the render just before (the engine's own choice), or none yet
}

ObjectRing::Picks ObjectRing::decide(Eye eye, std::uint64_t tick, std::uint32_t counter) {
    int previous = previousSlot(eye, tick);
    // Only a slot the render just before wrote or read: the slot written here is then the third one, which
    // that render did not touch. A render between ticks (untagged, or mono with a stale tick) can leave the
    // tick before's eye R in that third slot, and reading it would make this render write a slot the render
    // just before still uses. That render's previous frame is the next best.
    if (previous >= 0 && previous != lastSlot_ && previous != lastRead_) {
        previous = lastRead_;
    }
    // The least recently used slot that is not the one read as the previous frame.
    int slot = -1;
    for (int i = 0; i < 3; ++i) {
        if (i == previous) {
            continue;
        }
        if (slot < 0 || (!slots_[i].used && slots_[slot].used) ||
            (slots_[i].used == slots_[slot].used &&
             slots_[i].lastUse - slots_[slot].lastUse >= 0x80000000u)) {
            slot = i;
        }
    }
    const int read = previous >= 0 ? previous : slot;
    slots_[slot] = Slot{true, eye, tick, counter, true, counter};
    slots_[read].used = true;
    slots_[read].lastUse = counter;
    lastSlot_ = slot;
    lastRead_ = read;
    const Picks p{currentFor(counter, slot), previousFor(counter, read), true};
    return p.current == counter && p.previous == counter ? Picks{counter, counter, false} : p;
}

} // namespace evr::stereo_seq
