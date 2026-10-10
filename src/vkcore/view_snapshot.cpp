#include "vkcore/view_snapshot.hpp"
#include "vkcore/view_snapshot_frames.hpp"

#include "ui_layer/gui_target.hpp"
#include "vkcore/dispatch.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/presenter_eyes.hpp"
#include "vkcore/snapshot_ring.hpp"
#include "vkcore/swapchain_entry.hpp"
#include "vkcore/ui_engine.hpp"
#include "vkcore/ui_vulkan.hpp"
#include "vkcore/view_slots.hpp"
#include "vkcore/view_snapshot_impl.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace evr::vkcore::view_snapshot {

namespace {

using snapshot_ring::kSlots;

// A slot's readValue while a present that picked it is still recording its copy: never free to write.
constexpr std::uint64_t kReading = UINT64_MAX;

// Allocated once and never destroyed (layer_entry.cpp: no teardown at process exit).
std::mutex& g_mutex = *new std::mutex;
// Held from a copy's timeline value to the end of the submit carrying it (Append::order), so the copies
// reach the queues in the order of their values, whichever thread or queue submits them. Taken before
// g_mutex, never while holding it.
std::mutex& g_submitOrder = *new std::mutex;
Shared& g_shared = *new Shared;
// The guess (ETERNALVR_TEST_PE_PAIRING=guess): the copies of view 1's image.
Ring g_ring;
snapshot_ring::Slots g_slots{};
std::uint64_t g_seq = 0;
std::uint64_t g_lastRead = 0;

// The command buffer view 1's screen pass is being recorded into, its image, the swapchain image view 0 of
// the same frame draws and that frame's number (frameSubmits).
std::atomic<VkCommandBuffer> g_armedCb{VK_NULL_HANDLE};
VkImage g_armedClone = VK_NULL_HANDLE;
VkImage g_armedImage = VK_NULL_HANDLE;
std::uint64_t g_armedFrame = 0;
VkCommandBuffer g_view0Cb = VK_NULL_HANDLE; // the command buffer of the last view 0 screen pass
std::uint64_t g_view0Frame = 0;             // its frame
std::atomic<std::uint64_t> g_frameView{0};  // noteFrameView's last; g_armedView: the armed pass's frame's
std::uint64_t g_armedView = 0;
constexpr int kNewImagePresents = 4; // at most; two are expected undrawn after a recreate

// A/B runs (snapshot_ring::pickShown): ETERNALVR_TEST_EYE_SNAPSHOT=1 always the copy before the matched one,
// =2 the matched one also when both frames drew the presented image.
snapshot_ring::Shown shownMode() {
    static const snapshot_ring::Shown mode = [] {
        std::wstring value;
        readEnv(L"ETERNALVR_TEST_EYE_SNAPSHOT", value);
        return value == L"1"   ? snapshot_ring::Shown::Before
               : value == L"2" ? snapshot_ring::Shown::BothMatched
                               : snapshot_ring::Shown::ByImage;
    }();
    return mode;
}

} // namespace

const char* shownNote() {
    if (pairsOn()) {
        return "";
    }
    switch (shownMode()) {
    case snapshot_ring::Shown::Before:
        return " (ETERNALVR_TEST_EYE_SNAPSHOT=1: always the copy before the matched one)";
    case snapshot_ring::Shown::BothMatched:
        return " (ETERNALVR_TEST_EYE_SNAPSHOT=2: the matched copy when both frames drew the image)";
    default:
        return "";
    }
}

std::mutex& mutex() {
    return g_mutex;
}

Shared& shared() {
    return g_shared;
}

std::uint32_t imageNumber(VkImage image) {
    for (std::size_t i = 0; i < g_shared.swapImages.size(); ++i) {
        if (image && g_shared.swapImages[i] == image) {
            return static_cast<std::uint32_t>(i + 1);
        }
    }
    return 0;
}

void countLag(std::uint64_t own, std::uint64_t shown) {
    if (own == 0 || shown == 0 || shown > own) {
        return;
    }
    const std::uint64_t lag = own - shown;
    Counters& c = g_shared.count;
    ++c[lag == 0 ? Count::Lag0 : lag == 1 ? Count::Lag1 : Count::LagMore];
    c[Count::LagChanges] += g_shared.lastLag != kNoLag && lag != g_shared.lastLag ? 1 : 0;
    g_shared.lastLag = lag;
}

namespace {

VkImage vkImageOf(std::uintptr_t engineImage) {
    const std::optional<ui_layer::GuiImageFields> fields = ui_engine::readImageOrTarget(engineImage);
    if (!fields) {
        return VK_NULL_HANDLE;
    }
    std::uint64_t image = fields->vkImage;
    if ((fields->flags & ui_layer::engine::kImageSetFlag) != 0) {
        const auto member = ui_engine::readSetMember(image);
        image = member ? member->second : 0;
    }
    return reinterpret_cast<VkImage>(image);
}

void giveUp(const char* why) {
    g_ring.failed = true;
    EVR_LOG("%s: %s; eye 1 is copied from view 1's image at present", kTag, why);
}

// The timeline, the command buffers (the queue family of view 1's first submit) and the images (view 1's
// format and size). A size change waits until no copy writes or reads a slot (a present's pick holds its slot
// until its copy is submitted: kReading).
bool ensureRing(DeviceData& dev, std::uint32_t family, VkFormat format, VkExtent2D extent) {
    if (g_ring.failed) {
        return false;
    }
    if (const char* why = makeObjects(dev, g_ring, family)) {
        giveUp(why);
        return false;
    }
    if (g_ring.images[0] && g_ring.format == format && g_ring.extent.width == extent.width &&
        g_ring.extent.height == extent.height) {
        return true;
    }
    if (g_ring.images[0]) {
        std::uint64_t copied = 0;
        std::uint64_t read = 0;
        dev.vk.GetSemaphoreCounterValueKHR(dev.device, g_ring.semaphore, &copied);
        if (g_shared.presenterTimeline) {
            dev.vk.GetSemaphoreCounterValueKHR(dev.device, g_shared.presenterTimeline, &read);
        }
        for (const snapshot_ring::Slot& s : g_slots) {
            if ((s.submitted && s.seq > copied) || s.readValue > read) {
                return false;
            }
        }
        g_slots = {};
    }
    if (!makeImages(dev, g_ring, kSlots, format, extent)) {
        giveUp("no ring image");
        return false;
    }
    EVR_LOG("%s: %zu images %ux%u format %d, %.1f MB", kTag, kSlots, extent.width, extent.height, format,
            static_cast<double>(g_ring.bytes) / (1024.0 * 1024.0));
    return true;
}

// The guess: the copy of the armed view 1 pass's image, tagged with the semaphore its submit signals.
std::optional<Append> guessCopy(DeviceData& dev, std::uint32_t family, VkSemaphore tag) {
    Counters& c = g_shared.count;
    if (!tag) {
        ++c[Count::NoTag];
        return std::nullopt;
    }
    if ((g_ring.device && g_ring.device != dev.device) || (g_ring.pool && family != g_ring.family)) {
        // The ring's command buffers can only go to the queue family they were made for.
        ++c[Count::OtherFamily];
        return std::nullopt;
    }
    const std::optional<ui_layer::ImageRecord> record = ui_vulkan::recordOf(g_armedClone);
    if (!record) {
        ++c[Count::Unknown];
        return std::nullopt;
    }
    if (!ensureRing(dev, family, static_cast<VkFormat>(record->format),
                    VkExtent2D{record->width, record->height})) {
        ++c[Count::Busy];
        return std::nullopt;
    }
    std::uint64_t copied = 0;
    std::uint64_t read = 0;
    dev.vk.GetSemaphoreCounterValueKHR(dev.device, g_ring.semaphore, &copied);
    if (g_shared.presenterTimeline) {
        dev.vk.GetSemaphoreCounterValueKHR(dev.device, g_shared.presenterTimeline, &read);
    }
    const std::size_t slot = snapshot_ring::pickWrite(g_slots, copied, read);
    // View 1's image stays in GENERAL: its screen pass writes it as a storage image and nothing moves it.
    if (slot == kSlots ||
        !recordCopy(dev, g_ring.cbs[slot], g_ring, slot, g_armedClone, VK_IMAGE_LAYOUT_GENERAL)) {
        ++c[Count::Busy];
        return std::nullopt;
    }
    snapshot_ring::Slot& s = g_slots[slot];
    c[Count::NeverShown] += s.seq != 0 && s.submitted && s.readValue == 0 ? 1 : 0;
    s.seq = ++g_seq;
    s.tag = reinterpret_cast<std::uint64_t>(tag);
    s.image = reinterpret_cast<std::uint64_t>(g_armedImage);
    s.view = g_armedView;
    s.frame = g_armedFrame;
    s.submitted = true;
    s.readValue = 0;
    ++c[Count::Copies];
    Append a;
    a.cbs[a.cbCount++] = g_ring.cbs[slot];
    a.signals[a.signalCount] = g_ring.semaphore;
    a.signalValues[a.signalCount++] = s.seq;
    return a;
}

// The guess: the copy a present waiting on `waits` of `presented` shows.
std::optional<Pick>
guessForPresent(const VkSemaphore* waits, std::uint32_t waitCount, VkImage presented, TraceEntry& trace) {
    Counters& c = g_shared.count;
    snapshot_ring::Read r;
    for (std::uint32_t w = 0; w < waitCount && r.miss != snapshot_ring::Miss::None; ++w) {
        const snapshot_ring::Read t =
            snapshot_ring::pickRead(g_slots, reinterpret_cast<std::uint64_t>(waits[w]), g_lastRead);
        if (t.index < kSlots && (r.index == kSlots || t.miss == snapshot_ring::Miss::None)) {
            r = t;
        }
    }
    const std::uint64_t matched = r.index < kSlots ? g_slots[r.index].seq : 0;
    const auto image = reinterpret_cast<std::uint64_t>(presented);
    const auto shows = static_cast<std::uint8_t>(snapshot_ring::whoDrew(g_slots, r, image));
    if (r.index < kSlots) {
        ++c[static_cast<Count>(static_cast<std::size_t>(Count::DrewNotKnown) + (shows & 3))];
        trace.drew = "?mpb"[shows & 3];
    }
    const bool held = g_shared.newImages.holds(image);
    if (held) {
        r = snapshot_ring::orLast(g_slots, snapshot_ring::Read{},
                                  g_lastRead); // a repeat: the last pair stays
        c[Count::Undrawn] += r.index < kSlots ? 1 : 0;
    } else {
        r = snapshot_ring::pickShown(g_slots, r, image, g_lastRead, shownMode());
    }
    if (r.index == kSlots) {
        r = snapshot_ring::orLast(g_slots, r, g_lastRead);
        c[Count::LastAgain] += r.index < kSlots ? 1 : 0;
    }
    if (r.index == kSlots) {
        ++c[Count::NoCopy];
        trace.outcome = 'n';
        return std::nullopt;
    }
    const bool repeat = r.miss == snapshot_ring::Miss::Repeat;
    ++c[repeat ? Count::Repeated : Count::Matched];
    snapshot_ring::Slot& s = g_slots[r.index];
    Pick pick;
    pick.eyes[1] = Copy{g_ring.images[r.index], g_ring.format, g_ring.extent, g_ring.semaphore, s.seq};
    // Eye 0 is a new image: the pair would be a frame apart; the headset keeps the last pair
    // (ETERNALVR_TEST_PE_REPEATS=show shows it).
    pick.drop = repeat && !parallelEyesSettings().showRepeats;
    pick.dropWhy = held ? FrameRepeat::Held : FrameRepeat::Kept; // kept: no new eye 1 copy
    pick.slot = r.index;
    pick.heldRead = s.readValue;
    pick.frame = s.frame;
    pick.matched = matched;
    pick.lastRead = g_lastRead;
    pick.repeat = repeat;
    pick.shows = shows;
    pick.view = s.view;
    s.readValue = kReading; // until read() or notRead(): no snapshot overwrites it, no resize frees it
    trace.shown = s.frame;
    trace.view = s.view;
    trace.outcome = held ? 'h' : repeat ? 'r' : 's';
    countLag(trace.own, s.frame);
    return pick;
}

} // namespace

bool enabled() {
    // Only with Parallel Eye Rendering's eye copy (installed at the game's vkCreateInstance, before any
    // command buffer): under Route S nothing is followed or logged.
    static const bool on = [] {
        if (!eyeCopyRequested()) {
            return false;
        }
        std::wstring value;
        const bool off = readEnv(L"ETERNALVR_TEST_EYE_SNAPSHOT", value) && value == L"0";
        if (off) {
            EVR_LOG("%s: off (ETERNALVR_TEST_EYE_SNAPSHOT=0): each eye is copied from its view's image at "
                    "present",
                    kTag);
        } else if (pairsOn()) {
            EVR_LOG("%s: on: each eye from a copy of its view's image made after its frame's submit, a "
                    "present shows the newest pair of one frame (%u kept)",
                    kTag, parallelEyesSettings().pairSlots);
            if (parallelEyesSettings().eye1Lag) {
                EVR_LOG(
                    "%s: eye 1 a frame late (ETERNALVR_TEST_PE_EYE1_LAG=1, a rig control for the eye sync "
                    "check): a new pair shows eye 1 of the pair shown before it",
                    kTag);
            }
            if (const std::uint32_t every = parallelEyesSettings().dropEvery) {
                EVR_LOG("%s: one present in %u with a new pair is left out (ETERNALVR_TEST_PE_DROP=%u, a "
                        "rig control for the judder check): the headset keeps the last pair",
                        kTag, every, every);
            }
        } else {
            EVR_LOG("%s: on: eye 1 from a copy of view 1's image made after its own frame's command buffer, "
                    "eye 0 the presented image (ETERNALVR_TEST_PE_PAIRING=guess)%s",
                    kTag, shownNote());
        }
        return !off;
    }();
    return on;
}

void noteView0Pass(VkCommandBuffer cb) {
    if (!cb || !enabled() || !mp_guard::allowsGameTouch()) {
        return;
    }
    std::lock_guard lock(g_mutex);
    g_view0Cb = cb;
    g_view0Frame = frameSubmits().start(cb, g_frameView.load(std::memory_order_relaxed), g_shared.count);
}

void arm(std::uintptr_t cloneImage, VkCommandBuffer cb) {
    if (!enabled() || !mp_guard::allowsGameTouch()) {
        return;
    }
    const VkImage clone = vkImageOf(cloneImage);
    std::lock_guard lock(g_mutex);
    ++g_shared.count[Count::Arms];
    if (!cb || !clone) {
        ++g_shared.count[Count::NoCb];
        return;
    }
    g_armedClone = clone;
    g_armedImage = g_view0Cb ? frameOf(g_view0Cb) : VK_NULL_HANDLE;
    g_armedFrame = g_view0Frame;
    g_shared.newImages.drew(reinterpret_cast<std::uint64_t>(g_armedImage));
    g_armedView = g_frameView.load(std::memory_order_relaxed);
    if (g_armedCb.exchange(cb) != VK_NULL_HANDLE) {
        // The last pass's command buffer was not submitted before this one: no copy of it.
        ++g_shared.count[Count::Dropped];
    }
}

void holdNewSwapchainImages() {
    std::lock_guard lock(g_mutex);
    g_shared.newImages.arm(kNewImagePresents);
}

void noteFrameView(std::uint64_t view) {
    g_frameView.store(view, std::memory_order_relaxed);
}

void noteSwapchain(const VkImage* images, std::uint32_t count, VkFormat format, VkExtent2D extent) {
    std::lock_guard lock(g_mutex);
    g_shared.swapImages = {};
    for (std::uint32_t i = 0; i < count && i < g_shared.swapImages.size(); ++i) {
        g_shared.swapImages[i] = images[i];
    }
    g_shared.swapFormat = format;
    g_shared.swapExtent = extent;
}

void captureFired(std::uint32_t frames) {
    std::lock_guard lock(g_mutex);
    traceCapture(frames);
}

std::optional<Append> submitted(VkQueue queue, std::uint32_t submitCount, const VkSubmitInfo* submits) {
    const VkCommandBuffer armed = g_armedCb.load(std::memory_order_relaxed);
    const bool view0 = frameSubmits().carriesView0(submitCount, submits);
    // After a multiplayer guard trip nothing of the game is copied (the presenter asks forPresent no more).
    if ((!armed && !view0) || !mp_guard::allowsGameTouch()) {
        return std::nullopt;
    }
    // The frame's present waits on what the game's batches signal: the guess tags the copy with that
    // semaphore (the batch with view 1's command buffer, else the last one before it that signals one, else
    // the first one after it).
    bool found = false;
    VkSemaphore tag = VK_NULL_HANDLE;
    for (std::uint32_t s = 0; s < submitCount && armed; ++s) {
        bool here = false;
        for (std::uint32_t i = 0; i < submits[s].commandBufferCount && !here; ++i) {
            here = submits[s].pCommandBuffers[i] == armed;
        }
        found = found || here;
        if (submits[s].signalSemaphoreCount > 0 && (here || !found || !tag)) {
            tag = submits[s].pSignalSemaphores[submits[s].signalSemaphoreCount - 1];
        }
    }
    if (!found && !view0) {
        return std::nullopt;
    }
    DeviceData* dev = findDeviceData(queue);
    if (!dev || !dev->interopEnabled) {
        return std::nullopt; // no presenter without interop (nor the timeline semaphores the rings need)
    }
    std::uint32_t family = UINT32_MAX;
    {
        std::lock_guard lock(dev->queueMutex);
        const auto it = dev->queueFamilies.find(queue);
        if (it != dev->queueFamilies.end()) {
            family = it->second;
        }
    }
    std::unique_lock order(g_submitOrder);
    std::lock_guard lock(g_mutex);
    g_shared.device = dev->device;
    // Which submits carry each frame's view 0 and view 1 command buffers (the order lines and the trace).
    FrameSubmits::View0s view0s{};
    const std::size_t view0Count =
        frameSubmits().submitted(submitCount, submits, found ? g_armedFrame : 0, view0s, g_shared.count);
    VkCommandBuffer expected = armed;
    const bool took = found && g_armedCb.compare_exchange_strong(expected, VK_NULL_HANDLE);
    if (family == UINT32_MAX) {
        return std::nullopt;
    }
    const View1Copy view1 = took ? View1Copy{g_armedFrame, g_armedView, g_armedClone} : View1Copy{};
    std::optional<Append> append =
        pairsOn() ? pairCopies(*dev, family, submitCount, submits, view0s, view0Count, view1)
        : took    ? guessCopy(*dev, family, tag)
                  : std::nullopt;
    if (append) {
        append->order = std::move(order); // until the submit that carries it has returned
    }
    return append;
}

void copiesSubmitted(const Append& append, bool ok) {
    std::lock_guard lock(g_mutex);
    if (pairsOn()) {
        pairsSubmitted(append, ok);
        return;
    }
    for (snapshot_ring::Slot& s : g_slots) {
        if (!ok && append.signalCount > 0 && s.seq == append.signalValues[0]) {
            s = snapshot_ring::Slot{}; // never signalled: free again, never read
        }
    }
}

std::optional<Pick> forPresent(const VkSemaphore* waits, std::uint32_t waitCount, VkImage presented) {
    std::lock_guard lock(g_mutex);
    Counters& c = g_shared.count;
    if (c[Count::Arms] == 0) {
        return std::nullopt; // no view 1 screen pass yet (or off): nothing counted
    }
    ++c[Count::Presents];
    TraceEntry trace;
    trace.present = c[Count::Presents];
    trace.seconds = logSeconds();
    trace.own = frameSubmits().newestView1();
    trace.view0 = frameSubmits().view0(trace.own);
    trace.order = frameSubmits().order(trace.own);
    trace.image = imageNumber(presented);
    if (trace.view0 != '?') {
        ++c[trace.view0 == 's' ? Count::AtPresentSubmitted : Count::AtPresentNotYet];
    }
    std::optional<Pick> pick =
        pairsOn() ? pairForPresent(presented, trace) : guessForPresent(waits, waitCount, presented, trace);
    if (pick) {
        pick->present = trace.present;
    }
    tracePresent(trace);
    logWindow(c);
    return pick;
}

void read(const Pick& pick, VkSemaphore presenterTimeline, std::uint64_t value) {
    std::lock_guard lock(g_mutex);
    g_shared.presenterTimeline = presenterTimeline;
    // Every pick of the pairs is `paired` (its slot an index of the pairs' slots, or none); only the guess's
    // index the guess's slots.
    if (pick.paired) {
        pairRead(pick, value);
    } else if (!pairsOn() && pick.slot < kSlots && g_slots[pick.slot].seq == pick.eyes[1].wait) {
        g_slots[pick.slot].readValue = value;
        g_lastRead = pick.eyes[1].wait;
    }
    traceCopied(pick.present, value);
}

void notRead(const Pick& pick) {
    std::lock_guard lock(g_mutex);
    if (pick.paired) {
        pairNotRead(pick);
    } else if (!pairsOn() && pick.slot < kSlots && g_slots[pick.slot].seq == pick.eyes[1].wait &&
               g_slots[pick.slot].readValue == kReading) {
        g_slots[pick.slot].readValue = pick.heldRead;
    }
}

std::string describe(const std::optional<Pick>& pick) {
    if (pairsOn()) {
        return describePair(pick);
    }
    static constexpr const char* kShows[] = {"not known", "the matched frame", "the frame before", "both"};
    char line[200];
    if (!pick || !pick->eyes[1].image) {
        std::snprintf(line, sizeof(line), "  eye 1: view 1's image (no copy)\n");
    } else {
        std::snprintf(
            line, sizeof(line),
            "  eye 1: copy %llu%s (the present waits on copy %llu's frame; last read %llu; its image was "
            "drawn by %s; view record %llu)\n",
            static_cast<unsigned long long>(pick->eyes[1].wait), pick->repeat ? ", a repeat" : "",
            static_cast<unsigned long long>(pick->matched), static_cast<unsigned long long>(pick->lastRead),
            kShows[pick->shows & 3], static_cast<unsigned long long>(pick->view));
    }
    return line;
}

void onDeviceDestroyed(DeviceData& dev) {
    std::lock_guard lock(g_mutex);
    g_armedCb.store(VK_NULL_HANDLE);
    if (g_shared.device != dev.device) {
        return;
    }
    logSession(g_shared.count);
    g_shared.presenterTimeline = VK_NULL_HANDLE; // the presenter's, destroyed with it
    g_shared.device = VK_NULL_HANDLE;
    destroyPairs(dev);
    destroyRing(dev, g_ring);
    g_slots = {};
    g_lastRead = 0;
    frameSubmits().reset();
}

} // namespace evr::vkcore::view_snapshot
