// The depth layer's copy in the present hook (presenter_depth.hpp): each eye's depth into its slot's shared
// buffer, recorded into the command buffer that copies that eye's colour; the copies' GPU times and the
// source's views.

#include "vkcore/presenter_depth.hpp"

#include "vkcore/log.hpp"
#include "vkcore/presenter_depth_functions.hpp"
#include "vkcore/ui_engine.hpp"
#include "vkcore/ui_vulkan.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <mutex>

namespace evr::vkcore {

namespace {

// The shader's push constants (presenter_depth_shader.hpp).
struct Push {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t srcWidth = 0;
    std::uint32_t srcHeight = 0;
    std::uint32_t rowTexels = 0;
    std::uint32_t firstTexel = 0;
    float nearest = 1.0f;
};
static_assert(sizeof(Push) == 28);

constexpr std::uint32_t kGroup = 8; // local_size_x and _y

} // namespace

void DepthLayer::notCopied(const char* why) {
    eyesNotCopied_.fetch_add(1, std::memory_order_relaxed);
    lastWhy_.store(why, std::memory_order_relaxed);
}

void DepthLayer::noteRows(std::uint64_t tick, float m10, float m11) {
    std::lock_guard lock(rowsMutex_);
    // Both eyes of a tick note it: one record per tick, the newest row kept.
    for (RowRecord& r : rows_) {
        if (r.tick == tick) {
            r.m10 = m10;
            r.m11 = m11;
            return;
        }
    }
    rows_[nextRow_] = RowRecord{tick, m10, m11};
    nextRow_ = (nextRow_ + 1) % kRowRecords;
}

std::optional<DepthLayer::RowRecord> DepthLayer::rowsOf(std::uint64_t tick) {
    std::lock_guard lock(rowsMutex_);
    for (const RowRecord& r : rows_) {
        if (r.tick == tick && tick != 0) {
            return r;
        }
    }
    return std::nullopt;
}

void DepthLayer::retireView() {
    if (view_) {
        retired_.push_back({view_, viewValue_});
        view_ = VK_NULL_HANDLE;
    }
    viewImage_ = VK_NULL_HANDLE;
    viewSerial_ = 0;
}

VkImageView DepthLayer::viewFor(DeviceData& dev, VkImage image, std::uint64_t serial, std::int32_t format) {
    if (view_ && image == viewImage_ && serial == viewSerial_) {
        return view_;
    }
    retireView();
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = static_cast<VkFormat>(format);
    info.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (fn_->CreateImageView(dev.device, &info, nullptr, &view_) != VK_SUCCESS) {
        view_ = VK_NULL_HANDLE;
        return VK_NULL_HANDLE;
    }
    viewImage_ = image;
    viewSerial_ = serial;
    viewValue_ = 0;
    ui_vulkan::watchDestroy(image); // a game destroy of it retires the view (poll)
    return view_;
}

void DepthLayer::beginSlot(std::uint32_t slot, bool keepOther) {
    if (slot < kSlots && !keepOther) {
        slots_[slot].eyes = {};
    }
}

bool DepthLayer::record(DeviceData& dev,
                        VkCommandBuffer cb,
                        std::uint32_t family,
                        std::uint32_t slotIndex,
                        std::uint32_t cbIndex,
                        std::uint32_t firstEye,
                        std::uint32_t eyeCount,
                        std::uint64_t tick) {
    if (!active() || slotIndex >= kSlots || cbIndex >= kCommandBuffers || firstEye + eyeCount > 2) {
        return false;
    }
    Slot& slot = slots_[slotIndex];
    // The planes of this eye's own game tick, never another frame's (a map change moves the far plane).
    const std::optional<RowRecord> rows = rowsOf(tick);
    const float m10 = rows ? rows->m10 : 0.0f;
    const float m11 = rows ? rows->m11 : 0.0f;
    const std::optional<depth::DepthModel> model = rows ? depth::modelFromEngineRow(m10, m11) : std::nullopt;
    if (!slot.buffer || !model || !depth::planesOf(*model, unitsPerMetre_)) {
        notCopied(!slot.buffer ? "no shared buffer"
                  : rows       ? "the eye projection's depth row is not a reversed perspective"
                               : "no depth row of the eye's game tick");
        return false;
    }
    const std::optional<std::uint64_t> handle = ui_engine::readUpscaledDepth();
    if (!handle) {
        notCopied(depth::toString(depth::SourceCheck::NoImage));
        return false;
    }
    const auto image = reinterpret_cast<VkImage>(*handle);
    const std::optional<ui_layer::ImageRecord> record = ui_vulkan::recordOf(image);
    if (record) {
        ui_vulkan::follow(image);
    }
    const std::optional<ui_vulkan::ImageState> state = ui_vulkan::stateOf(image);
    std::optional<std::uint32_t> lastFamily;
    if (state) {
        std::lock_guard lock(dev.queueMutex);
        if (const auto it = dev.queueFamilies.find(state->queue); it != dev.queueFamilies.end()) {
            lastFamily = it->second;
        }
    }
    depth::SourceRecord source;
    if (record) {
        source.format = record->format;
        source.width = record->width;
        source.height = record->height;
        source.usage = record->usage;
        source.sharingMode = record->sharingMode;
        source.queueFamilies = record->queueFamilies;
    }
    // A depth layout change only on a queue with graphics (the game presents from its compute queue in a
    // level).
    const bool graphics = family < graphicsFamily_.size() && graphicsFamily_[family];
    const depth::SourceCheck check = depth::checkSource(
        record ? &source : nullptr, state ? std::optional<std::int32_t>(state->layout) : std::nullopt,
        lastFamily, family, graphics, eye_.width, eye_.height);
    const std::optional<depth::ReadLayout> readable =
        state ? depth::readLayoutFor(state->layout, graphics) : std::optional<depth::ReadLayout>();
    if (check != depth::SourceCheck::Ok || !record || !state || !readable) {
        notCopied(depth::toString(check));
        return false;
    }
    const depth::ReadLayout read = *readable;
    // The layouts met at the copy (the 10 s line): shader read-only, depth read-only, attachment.
    const std::size_t seen = state->layout == depth::vk::kLayoutShaderReadOnly         ? 0
                             : state->layout == depth::vk::kLayoutDepthStencilReadOnly ? 1
                                                                                       : 2;
    layouts_[seen].fetch_add(1, std::memory_order_relaxed);
    const VkImageView view = viewFor(dev, image, record->serial, record->format);
    if (!view) {
        notCopied("no view of the depth image");
        return false;
    }
    const VkDescriptorImageInfo imageInfo{VK_NULL_HANDLE, view, static_cast<VkImageLayout>(read.layout)};
    const VkDescriptorBufferInfo bufferInfo{slot.buffer, 0, VK_WHOLE_SIZE};
    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = sets_[cbIndex];
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &imageInfo;
    writes[1] = writes[0];
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pImageInfo = nullptr;
    writes[1].pBufferInfo = &bufferInfo;
    // The command buffer's earlier use has completed (acquireFreeSlot), so its set may be written again.
    fn_->UpdateDescriptorSets(dev.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0,
                              nullptr);

    const bool timed = queries_ && family < timestampBits_.size() && timestampBits_[family] > 0;
    if (timed) {
        fn_->CmdResetQueryPool(cb, queries_, cbIndex * 2, 2);
        fn_->CmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries_,
                               cbIndex * 2); // after the eye copy
    }
    // The game's depth, written earlier on its queues (covered by the present's wait semaphores), is read in
    // `read.layout`; the buffer comes from the D3D12 side, whose last read of it has finished (the slot is
    // free).
    const VkImageSubresourceRange range{depth::barrierAspects(record->format), 0, 1, 0, 1};
    VkImageMemoryBarrier toRead{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toRead.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toRead.oldLayout = static_cast<VkImageLayout>(state->layout);
    toRead.newLayout = static_cast<VkImageLayout>(read.layout);
    toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead.image = image;
    toRead.subresourceRange = range;
    VkBufferMemoryBarrier acquire{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    acquire.srcAccessMask = 0;
    acquire.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    acquire.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    acquire.dstQueueFamilyIndex = family;
    acquire.buffer = slot.buffer;
    acquire.offset = 0;
    acquire.size = VK_WHOLE_SIZE;
    dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                              0, nullptr, 1, &acquire, 1, &toRead);
    fn_->CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    fn_->CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0, 1, &sets_[cbIndex], 0,
                               nullptr);
    for (std::uint32_t e = firstEye; e < firstEye + eyeCount; ++e) {
        Push push;
        push.width = eye_.width;
        push.height = eye_.height;
        push.srcWidth = record->width;
        push.srcHeight = record->height;
        push.rowTexels = static_cast<std::uint32_t>(rowTexels_);
        push.firstTexel = e * eye_.width;
        push.nearest = depth::nearestDepth(*model, unitsPerMetre_);
        fn_->CmdPushConstants(cb, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        fn_->CmdDispatch(cb, (eye_.width + kGroup - 1) / kGroup, (eye_.height + kGroup - 1) / kGroup, 1);
        slot.eyes[e] = true;
        if (fault_ && e == 1) { // the planted fault: eye R's depth over eye L's too
            push.firstTexel = 0;
            fn_->CmdPushConstants(cb, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
            fn_->CmdDispatch(cb, (eye_.width + kGroup - 1) / kGroup, (eye_.height + kGroup - 1) / kGroup, 1);
        }
    }
    VkImageMemoryBarrier back = toRead;
    back.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    back.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    back.oldLayout = toRead.newLayout;
    back.newLayout = toRead.oldLayout; // where the game's own barriers have it
    VkBufferMemoryBarrier release = acquire;
    release.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    release.dstAccessMask = 0;
    release.srcQueueFamilyIndex = family;
    release.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                              0, nullptr, 1, &release, 1, &back);
    if (timed) {
        fn_->CmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries_, cbIndex * 2 + 1);
    }
    timing_[cbIndex].recorded = timed;
    timing_[cbIndex].read = true;
    slot.m10 = m10;
    slot.m11 = m11;
    eyesCopied_.fetch_add(eyeCount, std::memory_order_relaxed);
    if (!loggedFirst_) {
        loggedFirst_ = true;
        const depth::Planes planes = depth::planesOf(*model, unitsPerMetre_).value_or(depth::Planes{});
        EVR_LOG(
            "depth: first copy: _upscaledOpaqueDepth is image %p (%ux%u, format %d, usage 0x%X, %s), layout "
            "%d (last moved on queue family %d), read in layout %d%s, on queue family %u; depth row %.7f "
            "%.5f: nearZ %.1f m, farZ %.4f m; nothing nearer than %.2f m (depth %.4f)",
            reinterpret_cast<void*>(image), record->width, record->height, record->format, record->usage,
            record->sharingMode == VK_SHARING_MODE_CONCURRENT ? "concurrent" : "exclusive", state->layout,
            lastFamily ? static_cast<int>(*lastFamily) : -1, read.layout,
            read.transition ? " (moved there and back)" : "", family, m10, m11, planes.nearZ, planes.farZ,
            depth::kNearestMetres, depth::nearestDepth(*model, unitsPerMetre_));
    }
    return true;
}

void DepthLayer::submitted(std::uint32_t cbIndex, std::uint64_t value, VkQueue queue, VkSemaphore timeline) {
    if (!fn_ || cbIndex >= kCommandBuffers) { // off: nothing was made
        return;
    }
    Timing& t = timing_[cbIndex];
    if (value != 0 && t.read) {
        // The game's next work on any other queue waits for this read (ui_vulkan::holdQueues).
        ui_vulkan::holdQueues(queue, timeline, value);
    }
    if (value != 0 && t.recorded) {
        t.value = value;
    } else if (value == 0) {
        t = {};
    }
    t.recorded = false;
    t.read = false;
    if (value != 0 && view_) {
        viewValue_ = std::max(viewValue_, value);
    }
}

void DepthLayer::poll(DeviceData& dev, std::uint64_t completed) {
    if (!fn_) {
        return;
    }
    if (ui_vulkan::takeDestroyed()) {
        retireView(); // the game destroyed the source: its view goes once the reads of it have run
    }
    for (std::uint32_t i = 0; i < kCommandBuffers; ++i) {
        Timing& t = timing_[i];
        if (t.value == 0 || t.value > completed || !queries_) {
            continue;
        }
        std::array<std::uint64_t, 2> stamps{};
        const VkResult r =
            fn_->GetQueryPoolResults(dev.device, queries_, i * 2, 2, sizeof(stamps), stamps.data(),
                                     sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT);
        t.value = 0;
        if (r != VK_SUCCESS || stamps[1] < stamps[0]) {
            continue;
        }
        const auto ns = static_cast<std::uint64_t>(static_cast<double>(stamps[1] - stamps[0]) * timestampNs_);
        gpuSamples_.fetch_add(1, std::memory_order_relaxed);
        gpuSumNs_.fetch_add(ns, std::memory_order_relaxed);
        std::uint64_t seen = gpuMaxNs_.load(std::memory_order_relaxed);
        while (ns > seen && !gpuMaxNs_.compare_exchange_weak(seen, ns, std::memory_order_relaxed)) {
        }
    }
    std::erase_if(retired_, [&](const RetiredView& r) {
        if (r.value != 0 && r.value > completed) {
            return false;
        }
        fn_->DestroyImageView(dev.device, r.view, nullptr);
        return true;
    });
}

} // namespace evr::vkcore
