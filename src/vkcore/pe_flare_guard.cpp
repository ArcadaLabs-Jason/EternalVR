#include "vkcore/pe_flare_guard.hpp"

#include "features/flares/flare_pending.hpp"
#include "vkcore/log.hpp"
#include "vkcore/pe_flare_sites.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <span>
#include <string>

namespace evr::vkcore::pe_flare_guard {

namespace {

constexpr const char* kTag = "pe-flares";
constexpr int kMaxLogged = 5;

std::uintptr_t g_base = 0;
bool g_testStale = false; // ETERNALVR_TEST_PE_FLARES_STALE=1: a pending bit nobody marked (flare_pending.hpp)
bool g_testTrip = false; // ETERNALVR_TEST_PE_FLARES_STALE=2: a waited query nobody issued (flare_pending.hpp)
std::atomic<std::uint64_t> g_viewOneWaits{0}; // view 1's copies that waited (the trip test's clock)

flares::ViewMarks g_marks;    // reset at view 0's latch, marked by the surface setups, used up by the copies
flares::ViewMarks g_issued;   // the queries the occlusion passes begin, checked at view 0's next latch
flares::ViewMarks g_waitedOn; // the bits the copies that waited scanned, checked against g_issued
std::atomic<bool> g_tripped{false}; // a copy waited on a query its view's pass did not begin: no more waits
std::uint64_t g_splitFrames = 0;    // under the latches' mutex: frames split by view since the install

struct Counters {
    std::atomic<std::uint64_t> emptyWaits{0};
    std::atomic<std::uint64_t> unmarked{0};
    std::atomic<std::uint64_t> unissued{0};
    std::atomic<std::uint64_t> staleFrames{0};
    std::atomic<std::uint64_t> staleWords{0};
    std::atomic<std::uint64_t> testBits{0};
} g_counts;

std::atomic<int> g_loggedStale{0};
std::atomic<int> g_loggedUnmarked{0};
std::atomic<int> g_loggedUnissued{0};

void add(std::atomic<std::uint64_t>& counter, std::uint64_t n = 1) {
    counter.fetch_add(n, std::memory_order_relaxed);
}

std::span<std::uint32_t> pendingWords() {
    return {reinterpret_cast<std::uint32_t*>(g_base + pe_flare_sites::kPendingBits),
            static_cast<std::size_t>(flares::kPendingWords)};
}

// Every bit set in the scanned words is one view `view`'s setup marked since view 0's latch and its last
// copy; the bits are noted for the check against its pass's issues (a scan with none set is counted). Else
// counted and logged.
bool marksCover(int view, std::int32_t first, std::int32_t words) {
    std::array<std::uint32_t, flares::kPendingWords> snapshot{};
    if (first < 0 || words < 0 || first + words > flares::kPendingWords) {
        return false;
    }
    const std::span<std::uint32_t> bits = pendingWords();
    for (std::int32_t i = 0; i < words; ++i) {
        snapshot[static_cast<std::size_t>(i)] =
            std::atomic_ref<std::uint32_t>(bits[static_cast<std::size_t>(first + i)]).load();
    }
    const std::span<const std::uint32_t> scanned(snapshot.data(), static_cast<std::size_t>(words));
    std::uint32_t unmarked = 0;
    const std::int32_t word = g_marks.firstUnmarked(view, scanned, first, unmarked);
    if (word < 0) {
        g_waitedOn.note(view, scanned, first);
        bool any = false;
        for (const std::uint32_t w : scanned) {
            any = any || w != 0;
        }
        if (!any) {
            add(g_counts.emptyWaits);
        }
        return true;
    }
    add(g_counts.unmarked);
    if (g_loggedUnmarked.fetch_add(1) < kMaxLogged) {
        EVR_LOG(
            "%s: view %d's copy found pending queries its surface setup did not mark this frame (word %d, "
            "bits 0x%08X); copied without the wait",
            kTag, view, word, unmarked);
    }
    return false;
}

// The last frame's waited copies against their views' occlusion passes.
void checkWaits() {
    for (int view = 0; view < 2; ++view) {
        std::uint32_t bits = 0;
        const std::int32_t word = flares::firstWaitedUnissued(g_waitedOn, g_issued, view, bits);
        if (word < 0) {
            continue;
        }
        add(g_counts.unissued);
        if (g_loggedUnissued.fetch_add(1) < kMaxLogged) {
            EVR_LOG(
                "%s: view %d's copy waited on queries its occlusion pass did not issue in that frame (word "
                "%d, bits 0x%08X)",
                kTag, view, word, bits);
        }
        if (!g_tripped.exchange(true)) {
            EVR_LOG("%s: the occlusion copies no longer wait (this session)", kTag);
        }
    }
    g_waitedOn.reset();
    g_issued.reset();
}

// The pool's pending bits, cleared with atomic stores: none is left from an earlier frame for a copy of this
// one to wait on. The words found set are counted and the first few logged.
void clearPendingBits() {
    const flares::PendingCleared found = flares::clearPending(pendingWords());
    if (found.words == 0) {
        return;
    }
    add(g_counts.staleFrames);
    add(g_counts.staleWords, static_cast<std::uint64_t>(found.words));
    if (g_loggedStale.fetch_add(1) < kMaxLogged) {
        EVR_LOG("%s: view 0's latch found %d pending word(s) left set (word %d, bits 0x%08X); cleared", kTag,
                found.words, found.firstWord, found.firstValue);
    }
}

} // namespace

void install(std::uintptr_t base) {
    g_base = base;
    std::wstring value;
    if (!readEnv(L"ETERNALVR_TEST_PE_FLARES_STALE", value)) {
        return;
    }
    if (value == L"1") {
        g_testStale = true;
        EVR_LOG("%s: test: one frame split by view in %llu gets pending query 0x%llX set with no mark and no "
                "issue, after the clear (ETERNALVR_TEST_PE_FLARES_STALE=1)",
                kTag, static_cast<unsigned long long>(flares::kStaleTestPeriod),
                static_cast<unsigned long long>(flares::kStaleTestQuery));
    } else if (value == L"2") {
        g_testTrip = true;
        EVR_LOG(
            "%s: test: view 1's copy number %llu that waits notes query 0x%llX as waited on without waiting "
            "on it; no pass issues it (ETERNALVR_TEST_PE_FLARES_STALE=2)",
            kTag, static_cast<unsigned long long>(flares::kTripTestCopy),
            static_cast<unsigned long long>(flares::kStaleTestQuery));
    }
}

void marked(int view, std::int64_t query) {
    g_marks.mark(view, query);
}

void issued(int view, std::int64_t query) {
    g_issued.mark(view, query);
}

bool copyMayWait(int view, std::int32_t first, std::int32_t words, bool allowed) {
    const bool wait = allowed && !g_tripped.load(std::memory_order_acquire) && marksCover(view, first, words);
    g_marks.clear(view); // used up: what this view's next copy may wait on is marked after this one
    if (wait && g_testTrip && view == 1 &&
        flares::noteTripTest(g_waitedOn, view, g_viewOneWaits.fetch_add(1) + 1)) {
        EVR_LOG("%s: test: query 0x%llX noted as waited on by view 1's copy, not waited on and not issued "
                "(ETERNALVR_TEST_PE_FLARES_STALE=2)",
                kTag, static_cast<unsigned long long>(flares::kStaleTestQuery));
    }
    return wait;
}

void startFrame(bool clear, bool perView) {
    checkWaits();
    if (clear) {
        clearPendingBits();
    }
    if (perView && g_testStale && flares::setStaleTestBit(pendingWords(), g_splitFrames)) {
        add(g_counts.testBits);
    }
    g_splitFrames += perView ? 1 : 0;
    g_marks.reset();
}

Counts takeCounts() {
    Counts c;
    c.emptyWaits = g_counts.emptyWaits.exchange(0);
    c.unmarked = g_counts.unmarked.exchange(0);
    c.unissued = g_counts.unissued.exchange(0);
    c.staleWords = g_counts.staleWords.exchange(0);
    c.staleFrames = g_counts.staleFrames.exchange(0);
    c.testBits = g_counts.testBits.exchange(0);
    c.testOn = g_testStale;
    c.tripped = g_tripped.load(std::memory_order_acquire);
    return c;
}

} // namespace evr::vkcore::pe_flare_guard
