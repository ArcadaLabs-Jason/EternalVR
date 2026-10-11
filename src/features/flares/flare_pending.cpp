#include "features/flares/flare_pending.hpp"

#include <cstddef>

namespace evr::flares {

PendingCleared clearPending(std::span<std::uint32_t> words) {
    PendingCleared out;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::uint32_t was = std::atomic_ref<std::uint32_t>(words[i]).exchange(0);
        if (was == 0) {
            continue;
        }
        if (out.words == 0) {
            out.firstWord = static_cast<std::int32_t>(i);
            out.firstValue = was;
        }
        ++out.words;
    }
    return out;
}

void ViewMarks::reset() {
    for (auto& view : marks_) {
        for (auto& word : view) {
            word.store(0, std::memory_order_relaxed);
        }
    }
}

void ViewMarks::clear(int view) {
    if (view < 0 || view > 1) {
        return;
    }
    for (auto& word : marks_[static_cast<std::size_t>(view)]) {
        word.store(0, std::memory_order_relaxed);
    }
}

std::uint32_t ViewMarks::noted(int view, std::int64_t index) const {
    if (view < 0 || view > 1 || index < 0 || index >= kPendingWords) {
        return 0;
    }
    return marks_[static_cast<std::size_t>(view)][static_cast<std::size_t>(index)].load(
        std::memory_order_acquire);
}

void ViewMarks::mark(int view, std::int64_t query) {
    if (view < 0 || view > 1 || query < 0 || query >= kPoolQueries) {
        return;
    }
    const auto word = static_cast<std::size_t>(query / 32);
    const std::uint32_t bit = 1u << static_cast<unsigned>(query % 32);
    marks_[static_cast<std::size_t>(view)][word].fetch_or(bit, std::memory_order_release);
}

void ViewMarks::note(int view, std::span<const std::uint32_t> words, std::int32_t firstWord) {
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::int64_t word = std::int64_t{firstWord} + static_cast<std::int64_t>(i);
        if (view < 0 || view > 1 || word < 0 || word >= kPendingWords || words[i] == 0) {
            continue;
        }
        marks_[static_cast<std::size_t>(view)][static_cast<std::size_t>(word)].fetch_or(
            words[i], std::memory_order_release);
    }
}

std::int32_t ViewMarks::firstUnmarked(int view,
                                      std::span<const std::uint32_t> pending,
                                      std::int32_t firstWord,
                                      std::uint32_t& bits) const {
    bits = 0;
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const std::int64_t word = std::int64_t{firstWord} + static_cast<std::int64_t>(i);
        const std::uint32_t marked = noted(view, word);
        if ((pending[i] & ~marked) != 0) {
            bits = pending[i] & ~marked;
            return static_cast<std::int32_t>(word);
        }
    }
    return -1;
}

bool ViewMarks::covers(int view, std::span<const std::uint32_t> pending, std::int32_t firstWord) const {
    std::uint32_t bits = 0;
    return (view == 0 || view == 1) && firstWord >= 0 &&
           std::int64_t{firstWord} + static_cast<std::int64_t>(pending.size()) <= kPendingWords &&
           firstUnmarked(view, pending, firstWord, bits) < 0;
}

std::int32_t
firstWaitedUnissued(const ViewMarks& waited, const ViewMarks& issued, int view, std::uint32_t& bits) {
    bits = 0;
    for (std::int32_t word = 0; word < kPendingWords; ++word) {
        const std::uint32_t missing = waited.noted(view, word) & ~issued.noted(view, word);
        if (missing != 0) {
            bits = missing;
            return word;
        }
    }
    return -1;
}

bool setStaleTestBit(std::span<std::uint32_t> words, std::uint64_t splitFrame) {
    constexpr auto kWord = static_cast<std::size_t>(kStaleTestQuery / 32);
    if (splitFrame % kStaleTestPeriod != 0 || words.size() <= kWord) {
        return false;
    }
    const std::uint32_t bit = 1u << static_cast<unsigned>(kStaleTestQuery % 32);
    std::atomic_ref<std::uint32_t>(words[kWord]).fetch_or(bit);
    return true;
}

bool noteTripTest(ViewMarks& waited, int view, std::uint64_t viewOneWaits) {
    if (view != 1 || viewOneWaits != kTripTestCopy) {
        return false;
    }
    waited.mark(1, kStaleTestQuery);
    return true;
}

} // namespace evr::flares
