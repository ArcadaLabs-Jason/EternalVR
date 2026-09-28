#include "stereo_seq/render_idle.hpp"

namespace evr::stereo_seq {

RenderIdle::Verdict RenderIdle::check(std::uint32_t backend, std::uint64_t quietMs) const {
    if (reference_) {
        // Each kicked frame raises the counter once when it presents.
        const auto expected = static_cast<std::uint32_t>(referenceBackend_ + (kicks_ - referenceKicks_));
        const auto ahead = static_cast<std::int32_t>(backend - expected);
        if (ahead == 0) {
            return Verdict::Idle;
        }
        if (ahead < 0) {
            // Kicked frames have not presented yet: never a base, however long the counter stands still.
            return Verdict::Wait;
        }
        // More backend frames than kicks: frames came from outside the counted chains, the count tells
        // nothing any more.
    }
    return quietMs >= kUnverifiedIdleQuietMs ? Verdict::IdleUnverified : Verdict::Wait;
}

void RenderIdle::based(std::uint32_t backend) {
    reference_ = true;
    referenceBackend_ = backend;
    referenceKicks_ = kicks_;
}

FrameEndPlan planFrameEnd(const FrameEndInput& in) {
    FrameEndPlan plan;
    if (!in.presents) {
        return plan; // no backend frame, no present: nothing to tag or pair
    }
    const bool needBase = !in.synced || in.rebaseDue;
    plan.stereo = in.leftApplied;
    plan.drain = needBase && (in.leftApplied || (in.wanted && in.drainAllowed));
    return plan;
}

} // namespace evr::stereo_seq
