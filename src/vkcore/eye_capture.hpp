#pragma once

// Per-eye capture for the Route S experiments (ETERNALVR_CAPTURE_EYES=<dir>[,<every N pairs>],
// docs/VR_STEREO.md): every Nth complete eye pair, both presented images are also copied into host
// buffers by the present hook's own copy command buffers; once the shared timeline shows the copies done,
// a background thread writes them as <dir>\eyes-<pid>-p<pair>-t<tick>-L.png and -R.png (S1 pixel diff,
// S2 fusion checks), uncompressed so that a pair is written in a few tens of ms. At most one pair is in
// flight, so a slow disk or a small N skips pairs rather than piling up.
//
// The same copies take the in-headset capture for bug reports (bug_capture.hpp): one pair (or one mono
// image) armed with armOnce, whatever the every-N setting and whether it is on. A burst
// (ETERNALVR_CAPTURE_BURST) takes that many consecutive pairs (or mono frames) into a pool of host buffers,
// one per image, which the writer thread then compresses in place.
//
// All calls except the writer thread run on the present hook under the presenter's mutex.

#include "stereo_seq/seq_settings.hpp"
#include "vkcore/dispatch.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace evr::vkcore {

// A capture for a bug report: the next pair's (or a mono frame's) images as <base>-L.png and <base>-R.png
// (or <base>-mono.png), and <base>.txt.
struct OneShot {
    std::wstring base;        // the full path without its suffix
    std::string sidecar;      // the text file's contents; a line on the GUI target is added
    bool mono = false;        // one image for both eyes: only eye L's buffer is written
    std::uint32_t frames = 1; // a burst: this many consecutive frames, saved as <base>-fNN-L.png / -R.png
    bool ui = false;          // the GUI target was captured with eye L (set once eye L's copy is recorded)
    std::string uiNote;       // why not, when it was not
    std::uint32_t number = 0;
    std::uint32_t reserved = 0; // frames counted against the session's limit (bug_capture::take)
    LONGLONG requestQpc = 0;
    // Where the lines of the burst frame being copied start in `sidecar` (a frame given up drops them).
    std::size_t frameText = 0;
};

class EyeCapture {
public:
    void configure(const stereo_seq::CaptureSetting& setting);
    bool enabled() const { return every_ != 0; }

    // A one-shot capture can start now (no pair in flight, the writer idle, the format handled so far).
    [[nodiscard]] bool readyForOnce() const {
        return !failed_ && state_ == State::Idle && !writerBusy_->load();
    }
    // Arms `shot` for the next bufferFor(eye 0); disarmOnce drops it when that did not take it.
    void armOnce(OneShot shot) { once_ = std::move(shot); }
    void disarmOnce() { once_.reset(); }
    [[nodiscard]] OneShot* once() { return once_ ? &*once_ : nullptr; }

    // The buffer eye `eye` (0 left, 1 right) of pair `pairIndex` is copied into, or VK_NULL_HANDLE when
    // this pair is not captured. Eye 0 decides for the pair; eye 1 follows it. `completedTimeline` is the
    // shared timeline's value now: buffers are only remade (a new size) once no copy into them is pending.
    VkBuffer bufferFor(DeviceData& dev,
                       int eye,
                       std::uint64_t pairIndex,
                       VkFormat format,
                       VkExtent2D extent,
                       std::uint64_t completedTimeline);
    // A one-shot burst has taken some of its frames and waits for the next one (bufferFor eye 0, then eye 1
    // for a pair).
    [[nodiscard]] bool between() const { return state_ == State::Between; }
    // The frame the burst is at (0 on).
    [[nodiscard]] std::uint32_t frame() const { return frame_; }
    // The burst stops with the frames it has (a frame of the other kind came: a menu's mono frame in a burst
    // of pairs, a pair in a burst of mono frames).
    void endBurst();
    // A copy into a buffer handed out above was submitted; it is done at `timelineValue`.
    void copySubmitted(std::uint64_t timelineValue);
    // Eye R's copy was submitted; the pair is complete once the timeline reaches `timelineValue`.
    void submitted(std::uint64_t timelineValue, std::uint64_t tick);
    // The pair being captured was given up.
    void cancel();
    // Hands a completed pair to the writer thread.
    void poll(DeviceData& dev, std::uint64_t completedTimeline);
    void destroy(DeviceData& dev);

    // Records the copy of `source` (in TRANSFER_SRC_OPTIMAL) into `buffer` and makes it visible to the host.
    static void
    record(DeviceData& dev, VkCommandBuffer cb, VkImage source, VkExtent2D extent, VkBuffer buffer);
    // Records the copy of both halves of a two-eye ring image (in TRANSFER_DST_OPTIMAL, just written by the
    // eye copies; left there afterwards) into `left` and `right`, `eye` wide each.
    static void recordRingEyes(DeviceData& dev,
                               VkCommandBuffer cb,
                               VkImage ring,
                               VkExtent2D eye,
                               const std::array<VkBuffer, 2>& buffers);

private:
    enum class State { Idle, Left, Between, Submitted };
    bool ensureBuffers(DeviceData& dev, VkFormat format, VkExtent2D extent, std::size_t count, bool burst);
    void freeBuffers(DeviceData& dev);
    // The buffer of image `eye` of burst frame `frame`: two per pair; a burst of mono frames one per frame.
    [[nodiscard]] std::size_t slot(std::uint32_t frame, int eye) const {
        return monoBurst_ ? frame : std::size_t{frame} * 2 + static_cast<std::size_t>(eye);
    }
    // Hands the one-shot's images to a writer thread, which reads them from the mapped buffers (nothing
    // writes them until it is done: bufferFor waits for the writer; destroy stops it first).
    void writeOnce(OneShot shot);
    // The pool of a burst (hundreds of MB) is freed once its frames are written and no copy is pending.
    void freeBurstPool(DeviceData& dev, std::uint64_t completedTimeline);

    // What a one-shot's writer shares with destroy(): it reads the mapped buffers only while holding `lock`,
    // and before each image checks `stop`, which destroy sets under `lock` before it frees them.
    struct WriterGuard {
        std::mutex lock;
        bool stop = false;
    };

    std::wstring directory_;
    std::uint32_t every_ = 0;
    State state_ = State::Idle;
    std::uint32_t frame_ = 0; // the burst's frame being copied
    bool monoBurst_ = false;  // the capture is a burst of mono frames (one buffer per frame)
    std::uint64_t pairIndex_ = 0;
    std::uint64_t tick_ = 0;
    std::uint64_t value_ = 0;
    std::uint64_t lastCopy_ = 0; // timeline value of the last copy submitted into the buffers
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    bool coherent_ = true;
    bool failed_ = false;
    std::vector<VkBuffer> buffers_; // eye L and eye R of each frame (a mono burst: one per frame)
    std::vector<VkDeviceMemory> memory_;
    std::vector<void*> mapped_;
    std::shared_ptr<std::atomic<bool>> writerBusy_ = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<WriterGuard> guard_ = std::make_shared<WriterGuard>(); // the newest one-shot writer's
    std::uint64_t written_ = 0;
    std::optional<OneShot> once_;
};

} // namespace evr::vkcore
