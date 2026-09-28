#pragma once

// Per-eye motion-vector capture for the TAA smear work (ETERNALVR_CAPTURE_MOTION=<dir>[,<every N pairs>],
// ui_layer/motion_target.hpp). With the variable set, images that could be the game's velocity targets get
// TRANSFER_SRC when the game creates them and their layouts are followed (ui_vulkan.hpp); the exposure hook
// notes the velocity target each eye's render bound (taa_hooks.hpp). On every Nth eye R copy of a Route S
// pair (and with each in-headset capture), both eyes' velocity images are copied into host buffers in eye R's
// copy command buffer; once the shared timeline shows them done, a background thread writes
// <dir>\mv-<pid>-p<n>-L.raw and -R.raw (the pixels as the image holds them, row after row) and a .txt file
// with the format, size, ticks and statistics of each eye. An in-headset capture writes <base>-MV-L.raw and
// so on next to its images.
//
// Needs per-eye TAA (the exposure hook) and the UI layer (the Vulkan hooks). Reads game memory only.
// All calls except the writer thread run on the present hook under the presenter's mutex.

#include "stereo_seq/eye_tags.hpp"
#include "stereo_seq/seq_settings.hpp"
#include "ui_layer/gui_target.hpp"
#include "vkcore/dispatch.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace evr::vkcore {

// The velocity target an eye's last render bound, read from the post-process context. The pointers are the
// engine's (a render target or an image; resolved when copied).
struct MotionTarget {
    std::uintptr_t current = 0;  // post-process + 0x60: what this render's TAA and DLSS read
    std::uintptr_t previous = 0; // + 0x68: the other one of the pair
    std::uint64_t tick = 0;
    std::uint32_t backendFrame = 0;
    std::uint32_t eyeSeq = 0;
    // `current` resolved while the render was recorded (an image set's member in use changes per frame):
    bool resolved = false;
    bool viaTarget = false;          // `current` is a render target, `fields` its image's
    ui_layer::GuiImageFields fields; // the image's (vkImage: the set when it is one)
    int setIndex = -1;               // the set's member in use (-1: a single image)
    std::uint64_t vkImage = 0;       // the VkImage the render used
};
// The exposure hook (taa_hooks.cpp, render-view job, per-eye TAA on) for each tagged render: notes `tag`'s
// eye's velocity target from the post-process context. Nothing unless ETERNALVR_CAPTURE_MOTION is set.
void noteMotionTarget(const stereo_seq::RenderTag& tag, const std::byte* postProcessContext);
// Eye 0 (left) or 1 (right); nullopt before that eye's first noted render.
std::optional<MotionTarget> motionTargetFor(int eye);

class MotionCapture {
public:
    void configure(const stereo_seq::CaptureSetting& setting);
    bool enabled() const { return every_ != 0; }

    // The next eye R copy also takes a capture for an in-headset capture, written as <base>-MV-*.
    void armOnce(std::wstring base) { onceBase_ = std::move(base); }

    // A copy command buffer, on a queue of `family`: eye R's (`eyeR`) copies both eyes' velocity images when
    // this pair is captured. False when nothing was recorded (not eye R, not this pair, or a check failed:
    // logged).
    bool record(DeviceData& dev, VkCommandBuffer cb, std::uint32_t family, bool eyeR);
    // A copy command buffer was submitted (timeline value; nothing unless record took this one) or not.
    void submitted(std::uint64_t timelineValue);
    void cancel();
    // Hands a completed capture to the writer thread.
    void poll(DeviceData& dev, std::uint64_t completedTimeline);
    void destroy(DeviceData& dev);

private:
    enum class State { Idle, Recorded, Submitted };
    struct Eye {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize size = 0; // of the buffer
        bool coherent = true;
        // This capture's image:
        bool taken = false;
        std::int32_t format = 0;
        VkExtent2D extent{};
        std::string note; // what was read, or why this eye was not copied
    };
    bool ensureBuffer(DeviceData& dev, Eye& eye, VkDeviceSize size);
    void freeBuffer(DeviceData& dev, Eye& eye);
    // One eye's checks and copy; false (with the reason in eye.note) when not copied.
    bool recordEye(DeviceData& dev, VkCommandBuffer cb, std::uint32_t family, int index);

    std::wstring directory_;
    std::uint32_t every_ = 0;
    std::uint64_t pairs_ = 0; // eye R copies seen
    State state_ = State::Idle;
    std::uint64_t index_ = 0;
    std::uint64_t value_ = 0;
    std::wstring onceBase_;
    std::wstring currentBase_; // the one-shot being taken
    std::array<Eye, 2> eyes_{};
    std::shared_ptr<std::atomic<bool>> writerBusy_ = std::make_shared<std::atomic<bool>>(false);
    std::uint64_t written_ = 0;
    std::uint64_t failures_ = 0;
};

} // namespace evr::vkcore
