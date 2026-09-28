#include "stereo_seq/eye_tags.hpp"

namespace evr::stereo_seq {

namespace {

// Signed distance a - b of two wrapping 32-bit counters.
std::int32_t distance(std::uint32_t a, std::uint32_t b) {
    return static_cast<std::int32_t>(a - b);
}

} // namespace

const char* eyeName(Eye eye) {
    switch (eye) {
    case Eye::Left:
        return "left";
    case Eye::Right:
        return "right";
    case Eye::Mono:
        break;
    }
    return "mono";
}

const char* desyncReasonName(DesyncReason reason) {
    switch (reason) {
    case DesyncReason::MissingPresent:
        return "a tagged frame was never presented";
    case DesyncReason::UntaggedFrame:
        return "a frame was presented that no tag was queued for";
    case DesyncReason::Overflow:
        return "too many tags queued";
    case DesyncReason::Requested:
        return "fresh base requested";
    case DesyncReason::None:
        break;
    }
    return "none";
}

void EyeTagQueue::rebase(std::uint32_t idleBackendFrame) {
    queue_.clear();
    synced_ = true;
    base_ = idleBackendFrame;
    nextBackend_ = idleBackendFrame + 1;
    lastPresent_ = idleBackendFrame;
    ++stats_.rebases;
}

void EyeTagQueue::desync(DesyncReason reason) {
    if (!synced_) {
        return;
    }
    queue_.clear();
    synced_ = false;
    lastDesync_ = reason;
    switch (reason) {
    case DesyncReason::MissingPresent:
        ++stats_.missingPresent;
        break;
    case DesyncReason::UntaggedFrame:
        ++stats_.untaggedFrame;
        break;
    case DesyncReason::Overflow:
        ++stats_.overflow;
        break;
    case DesyncReason::Requested:
        ++stats_.requested;
        break;
    case DesyncReason::None:
        break;
    }
}

bool EyeTagQueue::push(RenderTag tag) {
    if (!synced_) {
        return false;
    }
    if (queue_.size() >= capacity_) {
        desync(DesyncReason::Overflow);
        return false;
    }
    tag.backendFrame = nextBackend_++;
    tag.eyeSeq = eyeSeq_[eyeIndex(tag.eye)]++;
    queue_.push_back(tag);
    ++stats_.pushed;
    return true;
}

const RenderTag* EyeTagQueue::peek(std::uint32_t backendFrame) const {
    if (!synced_) {
        return nullptr;
    }
    for (const RenderTag& t : queue_) {
        if (t.backendFrame == backendFrame) {
            return &t;
        }
    }
    return nullptr;
}

PresentMatch EyeTagQueue::pop(std::uint32_t backendFrame) {
    PresentMatch match;
    if (!synced_ || distance(backendFrame, base_) <= 0) {
        // Out of sync, or a frame that was in flight before the base: no tag.
        ++stats_.untagged;
        return match;
    }
    if (backendFrame == lastPresent_) {
        // A second present of the same backend frame: not a render frame of its own.
        ++stats_.untagged;
        return match;
    }
    lastPresent_ = backendFrame;
    if (queue_.empty()) {
        if (distance(backendFrame, nextBackend_) >= 0) {
            desync(DesyncReason::UntaggedFrame);
        }
        ++stats_.untagged;
        return match;
    }
    const std::int32_t ahead = distance(queue_.front().backendFrame, backendFrame);
    if (ahead == 0) {
        match.tagged = true;
        match.tag = queue_.front();
        queue_.pop_front();
        ++stats_.matched;
        return match;
    }
    desync(ahead < 0 ? DesyncReason::MissingPresent : DesyncReason::UntaggedFrame);
    ++stats_.untagged;
    return match;
}

} // namespace evr::stereo_seq
