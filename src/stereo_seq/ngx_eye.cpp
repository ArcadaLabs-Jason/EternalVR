#include "stereo_seq/ngx_eye.hpp"

namespace evr::stereo_seq {

void NgxOutputBook::note(std::uint64_t image, const RenderTag& tag) {
    if (image == 0) {
        return;
    }
    Entry* slot = &entries_[0];
    for (Entry& e : entries_) {
        if (e.order != 0 && e.image == image) {
            slot = &e;
            break;
        }
        if (e.order < slot->order) {
            slot = &e; // a free entry (0) or the oldest
        }
    }
    *slot = Entry{image, ++notes_, tag};
}

std::optional<RenderTag> NgxOutputBook::find(std::uint64_t image) const {
    if (image == 0) {
        return std::nullopt;
    }
    for (const Entry& e : entries_) {
        if (e.order != 0 && e.image == image) {
            return e.tag;
        }
    }
    return std::nullopt;
}

NgxEyePick pickNgxEye(const std::optional<RenderTag>& own, const std::optional<RenderTag>& inFlight) {
    NgxEyePick pick;
    // Counters wrap: the distance in presents, in-flight minus own, is 0 or 1 for the render's own tag.
    const bool fresh = own && (!inFlight || inFlight->backendFrame - own->backendFrame <= 1u);
    if (fresh) {
        pick.tag = own;
        pick.own = true;
        const Eye other = inFlight ? inFlight->eye : Eye::Mono;
        pick.inFlightDiffers = eyeIndex(other) != eyeIndex(own->eye);
        return pick;
    }
    pick.tag = inFlight;
    pick.fallback = inFlight && inFlight->eye != Eye::Mono;
    return pick;
}

void NgxResetBook::note(Eye eye, std::uint64_t gameFrame) {
    if (gameFrame == 0) {
        return;
    }
    for (const Entry& e : entries_) {
        if (e.eye == eye && e.gameFrame == gameFrame) {
            return;
        }
    }
    entries_[next_] = Entry{eye, gameFrame};
    next_ = (next_ + 1) % kCapacity;
}

bool NgxResetBook::take(Eye eye, std::uint64_t gameFrame) {
    if (gameFrame == 0) {
        return false;
    }
    for (Entry& e : entries_) {
        if (e.eye == eye && e.gameFrame == gameFrame) {
            e = Entry{};
            return true;
        }
    }
    return false;
}

} // namespace evr::stereo_seq
