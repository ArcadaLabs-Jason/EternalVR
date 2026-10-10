#include "vkcore/view_snapshot_impl.hpp"

#include "vkcore/dispatch.hpp"
#include "vkcore/log.hpp"
#include "vkcore/snapshot_ring.hpp"
#include "vkcore/submit_order.hpp"
#include "vkcore/ui_vulkan.hpp"
#include "vkcore/view_slots.hpp"
#include "vkcore/view_snapshot_frames.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace evr::vkcore::view_snapshot {

namespace {

using snapshot_ring::kPairReading;
using snapshot_ring::kPairSlots;
using snapshot_ring::PairSlot;

// Presents in a row that keep the last pair before the eyes are the presented image again: the game makes
// a pair a frame, so a longer run means the copies stopped (logged in the 10 s lines).
constexpr std::uint32_t kKeepLimit = 8;

// Under the snapshots' mutex.
std::array<Ring, 2> g_rings; // [eye]: view 0's swapchain image, view 1's image
snapshot_ring::PairSlots g_pairs{};
std::array<std::uint64_t, 2> g_seqs{}; // each eye's last copy on its ring's timeline
std::array<std::array<std::uint64_t, Ring::kImages * 2>, 2>
    g_cbSeqs{};                // the copy each command buffer last ran
std::uint64_t g_lastShown = 0; // the frame of the last pair a present read
std::uint32_t g_kept = 0;      // presents in a row that kept the last pair
std::uint64_t g_newPairs = 0;  // presents that had a new pair (ETERNALVR_TEST_PE_DROP)
LogCap g_familyMoves{8};
std::uint64_t g_skippedMoves = 0;

std::size_t slots() {
    return parallelEyesSettings().pairSlots;
}

std::uint64_t completed(DeviceData& dev, VkSemaphore semaphore) {
    std::uint64_t value = 0;
    if (semaphore) {
        dev.vk.GetSemaphoreCounterValueKHR(dev.device, semaphore, &value);
    }
    return value;
}

// No copy writes a pair's images or reads them any more: the rings may be remade.
bool idle(DeviceData& dev) {
    const std::array<std::uint64_t, 2> done{completed(dev, g_rings[0].semaphore),
                                            completed(dev, g_rings[1].semaphore)};
    const std::uint64_t read = completed(dev, shared().presenterTimeline);
    for (const PairSlot& s : g_pairs) {
        for (std::size_t e = 0; e < 2; ++e) {
            if ((s.seq[e] != 0 && (!s.submitted[e] || s.seq[e] > done[e])) || s.prior[e] > done[e]) {
                return false;
            }
        }
        if (s.readValue == kPairReading || s.readValue > read || s.priorRead > read) {
            return false;
        }
    }
    return true;
}

// Eye `eye`'s ring for copies of `format` and `extent` on queue family `family`: made (or remade once no
// copy uses it, which starts the pairs again); false while it cannot be. Its command buffers are one queue
// family's: when the game's submits move to another (a menu on one, a level on another), they are made
// again for it once no copy is pending (its images serve every family).
bool ensure(DeviceData& dev, int eye, std::uint32_t family, VkFormat format, VkExtent2D extent) {
    const auto e = static_cast<std::size_t>(eye);
    Ring& ring = g_rings[e];
    Counters& c = shared().count;
    if (ring.failed) {
        return false;
    }
    if (ring.device && ring.device != dev.device) {
        ++c[eye == 0 ? Count::OtherFamily0 : Count::OtherFamily];
        return false;
    }
    if (ring.pool && ring.family != family) {
        if (!idle(dev)) {
            ++c[eye == 0 ? Count::OtherFamily0 : Count::OtherFamily]; // until its copies are done
            return false;
        }
        if (g_familyMoves.due(GetTickCount64(), g_skippedMoves)) {
            EVR_LOG("%s: pairs: eye %d's copies move from queue family %u to %u", kTag, eye, ring.family,
                    family);
        }
        dropCommands(dev, ring);
        g_cbSeqs[e] = {};
    }
    if (const char* why = makeObjects(dev, ring, family)) {
        ring.failed = true;
        EVR_LOG("%s: %s for eye %d's copies; the eyes are the presented image and view 1's", kTag, why, eye);
        return false;
    }
    if (ring.count == slots() && ring.format == format && ring.extent.width == extent.width &&
        ring.extent.height == extent.height) {
        return true;
    }
    if (ring.count != 0 && !idle(dev)) {
        ++c[eye == 0 ? Count::Busy0 : Count::Busy];
        return false;
    }
    if (ring.count != 0) {
        g_pairs = {}; // remade (no copy uses either ring): the other ring's copies pair with nothing now
    }
    if (!makeImages(dev, ring, slots(), format, extent)) {
        ring.failed = true;
        EVR_LOG("%s: no image for eye %d's copies; the eyes are the presented image and view 1's", kTag, eye);
        return false;
    }
    EVR_LOG("%s: pairs: eye %d's copies %zu images %ux%u format %d, %.1f MB", kTag, eye, ring.count,
            extent.width, extent.height, format, static_cast<double>(ring.bytes) / (1024.0 * 1024.0));
    return true;
}

// Whether the signals of the game's batches can move onto the copies' batch, from the first batch carrying
// one of the `count` view 0 command buffers on (submit_order.hpp).
submit_order::Move view0Move(std::uint32_t submitCount,
                             const VkSubmitInfo* submits,
                             const FrameSubmits::View0s& view0,
                             std::size_t count) {
    std::vector<submit_order::Batch> batches(submitCount);
    for (std::uint32_t s = 0; s < submitCount; ++s) {
        const VkSubmitInfo& info = submits[s];
        submit_order::Batch& b = batches[s];
        for (std::uint32_t i = 0; i < info.waitSemaphoreCount; ++i) {
            b.waits.push_back(reinterpret_cast<std::uint64_t>(info.pWaitSemaphores[i]));
        }
        for (std::uint32_t i = 0; i < info.signalSemaphoreCount; ++i) {
            b.signals.push_back(reinterpret_cast<std::uint64_t>(info.pSignalSemaphores[i]));
        }
        const auto* next = static_cast<const VkBaseInStructure*>(info.pNext);
        b.plainChain =
            !next || (next->sType == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO && !next->pNext);
        for (std::uint32_t i = 0; i < info.commandBufferCount && !b.carries; ++i) {
            for (std::size_t v = 0; v < count && !b.carries; ++v) {
                b.carries = info.pCommandBuffers[i] == view0[v].cb;
            }
        }
    }
    return submit_order::moveSignals(batches);
}

// The batch's waits: each eye's timeline and the presenter's (the larger value for each).
struct Waits {
    std::array<std::uint64_t, 3> values{};

    void add(std::size_t which, std::uint64_t value) { values[which] = std::max(values[which], value); }
};

// Eye `eye`'s copy of `source` (in `layout`) for frame `frame` into its pair's slot, appended to `a`.
bool addCopy(DeviceData& dev,
             int eye,
             std::uint64_t frame,
             std::uint64_t view,
             VkImage source,
             VkImageLayout layout,
             Append& a,
             Waits& waits) {
    const auto e = static_cast<std::size_t>(eye);
    Ring& ring = g_rings[e];
    Counters& c = shared().count;
    // Under ETERNALVR_TEST_PE_EYE1_LAG=1 the pair shown last stays: the next new pair shows its eye 1.
    const std::size_t slot = snapshot_ring::pairSlotFor(g_pairs, slots(), frame, eye,
                                                        parallelEyesSettings().eye1Lag ? g_lastShown : 0);
    // Of the slot's two command buffers, one whose last copy is done (a pending one cannot be recorded).
    const std::uint64_t done = completed(dev, ring.semaphore);
    std::size_t cb = ring.cbs.size();
    for (std::size_t i = slot * 2; slot < kPairSlots && i < slot * 2 + 2 && cb == ring.cbs.size(); ++i) {
        cb = g_cbSeqs[e][i] <= done ? i : cb;
    }
    if (cb == ring.cbs.size() || a.cbCount == a.cbs.size()) {
        ++c[eye == 0 ? Count::Busy0 : Count::Busy];
        return false;
    }
    PairSlot& s = g_pairs[slot];
    if (s.frame == frame) {
        // The second copy of the frame: both eyes' passes should have carried its view record.
        c[Count::PairViewDiffers] += s.view != 0 && view != 0 && s.view != view ? 1 : 0;
    } else {
        c[Count::NeverShown] += snapshot_ring::complete(s) && s.readValue == 0 ? 1 : 0;
        c[Count::PairNeverComplete] += s.frame != 0 && !snapshot_ring::complete(s) ? 1 : 0;
        snapshot_ring::claim(s, frame, view);
    }
    if (!recordCopy(dev, ring.cbs[cb], ring, slot, source, layout)) {
        ++c[eye == 0 ? Count::Busy0 : Count::Busy];
        return false;
    }
    // Written only once the slot's last copies of this eye and its last read are done, on the GPU.
    waits.add(e, s.prior[e]);
    waits.add(2, s.priorRead);
    s.seq[e] = ++g_seqs[e];
    s.submitted[e] = false;
    g_cbSeqs[e][cb] = s.seq[e];
    a.cbs[a.cbCount++] = ring.cbs[cb];
    a.halves[a.halfCount++] = static_cast<std::uint8_t>(slot * 2 + e);
    ++c[eye == 0 ? Count::Copies0 : Count::Copies];
    return true;
}

// ETERNALVR_TEST_PE_EYE1_LAG=1: eye 1 from the pair shown before `pick`'s (snapshot_ring::shownBefore), when
// there is one.
void lateEye1(Pick& pick) {
    Counters& c = shared().count;
    const std::size_t before = snapshot_ring::shownBefore(g_pairs, slots(), g_lastShown);
    if (before == kPairSlots) {
        ++c[Count::Eye1NotLate];
        return;
    }
    const PairSlot& s = g_pairs[before];
    const Ring& ring = g_rings[1];
    pick.eyes[1] = Copy{ring.images[before], ring.format, ring.extent, ring.semaphore, s.seq[1]};
    pick.eye1Slot = before;
    pick.eye1Frame = s.frame;
    pick.eye1View = s.view;
    ++c[pick.frame == s.frame + 1 ? Count::Eye1Late : Count::Eye1LateMore];
}

} // namespace

bool pairsOn() {
    return !parallelEyesSettings().guessPairs;
}

std::optional<Append> pairCopies(DeviceData& dev,
                                 std::uint32_t family,
                                 std::uint32_t submitCount,
                                 const VkSubmitInfo* submits,
                                 const FrameSubmits::View0s& view0,
                                 std::size_t count,
                                 const View1Copy& view1) {
    Shared& sh = shared();
    Append a;
    Waits waits;
    std::array<bool, 2> copied{};
    // Whatever the game orders after these batches waits for the eye 0 copies too: their signals move onto
    // the copies' batch (submit_order.hpp). When they cannot, no eye 0 copy is made from this submit.
    const submit_order::Move move = view0Move(submitCount, submits, view0, count);
    if (count > 0 && !move.movable) {
        sh.count[Count::Unordered0] += count;
        count = 0;
    }
    for (std::size_t i = 0; i < count; ++i) {
        // The swapchain image the frame's view 0 command buffer moved to PRESENT_SRC, where it is left.
        bool released = false;
        const VkImage image = frameOf(view0[i].cb, &released);
        const std::uint32_t number = imageNumber(image);
        if (number == 0) {
            ++sh.count[Count::NotSwapchain0];
        } else if (released) {
            ++sh.count[Count::Released0]; // another queue family reads it next: not on this one
        } else if (ensure(dev, 0, family, sh.swapFormat, sh.swapExtent) &&
                   addCopy(dev, 0, view0[i].frame, view0[i].view, image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, a,
                           waits)) {
            copied[0] = true;
        }
    }
    if (view1.frame != 0) {
        const std::optional<ui_layer::ImageRecord> record = ui_vulkan::recordOf(view1.clone);
        if (!record) {
            ++sh.count[Count::Unknown];
        } else if (ensure(dev, 1, family, static_cast<VkFormat>(record->format),
                          VkExtent2D{record->width, record->height})) {
            // View 1's image stays in GENERAL: its screen pass writes it as a storage image.
            copied[1] =
                addCopy(dev, 1, view1.frame, view1.view, view1.clone, VK_IMAGE_LAYOUT_GENERAL, a, waits);
        }
    }
    if (a.cbCount == 0) {
        return std::nullopt;
    }
    const std::array<VkSemaphore, 3> semaphores{g_rings[0].semaphore, g_rings[1].semaphore,
                                                sh.presenterTimeline};
    for (std::size_t w = 0; w < semaphores.size(); ++w) {
        if (waits.values[w] != 0 && semaphores[w]) {
            a.waits[a.waitCount] = semaphores[w];
            a.waitValues[a.waitCount++] = waits.values[w];
        }
    }
    for (std::size_t e = 0; e < 2; ++e) {
        if (copied[e]) {
            a.signals[a.signalCount] = g_rings[e].semaphore;
            a.signalValues[a.signalCount++] = g_seqs[e];
        }
    }
    a.moveFrom = copied[0] ? static_cast<std::uint32_t>(move.from) : UINT32_MAX;
    return a;
}

void pairsSubmitted(const Append& append, bool ok) {
    for (std::uint32_t i = 0; i < append.halfCount; ++i) {
        const std::size_t slot = append.halves[i] / 2;
        const std::size_t e = append.halves[i] % 2;
        PairSlot& s = g_pairs[slot];
        if (ok) {
            s.submitted[e] = true;
            continue;
        }
        s.seq[e] = 0; // never signalled: the slot's other copy waits for a new one
        for (std::size_t cb = 0; cb < g_rings[e].cbs.size(); ++cb) {
            if (g_rings[e].cbs[cb] == append.cbs[i]) {
                g_cbSeqs[e][cb] = 0; // recorded, never pending
            }
        }
    }
}

std::optional<Pick> pairForPresent(VkImage presented, TraceEntry& trace) {
    Shared& sh = shared();
    const snapshot_ring::PairPick p =
        snapshot_ring::pickPair(g_pairs, slots(), g_lastShown, viewSlotsView1Rendered(), g_kept, kKeepLimit);
    Pick pick;
    pick.paired = true; // read() and notRead() tell the pairs' picks from the guess's by it
    pick.slot = kPairSlots;
    const std::uint32_t dropEvery = parallelEyesSettings().dropEvery;
    if (p.show == snapshot_ring::PairShow::New && dropEvery != 0 && ++g_newPairs % dropEvery == 0) {
        // ETERNALVR_TEST_PE_DROP: not handed to the headset, which keeps the last pair; the new one stays
        // unread.
        ++sh.count[Count::PairDropped];
        trace.outcome = 'D';
        pick.drop = true;
        pick.dropWhy = FrameRepeat::Dropped;
        return pick;
    }
    if (p.show == snapshot_ring::PairShow::New) {
        PairSlot& s = g_pairs[p.index];
        for (std::size_t e = 0; e < 2; ++e) {
            const Ring& ring = g_rings[e];
            pick.eyes[e] = Copy{ring.images[p.index], ring.format, ring.extent, ring.semaphore, s.seq[e]};
        }
        pick.slot = p.index;
        pick.heldRead = s.readValue;
        pick.frame = s.frame;
        pick.view = s.view;
        pick.eye1Frame = s.frame;
        pick.eye1View = s.view;
        if (parallelEyesSettings().eye1Lag) {
            lateEye1(pick);
        }
        s.readValue = kPairReading; // until pairRead() or pairNotRead(): no copy writes it
        g_kept = 0;
        ++sh.count[Count::PairNew];
        sh.count[Count::PairSkipped] += p.skipped;
        trace.outcome = 'N';
        trace.shown = s.frame;
        trace.view = s.view;
        countLag(trace.own, s.frame);
        return pick;
    }
    if (p.show == snapshot_ring::PairShow::Kept ||
        sh.newImages.holds(reinterpret_cast<std::uint64_t>(presented))) {
        // Kept: the newest pair is shown already. Held: eye 0 would be a new swapchain image nothing drew
        // yet.
        ++g_kept;
        ++sh.count[p.show == snapshot_ring::PairShow::Kept ? Count::PairKept : Count::Undrawn];
        trace.outcome = p.show == snapshot_ring::PairShow::Kept ? 'K' : 'h';
        pick.drop = true;
        pick.dropWhy = p.show == snapshot_ring::PairShow::Kept ? FrameRepeat::Kept : FrameRepeat::Held;
        return pick;
    }
    ++sh.count[Count::PairNone];
    trace.outcome = 'P';
    return std::nullopt; // the eyes as without pairs
}

void pairRead(const Pick& pick, std::uint64_t value) {
    if (pick.slot < kPairSlots && g_pairs[pick.slot].frame == pick.frame) {
        g_pairs[pick.slot].readValue = value;
        g_lastShown = std::max(g_lastShown, pick.frame);
    }
    if (pick.eye1Slot < kPairSlots && g_pairs[pick.eye1Slot].frame == pick.eye1Frame) {
        // Eye 1 of the pair shown before (ETERNALVR_TEST_PE_EYE1_LAG=1): its slot's next copies wait for
        // this read too.
        PairSlot& before = g_pairs[pick.eye1Slot];
        before.readValue = std::max(before.readValue, value);
    }
}

void pairNotRead(const Pick& pick) {
    if (pick.slot < kPairSlots && g_pairs[pick.slot].frame == pick.frame &&
        g_pairs[pick.slot].readValue == kPairReading) {
        g_pairs[pick.slot].readValue = pick.heldRead;
    }
}

std::string describePair(const std::optional<Pick>& pick) {
    char line[200];
    if (!pick || !pick->eyes[0].image || !pick->eyes[1].image) {
        std::snprintf(line, sizeof(line), "  eyes: the presented image and view 1's (no pair)\n");
    } else if (pick->eye1Frame != pick->frame) {
        std::snprintf(
            line, sizeof(line),
            "  eye 1: copy %llu of frame %llu (ETERNALVR_TEST_PE_EYE1_LAG=1), eye 0: copy %llu (frame "
            "%llu; view record %llu)\n",
            static_cast<unsigned long long>(pick->eyes[1].wait),
            static_cast<unsigned long long>(pick->eye1Frame),
            static_cast<unsigned long long>(pick->eyes[0].wait), static_cast<unsigned long long>(pick->frame),
            static_cast<unsigned long long>(pick->view));
    } else {
        std::snprintf(line, sizeof(line),
                      "  eye 1: copy %llu, eye 0: copy %llu (the pair of frame %llu; view record %llu)\n",
                      static_cast<unsigned long long>(pick->eyes[1].wait),
                      static_cast<unsigned long long>(pick->eyes[0].wait),
                      static_cast<unsigned long long>(pick->frame),
                      static_cast<unsigned long long>(pick->view));
    }
    return line;
}

void destroyPairs(DeviceData& dev) {
    for (Ring& ring : g_rings) {
        if (ring.device == dev.device) {
            destroyRing(dev, ring);
        }
    }
    g_pairs = {};
    g_seqs = {};
    g_cbSeqs = {};
    g_lastShown = 0;
    g_kept = 0;
    g_newPairs = 0;
}

} // namespace evr::vkcore::view_snapshot
