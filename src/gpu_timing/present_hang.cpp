#include "gpu_timing/present_hang.hpp"

#include "gpu_timing/present_stall.hpp"

namespace evr::gpu_timing {

namespace {

double ms(std::uint64_t micros) {
    return static_cast<double>(micros) / 1000.0;
}

} // namespace

HangResult HangWatch::onCheck(const HangCheck& check) {
    HangResult result;
    if (check.lastPresentMicros == 0) {
        return result;
    }
    if (notedPresent_ != 0 && check.lastPresentMicros != notedPresent_) {
        // A present after a noted gap: the game came back.
        result.action = HangAction::Resumed;
        result.gapMs =
            check.lastPresentMicros > notedPresent_ ? ms(check.lastPresentMicros - notedPresent_) : 0.0;
        result.hang = hangs_;
        notedPresent_ = 0;
        notes_ = 0;
        return result;
    }
    if (check.nowMicros <= check.lastPresentMicros) {
        return result;
    }
    const double gapMs = ms(check.nowMicros - check.lastPresentMicros);
    if (check.ticksKnown) {
        const bool tickNearPresent = check.lastTickMicros != 0 &&
                                     (check.lastTickMicros >= check.lastPresentMicros ||
                                      ms(check.lastPresentMicros - check.lastTickMicros) <= kInPlayTickMs);
        if (!tickNearPresent) {
            return result; // a loading screen or a menu
        }
    }
    const auto noted = [&](HangAction action) {
        result.action = action;
        result.gapMs = gapMs;
        result.tickAgeMs = check.lastTickMicros != 0 && check.nowMicros >= check.lastTickMicros
                               ? ms(check.nowMicros - check.lastTickMicros)
                               : -1.0;
        result.hang = hangs_;
        return result;
    };
    if (notedPresent_ == 0) {
        if (gapMs < kHangFirstNoteMs || hangs_ >= kHangsNoted) {
            return result;
        }
        notedPresent_ = check.lastPresentMicros;
        notes_ = 1;
        ++hangs_;
        return noted(HangAction::FirstNote);
    }
    if (notes_ == 1 && gapMs >= kHangSecondNoteMs) {
        notes_ = 2;
        return noted(HangAction::SecondNote);
    }
    return result;
}

} // namespace evr::gpu_timing
