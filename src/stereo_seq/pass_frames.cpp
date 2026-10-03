#include "stereo_seq/pass_frames.hpp"

#include <algorithm>

namespace evr::stereo_seq {

void PassFrames::begin(std::uint64_t commandBuffer) {
    if (const auto it = entries_.find(commandBuffer); it != entries_.end()) {
        endRecording(it->second);
        it->second.begun = true;
        it->second.learned = false;
        it->second.parityGuess.reset();
        it->second.contradicted = false;
    }
}

void PassFrames::reset(std::uint64_t commandBuffer) {
    if (const auto it = entries_.find(commandBuffer); it != entries_.end()) {
        endRecording(it->second);
        it->second.begun = false;
        it->second.learned = false;
        it->second.parityGuess.reset();
        it->second.contradicted = false;
    }
}

void PassFrames::forget(std::uint64_t commandBuffer) {
    entries_.erase(commandBuffer);
}

void PassFrames::endRecording(Entry& e) {
    if (e.begun && !e.learned) {
        e.streak = 0; // a recording seen whole without an agreement: its frame is not known
    }
}

void PassFrames::breakParity() {
    ++stats_.parityBreaks;
    ++epoch_;
}

void PassFrames::contradict(Entry& e) {
    if (!e.contradicted) {
        e.contradicted = true;
        ++stats_.contradicted;
    }
}

void PassFrames::agree(Entry& e, std::uint32_t now) {
    if (e.learned) {
        if (e.counter != now) { // one recording over two frames: not one of the two sets' buffers
            contradict(e);      // its guess for the passes in between was the earlier frame
            e.counter = now;
            e.streak = 0;
            e.parityLearned = false;
        }
        return;
    }
    e.learned = true;
    const bool against = e.epoch == epoch_ && e.parityLearned && (now & 1u) != e.parity;
    const bool guessedOther = e.parityGuess && *e.parityGuess != now; // its parity guessed another frame
    if (guessedOther) {
        contradict(e);
    }
    if (against || (guessedOther && e.epoch == epoch_)) {
        breakParity();
    }
    if (e.epoch != epoch_) { // learned under a link since broken
        e.epoch = epoch_;
        e.streak = 0;
        e.parityLearned = false;
    }
    // Counters wrap: compared by difference. Where this recording started is not known without its begin.
    const bool next = e.begun && e.streak > 0 && now - e.counter == 2u;
    e.streak = next ? std::min(e.streak + 1, kParityRecordings) : 1;
    e.counter = now;
    if (e.streak >= kParityRecordings) {
        e.parityLearned = true;
        e.parity = static_cast<std::uint8_t>(now & 1u);
    }
}

PassFrames::Answer
PassFrames::find(std::uint64_t commandBuffer, std::uint32_t now, std::optional<std::uint32_t> renderView) {
    const auto it = entries_.find(commandBuffer);
    if (renderView && *renderView == now) {
        ++stats_.agreed;
        if (it != entries_.end()) {
            agree(it->second, now);
        } else if (!full()) {
            Entry e;
            e.counter = now;
            e.streak = 1;
            e.epoch = epoch_;
            e.learned = true;
            entries_.emplace(commandBuffer, e);
        }
        return {now, Source::Agreed};
    }
    if (it != entries_.end() && it->second.begun) {
        Entry& e = it->second;
        if (e.learned) {
            // A guess: the recording's frame, read at its counter, or at the next one after the swap.
            if (now - e.counter <= 1u) {
                ++stats_.sameRecording;
                return {e.counter, Source::SameRecording};
            }
        } else if (e.epoch == epoch_ && e.parityLearned) {
            const std::uint32_t frame = ((e.counter ^ now) & 1u) == 0 ? now : now - 1u;
            if (frame == e.counter) {
                breakParity(); // recorded again in the frame after its last agreed recording, or the same one
            } else if (e.streak >= kParityRecordings && frame - e.counter == 2u && renderView &&
                       now - *renderView <= 1u) {
                ++stats_.parity;
                if (!e.parityGuess) {
                    e.parityGuess = frame;
                }
                return {frame, Source::Parity};
            }
        }
    }
    ++stats_.unknown;
    return {std::nullopt, Source::Unknown};
}

PassFrames::Stats PassFrames::stats() const {
    Stats s = stats_;
    s.commandBuffers = entries_.size();
    return s;
}

} // namespace evr::stereo_seq
