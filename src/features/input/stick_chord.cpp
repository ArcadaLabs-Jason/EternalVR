#include "features/input/stick_chord.hpp"

#include "common/finite.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace evr::input {

StickChord::StickChord(float holdSeconds, float windowSeconds)
    : holdSeconds_(finiteInRangeOr(holdSeconds, 0.0f, kMaxHoldSeconds, kDefaultHoldSeconds)),
      windowSeconds_(finiteInRangeOr(windowSeconds, 0.0f, 1.0f, kStickChordWindowSeconds)) {}

StickChordOutput StickChord::update(bool left, bool right, float dtSeconds) {
    const float dt = std::isfinite(dtSeconds) && dtSeconds > 0.0f ? dtSeconds : 0.0f;
    const std::array<bool, 2> now{left, right};
    std::array<bool, 2> pressed{};
    std::array<bool, 2> pulse{};
    bool formed = false;

    for (std::size_t i = 0; i < 2; ++i) {
        pressed[i] = now[i] && !down_[i];
        held_[i] = pressed[i] ? 0.0f : (now[i] ? held_[i] + dt : 0.0f);
        if (!now[i] && down_[i]) {
            // A stick let go: a second click let go inside the window was a click after all.
            pulse[i] = withheld_[i] && !chord_;
            withheld_[i] = false;
            suppressed_[i] = false;
        }
    }
    for (std::size_t i = 0; i < 2; ++i) {
        const std::size_t j = 1 - i;
        if (!pressed[i] || !now[j]) {
            continue;
        }
        if (chord_) {
            suppressed_[i] = true; // back into a chord still held on the other stick: it starts again
            chordSeconds_ = 0.0f;
        } else if (pressed[j] || held_[j] <= windowSeconds_) {
            formed = true; // both pressed together
        } else {
            withheld_[i] = true; // the other stick is held: wait to see which this is
        }
    }
    for (std::size_t i = 0; i < 2; ++i) {
        if (!withheld_[i]) {
            continue;
        }
        if (!now[1 - i]) {
            withheld_[i] = false; // the other stick went up: an ordinary press, now visible
        } else if (held_[i] >= windowSeconds_) {
            formed = true; // held past the window with the other: the chord
        }
    }
    if (formed && !chord_) {
        chord_ = true;
        chordSeconds_ = std::min(held_[0], held_[1]);
        for (std::size_t i = 0; i < 2; ++i) {
            suppressed_[i] = now[i];
            withheld_[i] = false;
        }
    } else if (chord_ && now[0] && now[1]) {
        chordSeconds_ += dt;
    }
    if (chord_ && !now[0] && !now[1]) {
        chord_ = false;
    }

    StickChordOutput out;
    for (std::size_t i = 0; i < 2; ++i) {
        out.click[i] = (now[i] && !suppressed_[i] && !withheld_[i]) || pulse[i];
    }
    out.chord = chord_ && now[0] && now[1];
    out.recenter = out.chord && chordSeconds_ >= holdSeconds_;
    down_ = now;
    return out;
}

} // namespace evr::input
