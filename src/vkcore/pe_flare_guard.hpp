#pragma once

// Lens flares per view under Parallel Eye Rendering (pe_flares.hpp): the guard on the occlusion copies' wait
// (src/features/flares/flare_pending.hpp). A copy waits only when every pending bit it scans is one its own
// view's surface setup marked since view 0's latch and since that view's last copy; the queries each view's
// occlusion pass begins are checked against what its copies waited on at view 0's next latch (the pass is
// recorded after the copy in all but 1 of about 14,000 copies, rig run pf4-a-pon), and one the pass did not
// begin stops the waits for the session. View 0's latch clears the pool's pending bits. Counted and logged as
// "pe-flares".

#include <cstdint>

namespace evr::vkcore::pe_flare_guard {

// From the install, the game's image at `base` (its pending bits at pe_flare_sites::kPendingBits). Reads
// ETERNALVR_TEST_PE_FLARES_STALE (1: a pending bit nobody marked; 2: a waited query nobody issued).
void install(std::uintptr_t base);

// Query `query` marked by view `view`'s surface setup, or begun by its occlusion pass (view -1: neither).
void marked(int view, std::int64_t query);
void issued(int view, std::int64_t query);

// View `view`'s copy of `words` pending words from `first` may wait (`allowed`: no test knob against it).
// Its view's marks are used up either way.
bool copyMayWait(int view, std::int32_t first, std::int32_t words, bool allowed);

// View 0's latch, under the latches' mutex: the last frame's waits checked against its issues, the pending
// bits cleared when `clear`, the rig's test bit set in a frame split by view (`perView`), the marks reset.
void startFrame(bool clear, bool perView);

// The counts since the last call (the 10 s line).
struct Counts {
    std::uint64_t emptyWaits = 0;  // copies that may wait and found no query pending
    std::uint64_t unmarked = 0;    // copies that found a bit their view did not mark
    std::uint64_t unissued = 0;    // views' frames whose copy waited on a query their pass did not begin
    std::uint64_t staleWords = 0;  // pending words view 0's latch found set
    std::uint64_t staleFrames = 0; // in so many frames
    std::uint64_t testBits = 0;    // bits ETERNALVR_TEST_PE_FLARES_STALE=1 set
    bool testOn = false;           // ETERNALVR_TEST_PE_FLARES_STALE=1
    bool tripped = false;          // the copies no longer wait (this session)
};
Counts takeCounts();

} // namespace evr::vkcore::pe_flare_guard
