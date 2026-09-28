#include "vkcore/ui_capture.hpp"

#include "stereo_seq/png_writer.hpp"
#include "vkcore/log.hpp"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace evr::vkcore {

void UiCapture::configure(const stereo_seq::CaptureSetting& setting) {
    directory_ = setting.directory;
    every_ = setting.everyPairs;
    if (!CreateDirectoryW(directory_.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        EVR_LOG("ui: cannot create the UI capture folder; UI capture off");
        every_ = 0;
        return;
    }
    EVR_LOG("ui: every %u GUI target capture(s) written as PNG files", every_);
}

bool UiCapture::ensureBuffer(DeviceData& dev, VkExtent2D extent) {
    if (buffer_ && extent.width == extent_.width && extent.height == extent_.height) {
        return true;
    }
    freeBuffer(dev);
    if (failed_) {
        return false;
    }
    VkPhysicalDeviceMemoryProperties props{};
    dev.instance->vk.GetPhysicalDeviceMemoryProperties(dev.physicalDevice, &props);
    const VkDeviceSize size = VkDeviceSize{extent.width} * extent.height * 4;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bool ok = dev.vk.CreateBuffer(dev.device, &info, nullptr, &buffer_) == VK_SUCCESS;
    VkMemoryRequirements req{};
    std::uint32_t type = UINT32_MAX;
    if (ok) {
        dev.vk.GetBufferMemoryRequirements(dev.device, buffer_, &req);
        for (std::uint32_t t = 0; t < props.memoryTypeCount; ++t) {
            const VkMemoryPropertyFlags f = props.memoryTypes[t].propertyFlags;
            if ((req.memoryTypeBits & (1u << t)) && (f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                // Cached memory reads fastest; take the first host-visible type otherwise.
                if (type == UINT32_MAX || (f & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) {
                    type = t;
                }
            }
        }
        ok = type != UINT32_MAX;
    }
    if (ok) {
        coherent_ = (props.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = type;
        ok = dev.vk.AllocateMemory(dev.device, &alloc, nullptr, &memory_) == VK_SUCCESS &&
             dev.vk.BindBufferMemory(dev.device, buffer_, memory_, 0) == VK_SUCCESS &&
             dev.vk.MapMemory(dev.device, memory_, 0, VK_WHOLE_SIZE, 0, &mapped_) == VK_SUCCESS;
    }
    if (!ok) {
        EVR_LOG("ui: a host buffer of %llu bytes could not be made; UI capture off",
                static_cast<unsigned long long>(size));
        freeBuffer(dev);
        failed_ = true;
        return false;
    }
    extent_ = extent;
    return true;
}

void UiCapture::freeBuffer(DeviceData& dev) {
    if (mapped_) {
        dev.vk.UnmapMemory(dev.device, memory_);
        mapped_ = nullptr;
    }
    if (buffer_) {
        dev.vk.DestroyBuffer(dev.device, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
    }
    if (memory_) {
        dev.vk.FreeMemory(dev.device, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
}

VkBuffer UiCapture::bufferFor(DeviceData& dev, std::uint64_t index, VkExtent2D extent) {
    const bool once = !oncePath_.empty();
    if ((!enabled() && !once) || failed_ || state_ != State::Idle || (!once && index % every_ != 0) ||
        writerBusy_->load() || !ensureBuffer(dev, extent)) {
        return VK_NULL_HANDLE;
    }
    state_ = State::Recorded;
    index_ = index;
    currentPath_ = std::move(oncePath_);
    oncePath_.clear();
    return buffer_;
}

void UiCapture::submitted(std::uint64_t timelineValue) {
    if (state_ == State::Recorded) {
        state_ = State::Submitted;
        value_ = timelineValue;
    }
}

void UiCapture::cancel() {
    if (state_ == State::Recorded) {
        state_ = State::Idle;
        currentPath_.clear(); // the eye capture takes the next pair, and arms this again
    }
}

void UiCapture::poll(DeviceData& dev, std::uint64_t completedTimeline) {
    if (state_ != State::Submitted || completedTimeline < value_ || writerBusy_->load()) {
        return;
    }
    if (!coherent_) {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = memory_;
        range.size = VK_WHOLE_SIZE;
        dev.vk.InvalidateMappedMemoryRanges(dev.device, 1, &range);
    }
    auto pixels =
        std::make_shared<std::vector<std::uint8_t>>(std::size_t{extent_.width} * extent_.height * 4);
    std::memcpy(pixels->data(), mapped_, pixels->size());
    state_ = State::Idle;
    writerBusy_->store(true);
    wchar_t name[96];
    swprintf_s(name, L"\\ui-%lu-c%06llu.png", GetCurrentProcessId(), static_cast<unsigned long long>(index_));
    const bool once = !currentPath_.empty();
    const std::wstring path = once ? currentPath_ : directory_ + name;
    currentPath_.clear();
    const VkExtent2D extent = extent_;
    const bool logIt = !once && ++written_ <= 10; // the eye capture logs a one-shot
    const std::uint64_t index = index_;
    // The DLL is pinned by the XR worker, so the thread's code outlives any unload of the layer.
    std::thread([busy = writerBusy_, pixels, path, extent, logIt, index] {
        const auto png = stereo_seq::encodePngRgba8(pixels->data(), extent.width, extent.height);
        std::FILE* f = nullptr;
        bool ok = _wfopen_s(&f, path.c_str(), L"wb") == 0 && f;
        if (ok) {
            ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
            ok = std::fclose(f) == 0 && ok;
        }
        if (logIt || !ok) {
            EVR_LOG("ui: GUI target capture %llu %s (%ux%u)", static_cast<unsigned long long>(index),
                    ok ? "written" : "NOT written", extent.width, extent.height);
        }
        busy->store(false);
    }).detach();
}

void UiCapture::destroy(DeviceData& dev) {
    freeBuffer(dev);
    state_ = State::Idle;
}

} // namespace evr::vkcore
