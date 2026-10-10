#pragma once

// Internal part of the eye snapshots (view_snapshot.hpp), shared by view_snapshot.cpp (the hooks, the guess
// of ETERNALVR_TEST_PE_PAIRING=guess), view_snapshot_pairs.cpp (the pairs, the default),
// view_snapshot_ring.cpp (the rings of copies), view_snapshot_frames.cpp (which swapchain image a command
// buffer draws, the frames and the submits that carry them) and view_snapshot_stats.cpp (the counts, their
// lines and the trace of the last presents).

#include "vkcore/snapshot_ring.hpp"
#include "vkcore/view_snapshot.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace evr::vkcore {
struct DeviceData;
}

namespace evr::vkcore::view_snapshot {

inline constexpr const char* kTag = "eye-snapshot";

// The copies of one view's image (view_snapshot_ring.cpp): device-local images every queue family can use, a
// command buffer or two for each (one queue family) and the timeline semaphore their copies signal.
struct Ring {
    static constexpr std::size_t kImages = 4;
    VkDevice device = VK_NULL_HANDLE;
    std::array<VkImage, kImages> images{};
    std::array<VkDeviceMemory, kImages> memory{};
    std::array<VkCommandBuffer, kImages * 2> cbs{};
    VkCommandPool pool = VK_NULL_HANDLE;
    std::uint32_t family = UINT32_MAX;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::size_t count = 0;  // images made
    VkDeviceSize bytes = 0; // their memory
    bool failed = false;
};

// The timeline and the command buffers (for queue family `family`) once: null, or why not.
const char* makeObjects(DeviceData& dev, Ring& ring, std::uint32_t family);
// `count` images of `format` and `extent` in place of the ring's; no copy may write or read them any more.
bool makeImages(DeviceData& dev, Ring& ring, std::size_t count, VkFormat format, VkExtent2D extent);
void destroyImages(DeviceData& dev, Ring& ring);
// The command pool and its command buffers (none pending), for makeObjects to make them for another family.
void dropCommands(DeviceData& dev, Ring& ring);
void destroyRing(DeviceData& dev, Ring& ring);
// Records into `cb` (one of the ring's, not pending) the copy of `source` (in `layout`: GENERAL is read in
// place, another one moved to TRANSFER_SRC and back) into image `slot`, left in TRANSFER_SRC.
bool recordCopy(DeviceData& dev,
                VkCommandBuffer cb,
                const Ring& ring,
                std::size_t slot,
                VkImage source,
                VkImageLayout layout);

// What the snapshots count. The four Drew* are snapshot_ring::Drew's order.
enum class Count : std::size_t {
    Arms,        // view 1 screen passes
    Dropped,     // ... whose command buffer was not submitted before the next pass
    NoCb,        // ... without a command buffer or image
    NoTag,       // ... whose submit signals no semaphore
    OtherFamily, // ... submitted to another queue family than the ring's
    Copies,      // copies of view 1's image
    Busy,        // ... not made: no slot free (or the ring being resized)
    Unknown,     // ... not made: view 1's image not known
    Presents,
    Matched,      // presents with eye 1 from their frame's copy
    Repeated,     // presents with eye 1 from the copy the last present read
    NoCopy,       // presents with eye 1 from view 1's image (no copy)
    DrewNotKnown, // matched presents by who drew the presented image
    DrewMatched,
    DrewBefore,
    DrewBoth,
    LastAgain, // presents with no copy of their own that repeated the last one
    Undrawn,   // presents of a new swapchain image no view 0 pass drew yet, held
    V0Before,  // frames whose view 0 command buffer was submitted before view 1's (an earlier submit)
    V0With,    // ... in the same submit
    V0After,   // ... in a later one
    V0Never,   // ... never seen submitted (view 1's was)
    AtPresentSubmitted, // presents: the newest frame with view 1 submitted had its view 0 submitted too
    AtPresentNotYet,    // ... not yet
    Lag0,               // presents showing the newest frame with view 1 submitted
    Lag1,               // ... the frame before it
    LagMore,            // ... an older one
    LagChanges,         // presents showing another lag than the present before
    NeverShown,         // copies written over without a present reading them
    PairNew,            // pairs: presents showing a new pair
    PairKept,           // ... none newer: the headset keeps the last one
    PairNone,           // ... no pair: the presented image (and view 1's)
    PairSkipped,        // complete pairs a newer one went past: never shown
    PairNeverComplete,  // pairs written over with one eye's copy only
    Copies0,            // copies of view 0's swapchain image
    Busy0,              // ... not made: no slot free (or the ring being resized), or too late
    NotSwapchain0,      // ... not made: its command buffer moved no swapchain image to PRESENT_SRC
    Released0,          // ... not made: that barrier hands the image to another queue family
    OtherFamily0,       // ... not made: another queue family than the ring's
    PairViewDiffers,    // pairs whose two copies came with different view records (frames told apart wrong)
    Unordered0,         // eye 0 copies not made: the game's signals could not move after them
    Eye1Late,           // ETERNALVR_TEST_PE_EYE1_LAG=1: new pairs with eye 1 of the frame before
    Eye1LateMore,       // ... of an older frame (the pair shown before is two or more frames older)
    Eye1NotLate,        // ... with their own eye 1 (no pair shown before)
    PairDropped,        // ETERNALVR_TEST_PE_DROP: presents with a new pair not handed to the headset
    Size
};

struct Counters {
    std::array<std::uint64_t, static_cast<std::size_t>(Count::Size)> n{};

    std::uint64_t& operator[](Count c) { return n[static_cast<std::size_t>(c)]; }
    std::uint64_t operator[](Count c) const { return n[static_cast<std::size_t>(c)]; }
};

// The snapshots' mutex (view_snapshot.cpp).
std::mutex& mutex();

inline constexpr std::uint64_t kNoLag = UINT64_MAX;

// What the guess and the pairs share, under the mutex (view_snapshot.cpp).
struct Shared {
    VkDevice device = VK_NULL_HANDLE; // the game's, once a submit carried a frame
    Counters count;                   // since the start
    VkSemaphore presenterTimeline = VK_NULL_HANDLE;
    std::array<VkImage, 8> swapImages{}; // the game's swapchain
    VkFormat swapFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapExtent{};
    // After a swapchain recreate: presents of its images until one a view 0 pass drew (snapshot_ring.hpp).
    snapshot_ring::NewImageHold newImages;
    std::uint64_t lastLag = kNoLag; // the frame shown behind the newest, at the last present showing one
};
Shared& shared();
// Under the mutex: `image`'s index in the swapchain + 1 (0: not one of its images).
std::uint32_t imageNumber(VkImage image);
// Under the mutex: a present shows frame `shown`, the newest with view 1 submitted being `own`.
void countLag(std::uint64_t own, std::uint64_t shown);

// Each view 0 screen pass starts a frame, numbered from 1 (view 1's pass of a frame is recorded after it).
// Which submit (counted) carried a frame's view 0 command buffer and its view 1 one, for the last kKept
// frames.
class FrameSubmits {
public:
    static constexpr std::size_t kKept = 8;

    // A frame whose view 0 command buffer a submit carries.
    struct View0 {
        std::uint64_t frame = 0;
        std::uint64_t view = 0; // its view record (0: not known)
        VkCommandBuffer cb = VK_NULL_HANDLE;
    };
    using View0s = std::array<View0, kKept>;

    // Under the mutex: view 0's screen pass of a frame with view record `view` is recorded into `cb`; the new
    // frame's number.
    std::uint64_t start(VkCommandBuffer cb, std::uint64_t view, Counters& count);
    // Any thread, no lock: a command buffer in `submits` is a kept frame's view 0 one not seen submitted.
    bool carriesView0(std::uint32_t submitCount, const VkSubmitInfo* submits) const;
    // Under the mutex: a game submit with kept frames' view 0 command buffers (those frames into `view0`,
    // oldest first; their count returned), or with frame `view1`'s view 1 one (0: none).
    std::size_t submitted(std::uint32_t submitCount,
                          const VkSubmitInfo* submits,
                          std::uint64_t view1,
                          View0s& view0,
                          Counters& count);
    // No lock: `cb` is a kept frame's view 0 command buffer not seen submitted; begun() then (under the
    // mutex) when the game begins it again: that frame's is never submitted.
    bool holds(VkCommandBuffer cb) const;
    void begun(VkCommandBuffer cb);
    // Under the mutex: the newest frame whose view 1 command buffer was submitted (0: none).
    std::uint64_t newestView1() const { return newest1_; }
    // Under the mutex, for the trace: frame `frame`'s view 0 command buffer 's' submitted, 'w' not yet ('?':
    // not kept); its submit against view 1's: 'b' before, 'w' the same, 'a' after ('?': not both yet).
    char view0(std::uint64_t frame) const;
    char order(std::uint64_t frame) const;
    void reset();

private:
    struct Entry {
        std::uint64_t frame = 0;
        std::uint64_t view = 0;
        std::atomic<VkCommandBuffer> cb{VK_NULL_HANDLE}; // view 0's, until seen submitted
        std::uint64_t call0 = 0;                         // the submits that carried view 0's and view 1's
        std::uint64_t call1 = 0;
    };
    const Entry* find(std::uint64_t frame) const;
    void classify(const Entry& e, Counters& count) const;

    std::array<Entry, kKept> entries_{};
    std::uint64_t frames_ = 0;
    std::uint64_t calls_ = 0;
    std::uint64_t newest1_ = 0;
};

FrameSubmits& frameSubmits(); // view_snapshot_frames.cpp

// One present in the trace (view_snapshot_stats.cpp): the last kTrace presents, logged when an in-headset
// capture fires.
struct TraceEntry {
    std::uint64_t present = 0; // its number
    double seconds = 0.0;      // the log's time
    std::uint64_t own = 0;     // the newest frame with view 1's command buffer submitted
    std::uint64_t shown = 0;   // the frame its copies are of (0: none)
    std::uint64_t value = 0;   // the presenter's copy with it (its timeline value; 0: none)
    std::uint64_t view = 0;    // the view record it carries
    char outcome = '?'; // pairs: N new, K kept, P none; guess: s its frame's copy, r again, n none; h held
    char view0 = '?';   // FrameSubmits::view0 of `own`
    char order = '?';   // FrameSubmits::order of `own`
    char drew = '?';    // who drew the presented image: m the matched frame, p the one before, b both
    std::uint32_t image = 0; // the presented image's index in the swapchain + 1 (0: not known)
};
inline constexpr std::size_t kTrace = 512;

// Under the mutex: a present's entry; the presenter's copy that went with present `present`; an in-headset
// capture of `frames` frames fired: the trace is logged once the burst's presents are in it too.
void tracePresent(const TraceEntry& entry);
void traceCopied(std::uint64_t present, std::uint64_t value);
void traceCapture(std::uint32_t frames);

// Under the snapshots' mutex (view_snapshot.cpp). On a present: every 10 s, the counts since the last such
// line, with shares of the presents. At vkDestroyDevice: the session's.
void logWindow(const Counters& now);
void logSession(const Counters& now);

// The A/B mode's note for the lines ("" by default; view_snapshot.cpp).
const char* shownNote();

// ---- The pairs (view_snapshot_pairs.cpp) ----------------------------------------------------------------

// ETERNALVR_TEST_PE_PAIRING is not guess.
bool pairsOn();

// The view 1 screen pass a submit carries: its frame (0: none), view record and image (GENERAL).
struct View1Copy {
    std::uint64_t frame = 0;
    std::uint64_t view = 0;
    VkImage clone = VK_NULL_HANDLE;
};

// Under the mutex, from submitted(): the copies a game submit (`submits`) on a queue of `family` carries
// frames for: view 0's swapchain image of the `count` frames in `view0`, view 1's image of `view1`.
std::optional<Append> pairCopies(DeviceData& dev,
                                 std::uint32_t family,
                                 std::uint32_t submitCount,
                                 const VkSubmitInfo* submits,
                                 const FrameSubmits::View0s& view0,
                                 std::size_t count,
                                 const View1Copy& view1);
void pairsSubmitted(const Append& append, bool ok);
// Under the mutex: the pair a present of `presented` shows (its trace entry filled in), or none.
std::optional<Pick> pairForPresent(VkImage presented, TraceEntry& trace);
void pairRead(const Pick& pick, std::uint64_t value);
void pairNotRead(const Pick& pick);
std::string describePair(const std::optional<Pick>& pick);
void destroyPairs(DeviceData& dev);

} // namespace evr::vkcore::view_snapshot
