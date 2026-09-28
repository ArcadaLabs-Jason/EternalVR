#include "stereo_seq/stack_budget.hpp"

#include <algorithm>

namespace evr::stereo_seq {

bool stackKnown(const StackPosition& s) {
    return s.low < s.high && s.sp > s.low && s.sp < s.high;
}

std::size_t stackHeadroom(const StackPosition& s) {
    return stackKnown(s) ? static_cast<std::size_t>(s.sp - s.low) : 0;
}

bool nestedRenderFits(const StackPosition& s, std::size_t deepestNested) {
    if (!stackKnown(s)) {
        return true;
    }
    const std::size_t chain = std::max(deepestNested, kNestedStackFirstGuess);
    return stackHeadroom(s) >= chain + kNestedStackMargin;
}

std::size_t nestedDepth(std::uintptr_t outerSp, std::uintptr_t innerSp) {
    return innerSp < outerSp ? static_cast<std::size_t>(outerSp - innerSp) : 0;
}

} // namespace evr::stereo_seq
