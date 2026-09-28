#include "stereo_seq/prev_matrices.hpp"

#include <cstring>
#include <utility>

namespace evr::stereo_seq {

std::vector<ByteRange> previousMatrixRanges() {
    // Destinations of the stores in RVA 0x1CE2340 (docs/rig-findings/stereo-routes.md section 2.4).
    return {
        {0x29480, 0x40}, // previous view-projection
        {0x29500, 0xB0}, // previous origin, axis, view matrix and view-projection (r_lockView 0)
        {0x295F0, 0x40}, // previous custom view-projection (hands and guns)
        {0x29670, 0x40}, // previous custom view-projection 2
        {0x296F0, 0x40}, // previous centred view-projection
        {0x297B0, 0x40},
        {0x29830, 0x64}, // two matrices' worth of fields, then the previous origin and offsets
        {0x298C0, 0x08},
        {0x298E0, 0x08}, // previous render size
    };
}

PrevMatrixBook::PrevMatrixBook(std::vector<ByteRange> ranges, std::size_t maxViews)
    : ranges_(std::move(ranges)), maxViews_(maxViews == 0 ? 1 : maxViews) {
    for (const ByteRange& r : ranges_) {
        bytes_ += r.size;
    }
}

void PrevMatrixBook::clear() {
    entries_.clear();
}

PrevMatrixBook::Entry& PrevMatrixBook::entryFor(const std::byte* view) {
    ++useCounter_;
    for (Entry& e : entries_) {
        if (e.view == view) {
            e.lastUse = useCounter_;
            return e;
        }
    }
    if (entries_.size() < maxViews_) {
        entries_.emplace_back();
        Entry& e = entries_.back();
        e.view = view;
        e.lastUse = useCounter_;
        return e;
    }
    // Reuse the entry used least recently.
    Entry* oldest = &entries_.front();
    for (Entry& e : entries_) {
        if (e.lastUse < oldest->lastUse) {
            oldest = &e;
        }
    }
    *oldest = Entry{};
    oldest->view = view;
    oldest->lastUse = useCounter_;
    return *oldest;
}

void PrevMatrixBook::save(const std::byte* view, std::vector<std::byte>& into) const {
    into.resize(bytes_);
    std::size_t at = 0;
    for (const ByteRange& r : ranges_) {
        std::memcpy(into.data() + at, view + r.offset, r.size);
        at += r.size;
    }
}

void PrevMatrixBook::restore(std::byte* view, const std::vector<std::byte>& from) const {
    if (from.size() != bytes_) {
        return;
    }
    std::size_t at = 0;
    for (const ByteRange& r : ranges_) {
        std::memcpy(view + r.offset, from.data() + at, r.size);
        at += r.size;
    }
}

bool PrevMatrixBook::afterStore(std::byte* view, Eye eye, std::uint32_t renderFrame) {
    Entry& e = entryFor(view);
    ++stats_.stores;
    // The store of the render frame right before this one, for this view.
    const bool follows = e.stored && static_cast<std::uint32_t>(renderFrame - e.lastFrame) == 1;
    bool rewrote = false;
    if (eye == Eye::Right) {
        // Stored from eye L's latch of this tick (when eye L's frame came right before): eye L's previous
        // matrices for the next tick.
        const bool afterLeft = follows && e.lastEye == Eye::Left;
        save(view, e.saved[0]);
        e.valid[0] = afterLeft;
        if (afterLeft && e.valid[1]) {
            restore(view, e.saved[1]);
            rewrote = true;
        }
        e.valid[1] = false;
    } else {
        if (follows && e.lastEye == Eye::Right) {
            // Stored from eye R's latch of the last tick: eye R's previous matrices for this tick.
            save(view, e.saved[1]);
            e.valid[1] = true;
            if (e.valid[0]) {
                restore(view, e.saved[0]);
                rewrote = true;
            }
        } else {
            e.valid[1] = false; // a mono frame came between: nothing kept is current
        }
        e.valid[0] = false;
    }
    e.stored = true;
    e.lastFrame = renderFrame;
    e.lastEye = eye == Eye::Right ? Eye::Right : Eye::Left;
    if (rewrote) {
        ++stats_.rewrites;
    } else {
        ++stats_.kept;
    }
    return rewrote;
}

} // namespace evr::stereo_seq
