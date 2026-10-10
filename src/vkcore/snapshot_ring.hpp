#pragma once

// Parallel Eye Rendering's eye snapshots (view_snapshot.hpp): which slot a frame's copies go into and which
// one a present reads, for the pairs (the default) and for the guess (ETERNALVR_TEST_PE_PAIRING=guess). Pure
// (no Vulkan), unit tested (tests/vkcore/snapshot_ring_tests.cpp, snapshot_pairs_tests.cpp).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace evr::vkcore::snapshot_ring {

inline constexpr std::size_t kSlots = 4;

struct Slot {
    std::uint64_t seq = 0;   // the snapshot's number (1, 2, ...); 0: never written
    std::uint64_t tag = 0;   // the semaphore the game's submit of that frame signals (a present waits on it)
    std::uint64_t image = 0; // the swapchain image view 0 of that frame drew (0: not known)
    bool submitted = false;  // its copy is recorded and goes to a queue with the signal of `seq` after it
    std::uint64_t readValue = 0; // the presenter timeline value of the last copy that read it; 0: none
    std::uint64_t view = 0;      // the head-tracked view record its frame was rendered with; 0: not known
    std::uint64_t frame = 0;     // its frame (view 0 screen passes counted, view_snapshot_impl.hpp)
};

using Slots = std::array<Slot, kSlots>;

// The slot for the next snapshot: the oldest one whose own copy is done (`snapDone`: the snapshot timeline's
// completed value) and whose last read is done (`readDone`: the presenter timeline's); kSlots: none free.
inline std::size_t pickWrite(const Slots& slots, std::uint64_t snapDone, std::uint64_t readDone) {
    std::size_t best = kSlots;
    for (std::size_t i = 0; i < kSlots; ++i) {
        const Slot& s = slots[i];
        const bool free = s.seq == 0 || ((!s.submitted || s.seq <= snapDone) && s.readValue <= readDone);
        if (free && (best == kSlots || s.seq < slots[best].seq)) {
            best = i;
        }
    }
    return best;
}

enum class Miss : std::uint8_t { None, Repeat, NoCopy };

struct Read {
    std::size_t index = kSlots;
    Miss miss = Miss::NoCopy;
};

// The slot whose tag a present waits on (`tag`). The game reuses its semaphores, so several copies can carry
// one tag: the oldest one newer than the last one read (`lastRead`) is the present's (None). Without one, the
// last copy again when it carries the tag (Repeat); never an older one. NoCopy: neither.
inline Read pickRead(const Slots& slots, std::uint64_t tag, std::uint64_t lastRead) {
    Read r;
    for (std::size_t i = 0; i < kSlots; ++i) {
        const Slot& s = slots[i];
        if (s.seq == 0 || !s.submitted || s.tag != tag || s.seq < lastRead) {
            continue;
        }
        if (s.seq == lastRead) {
            if (r.index == kSlots) {
                r = Read{i, Miss::Repeat};
            }
            continue;
        }
        if (r.miss != Miss::None || s.seq < slots[r.index].seq) {
            r = Read{i, Miss::None};
        }
    }
    return r;
}

// The snapshot before a matched one (`matched`, from pickRead), as a Repeat when the last present read it.
// Without that snapshot (or not submitted, or older than the last read), the matched one.
inline Read previousOf(const Slots& slots, Read matched, std::uint64_t lastRead) {
    if (matched.index >= kSlots || matched.miss != Miss::None) {
        return matched;
    }
    const std::uint64_t want = slots[matched.index].seq - 1;
    if (want < lastRead) {
        return matched;
    }
    for (std::size_t i = 0; i < kSlots; ++i) {
        if (slots[i].seq == want && slots[i].submitted) {
            return Read{i, want == lastRead ? Miss::Repeat : Miss::None};
        }
    }
    return matched;
}

enum class Drew : std::uint8_t { NotKnown = 0, Matched = 1, Before = 2, Both = 3 };

// Which frame drew the presented swapchain image `image`: the matched snapshot's frame, the one before it,
// both (the same image) or neither known (`image` 0, an image neither drew, no match, or the matched frame
// drew it but the frame before's image is not known: it may have drawn it too).
inline Drew whoDrew(const Slots& slots, Read matched, std::uint64_t image) {
    if (matched.index >= kSlots || image == 0) {
        return Drew::NotKnown;
    }
    const std::uint64_t seq = slots[matched.index].seq;
    std::uint8_t drew = slots[matched.index].image == image ? 1 : 0;
    bool beforeKnown = false;
    for (const Slot& s : slots) {
        if (s.seq != 0 && s.seq + 1 == seq && s.image != 0) {
            beforeKnown = true;
            drew |= s.image == image ? 2 : 0;
        }
    }
    return drew == 1 && !beforeKnown ? Drew::NotKnown : static_cast<Drew>(drew);
}

// The snapshot a present shows. The game usually presents the image its previous frame drew (measured: it
// draws one swapchain image and presents the other), so eye 0 is the frame before the one whose submit the
// present waits on and eye 1 is that frame's snapshot too (previousOf). After a hitch it can present the
// matched frame's own image (measured: whole capture bursts a frame apart): when only the matched frame drew
// the presented image, the matched snapshot. Taking the matched one without that evidence was measured to
// lock the eyes a frame apart. A/B runs: `Shown::Before` always the one before; `Shown::BothMatched` also the
// matched one when both frames drew the presented image.
enum class Shown : std::uint8_t { ByImage, Before, BothMatched };

inline Read pickShown(const Slots& slots,
                      Read matched,
                      std::uint64_t image,
                      std::uint64_t lastRead,
                      Shown mode = Shown::ByImage) {
    const Drew drew = mode == Shown::Before ? Drew::NotKnown : whoDrew(slots, matched, image);
    if (drew == Drew::Matched || (mode == Shown::BothMatched && drew == Drew::Both)) {
        return matched;
    }
    return previousOf(slots, matched, lastRead);
}

// A present with no copy of its own (NoCopy: no semaphore it waits on tags one, or only copies older than the
// last one read, e.g. right after a present that took its matched copy): the last copy again (a Repeat). Eye
// 1 from view 1's image instead would show the newest frame; an older copy would step eye 1 back.
inline Read orLast(const Slots& slots, Read r, std::uint64_t lastRead) {
    if (r.index < kSlots || lastRead == 0) {
        return r;
    }
    for (std::size_t i = 0; i < kSlots; ++i) {
        if (slots[i].seq == lastRead && slots[i].submitted) {
            return Read{i, Miss::Repeat};
        }
    }
    return r;
}

// After a swapchain recreate. View 0 draws into the image the frame before acquired, and nothing else draws a
// swapchain image (view 1 draws its clone), so the first presents of a new swapchain can show an image no
// frame has drawn: eye 0 black or garbage next to an older eye 1. Armed for `presents` presents: a present of
// an image no view 0 pass drew since the arm is held (shown as a repeat: the headset keeps the last pair);
// the first present of a drawn one ends it, and so does the end of the count, so it never holds for longer.
class NewImageHold {
public:
    void arm(int presents) {
        left_ = presents;
        drawn_ = {};
        count_ = 0;
    }

    // A view 0 pass drew `image` (0: not known).
    void drew(std::uint64_t image) {
        if (left_ > 0 && image != 0 && count_ < drawn_.size() && !wasDrawn(image)) {
            drawn_[count_++] = image;
        }
    }

    // A present of `image`: true when it is held.
    bool holds(std::uint64_t image) {
        if (left_ <= 0) {
            return false;
        }
        if (image != 0 && wasDrawn(image)) {
            left_ = 0;
            return false;
        }
        --left_;
        return true;
    }

    bool active() const { return left_ > 0; }

private:
    bool wasDrawn(std::uint64_t image) const {
        for (std::size_t i = 0; i < count_; ++i) {
            if (drawn_[i] == image) {
                return true;
            }
        }
        return false;
    }

    int left_ = 0;
    std::array<std::uint64_t, 8> drawn_{};
    std::size_t count_ = 0;
};

// ---- Pairs ------------------------------------------------------------------------------------------------
// Each eye of a frame is copied by a batch after the submit that carries its view's screen pass: view 0's
// swapchain image, view 1's image. A frame's two copies share a slot, and a present shows the newest pair
// both of whose copies were submitted: its eyes are of one frame by construction.

inline constexpr std::size_t kPairSlots = 4; // at most (ETERNALVR_TEST_PE_PAIR_SLOTS)
// A slot's readValue while a present that picked it is still recording its copy.
inline constexpr std::uint64_t kPairReading = UINT64_MAX;

struct PairSlot {
    std::uint64_t frame = 0;            // the frame its copies are of (0: never written)
    std::uint64_t view = 0;             // that frame's head-tracked view record (0: not known)
    std::array<std::uint64_t, 2> seq{}; // each eye's copy: its value on that eye's timeline (0: none yet)
    std::array<bool, 2> submitted{};    // ... the submit carrying it returned (false while it is made)
    std::uint64_t readValue = 0; // the presenter timeline value of the last copy that read it (0: none)
    // What a new frame's copies wait for on the GPU before they write the slot's images: the slot's last
    // copies (each eye's timeline) and its last read (the presenter's).
    std::array<std::uint64_t, 2> prior{};
    std::uint64_t priorRead = 0;
};
using PairSlots = std::array<PairSlot, kPairSlots>;

inline bool complete(const PairSlot& s) {
    return s.frame != 0 && s.seq[0] != 0 && s.seq[1] != 0 && s.submitted[0] && s.submitted[1];
}

// The slot, among the first `count`, for eye `eye`'s copy of frame `frame`: the one holding the frame's other
// copy, else the one with the oldest frame (a pair never shown is written over like any other), but never one
// a present is recording a read of, one whose copy is still being submitted, or one with a newer frame than
// `frame` (a late copy; kPairSlots: none). A frame's eye copied twice: none. Nor the pair of frame `keep`
// (0: none), the one shown last under ETERNALVR_TEST_PE_EYE1_LAG=1, whose eye 1 the next new pair shows.
inline std::size_t
pairSlotFor(const PairSlots& slots, std::size_t count, std::uint64_t frame, int eye, std::uint64_t keep = 0) {
    count = std::min(count, kPairSlots);
    std::size_t oldest = kPairSlots;
    for (std::size_t i = 0; i < count; ++i) {
        const PairSlot& s = slots[i];
        if (s.frame == frame && frame != 0) {
            return s.seq[static_cast<std::size_t>(eye)] == 0 ? i : kPairSlots;
        }
        const bool busy = s.readValue == kPairReading || (s.seq[0] != 0 && !s.submitted[0]) ||
                          (s.seq[1] != 0 && !s.submitted[1]) || (keep != 0 && s.frame == keep);
        if (!busy && (oldest == kPairSlots || s.frame < slots[oldest].frame)) {
            oldest = i;
        }
    }
    return oldest < kPairSlots && slots[oldest].frame < frame ? oldest : kPairSlots;
}

// The slot `s` takes frame `frame` (view record `view`): what it held becomes what the new copies wait for.
inline void claim(PairSlot& s, std::uint64_t frame, std::uint64_t view) {
    for (std::size_t e = 0; e < 2; ++e) {
        s.prior[e] = std::max(s.prior[e], s.seq[e]);
        s.seq[e] = 0;
        s.submitted[e] = false;
    }
    s.priorRead = std::max(s.priorRead, s.readValue);
    s.readValue = 0;
    s.frame = frame;
    s.view = view;
}

enum class PairShow : std::uint8_t {
    New,    // a complete pair newer than the last one shown
    Kept,   // none: the headset keeps the last pair (the present is not handed over)
    NoPair, // none for a while, or view 1 not rendered: the eyes as without pairs (the presented image)
};

struct PairPick {
    std::size_t index = kPairSlots;
    PairShow show = PairShow::NoPair;
    std::uint64_t skipped = 0; // complete pairs between the last one shown and this one: never shown
};

// The pair a present shows: the newest complete pair among the first `count` newer than the last one shown
// (`lastShown`, a frame; 0: none yet), skipping older complete ones. Without one, the headset keeps the last
// pair while fewer than `keepLimit` presents in a row (`kept`) have; after that, or with no pair shown yet,
// NoPair. NoPair too when the frames sent did not render view 1 (`view1Rendered`, view_frames.hpp: a
// loading screen, new clones), as the eye copy has it.
inline PairPick pickPair(const PairSlots& slots,
                         std::size_t count,
                         std::uint64_t lastShown,
                         bool view1Rendered,
                         std::uint32_t kept,
                         std::uint32_t keepLimit) {
    PairPick p;
    if (!view1Rendered) {
        return p;
    }
    count = std::min(count, kPairSlots);
    for (std::size_t i = 0; i < count; ++i) {
        const PairSlot& s = slots[i];
        if (complete(s) && s.frame > lastShown && s.readValue != kPairReading &&
            (p.index == kPairSlots || s.frame > slots[p.index].frame)) {
            p.index = i;
        }
    }
    if (p.index < kPairSlots) {
        p.show = PairShow::New;
        for (std::size_t i = 0; i < count; ++i) {
            p.skipped += complete(slots[i]) && slots[i].frame > lastShown && i != p.index ? 1 : 0;
        }
    } else if (lastShown != 0 && kept < keepLimit) {
        p.show = PairShow::Kept;
    }
    return p;
}

// ETERNALVR_TEST_PE_EYE1_LAG=1 (a rig control for the eye sync check): a new pair shows eye 1 of the pair
// shown before it (frame `lastShown`), so eye 1 is a frame late; pairSlotFor keeps that pair (its `keep`)
// until a newer one is read. Its slot among the first `count`; kPairSlots: none (no pair shown yet, or the
// pairs started again), and the new pair shows its own eye 1.
inline std::size_t shownBefore(const PairSlots& slots, std::size_t count, std::uint64_t lastShown) {
    count = std::min(count, kPairSlots);
    for (std::size_t i = 0; i < count && lastShown != 0; ++i) {
        if (slots[i].frame == lastShown && complete(slots[i])) {
            return i;
        }
    }
    return kPairSlots;
}

} // namespace evr::vkcore::snapshot_ring
