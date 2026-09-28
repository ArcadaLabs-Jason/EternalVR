#pragma once

// PNG capture of the game's GUI target for the UI layer experiments (ETERNALVR_CAPTURE_UI=<dir>[,<every N
// captures>], docs/rig-findings/ui-layer.md section 8): every Nth captured target is also copied into a
// host buffer by the present hook's copy command buffer; once the shared timeline shows the copy done, a
// background thread writes it as <dir>\ui-<pid>-c<capture>.png with its alpha channel (premultiplied, as
// the game draws it). At most one image is in flight.
//
// All calls except the writer thread run on the present hook under the presenter's mutex.

#include "stereo_seq/seq_settings.hpp"
#include "vkcore/dispatch.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace evr::vkcore {

class UiCapture {
public:
    void configure(const stereo_seq::CaptureSetting& setting);
    bool enabled() const { return every_ != 0; }

    // The buffer capture `index` of an RGBA8 target of `extent` is copied into, or VK_NULL_HANDLE when this
    // one is not captured.
    VkBuffer bufferFor(DeviceData& dev, std::uint64_t index, VkExtent2D extent);
    // The copy into the buffer was submitted and completes at `timelineValue`.
    void submitted(std::uint64_t timelineValue);
    // The copy was not submitted after all.
    void cancel();
    // Hands a completed image to the writer thread.
    void poll(DeviceData& dev, std::uint64_t completedTimeline);
    void destroy(DeviceData& dev);

    // The in-headset capture (bug_capture.hpp): the next bufferFor takes this image whatever the every-N
    // setting and whether it is on, written as `path`. disarmOnce drops it and returns true when it was not
    // taken.
    void armOnce(std::wstring path) { oncePath_ = std::move(path); }
    bool disarmOnce() {
        const bool untaken = !oncePath_.empty();
        oncePath_.clear();
        return untaken;
    }

private:
    enum class State { Idle, Recorded, Submitted };
    bool ensureBuffer(DeviceData& dev, VkExtent2D extent);
    void freeBuffer(DeviceData& dev);

    std::wstring directory_;
    std::uint32_t every_ = 0;
    State state_ = State::Idle;
    std::uint64_t index_ = 0;
    std::uint64_t value_ = 0;
    VkExtent2D extent_{};
    bool coherent_ = true;
    bool failed_ = false;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    void* mapped_ = nullptr;
    std::shared_ptr<std::atomic<bool>> writerBusy_ = std::make_shared<std::atomic<bool>>(false);
    std::uint64_t written_ = 0;
    std::wstring oncePath_;    // armed
    std::wstring currentPath_; // the image in flight is a one-shot, written here
};

} // namespace evr::vkcore
