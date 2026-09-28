#include "vkcore/eye_capture.hpp"

#include "stereo_seq/png_writer.hpp"
#include "vkcore/bug_capture.hpp"
#include "vkcore/log.hpp"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace evr::vkcore {

namespace {

// Creates `dir` and its missing parents.
bool makeDirectories(const std::wstring& dir) {
    for (std::size_t at = dir.find_first_of(L"\\/", 3);; at = dir.find_first_of(L"\\/", at + 1)) {
        const std::wstring part = at == std::wstring::npos ? dir : dir.substr(0, at);
        if (!part.empty() && !CreateDirectoryW(part.c_str(), nullptr) &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            return false;
        }
        if (at == std::wstring::npos) {
            return true;
        }
    }
}

bool writeFile(const std::wstring& path, const std::vector<std::uint8_t>& bytes) {
    std::FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) {
        return false;
    }
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return std::fclose(f) == 0 && ok;
}

std::string narrow(const std::wstring& text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size > 0 ? size : 0), '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size,
                            nullptr, nullptr);
    }
    return out;
}

} // namespace

void EyeCapture::configure(const stereo_seq::CaptureSetting& setting) {
    directory_ = setting.directory;
    every_ = setting.everyPairs;
    if (!makeDirectories(directory_)) {
        EVR_LOG("capture: cannot create the eye capture folder; capture off");
        every_ = 0;
        return;
    }
    EVR_LOG("capture: every %u eye pair(s) written as PNG files", every_);
}

bool EyeCapture::ensureBuffers(DeviceData& dev, VkFormat format, VkExtent2D extent) {
    if (buffers_[0] && format == format_ && extent.width == extent_.width &&
        extent.height == extent_.height) {
        return true;
    }
    freeBuffers(dev);
    if (failed_ || !stereo_seq::pixelLayoutOfVkFormat(static_cast<std::int32_t>(format))) {
        if (!failed_) {
            EVR_LOG("capture: swapchain format %d is not handled; capture off", format);
        }
        failed_ = true;
        return false;
    }
    VkPhysicalDeviceMemoryProperties props{};
    dev.instance->vk.GetPhysicalDeviceMemoryProperties(dev.physicalDevice, &props);
    const VkDeviceSize size = VkDeviceSize{extent.width} * extent.height * 4;
    for (int i = 0; i < 2; ++i) {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (dev.vk.CreateBuffer(dev.device, &info, nullptr, &buffers_[i]) != VK_SUCCESS) {
            break;
        }
        VkMemoryRequirements req{};
        dev.vk.GetBufferMemoryRequirements(dev.device, buffers_[i], &req);
        // Host-visible and coherent, cached if there is such a type (reads from uncached memory are slow).
        std::uint32_t best = UINT32_MAX;
        for (std::uint32_t t = 0; t < props.memoryTypeCount; ++t) {
            const VkMemoryPropertyFlags f = props.memoryTypes[t].propertyFlags;
            if (!(req.memoryTypeBits & (1u << t)) || !(f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                continue;
            }
            const auto score = [&](std::uint32_t type) {
                const VkMemoryPropertyFlags g = props.memoryTypes[type].propertyFlags;
                return ((g & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) ? 2 : 0) +
                       ((g & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ? 1 : 0);
            };
            if (best == UINT32_MAX || score(t) > score(best)) {
                best = t;
            }
        }
        if (best == UINT32_MAX) {
            break;
        }
        coherent_ = (props.memoryTypes[best].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = best;
        if (dev.vk.AllocateMemory(dev.device, &alloc, nullptr, &memory_[i]) != VK_SUCCESS ||
            dev.vk.BindBufferMemory(dev.device, buffers_[i], memory_[i], 0) != VK_SUCCESS ||
            dev.vk.MapMemory(dev.device, memory_[i], 0, VK_WHOLE_SIZE, 0, &mapped_[i]) != VK_SUCCESS) {
            break;
        }
    }
    if (!mapped_[0] || !mapped_[1]) {
        EVR_LOG("capture: host buffers of %llu bytes could not be made; capture off",
                static_cast<unsigned long long>(size));
        freeBuffers(dev);
        failed_ = true;
        return false;
    }
    format_ = format;
    extent_ = extent;
    return true;
}

void EyeCapture::freeBuffers(DeviceData& dev) {
    for (int i = 0; i < 2; ++i) {
        if (mapped_[i]) {
            dev.vk.UnmapMemory(dev.device, memory_[i]);
            mapped_[i] = nullptr;
        }
        if (buffers_[i]) {
            dev.vk.DestroyBuffer(dev.device, buffers_[i], nullptr);
            buffers_[i] = VK_NULL_HANDLE;
        }
        if (memory_[i]) {
            dev.vk.FreeMemory(dev.device, memory_[i], nullptr);
            memory_[i] = VK_NULL_HANDLE;
        }
    }
}

VkBuffer EyeCapture::bufferFor(DeviceData& dev,
                               int eye,
                               std::uint64_t pairIndex,
                               VkFormat format,
                               VkExtent2D extent,
                               std::uint64_t completedTimeline) {
    if ((!enabled() && !once_) || failed_) {
        return VK_NULL_HANDLE;
    }
    if (eye == 0) {
        if (state_ != State::Idle || (!once_ && pairIndex % every_ != 0) || writerBusy_->load()) {
            return VK_NULL_HANDLE;
        }
        // A pair given up after its eye L copy was submitted leaves that copy pending: the buffers may not
        // be freed (a new size) under it.
        const bool reshape = !buffers_[0] || format != format_ || extent.width != extent_.width ||
                             extent.height != extent_.height;
        if (reshape && completedTimeline < lastCopy_) {
            return VK_NULL_HANDLE;
        }
        if (!ensureBuffers(dev, format, extent)) {
            return VK_NULL_HANDLE;
        }
        state_ = State::Left;
        pairIndex_ = pairIndex;
        return buffers_[0];
    }
    if (state_ != State::Left || format != format_ || extent.width != extent_.width ||
        extent.height != extent_.height) {
        return VK_NULL_HANDLE;
    }
    return buffers_[1];
}

void EyeCapture::submitted(std::uint64_t timelineValue, std::uint64_t tick) {
    if (state_ == State::Left) {
        state_ = State::Submitted;
        value_ = timelineValue;
        tick_ = tick;
    }
}

void EyeCapture::copySubmitted(std::uint64_t timelineValue) {
    if (timelineValue > lastCopy_) {
        lastCopy_ = timelineValue;
    }
}

void EyeCapture::cancel() {
    if (state_ == State::Left) {
        state_ = State::Idle;
        if (once_) {
            once_.reset();
            bug_capture::retake();
        }
    }
}

void EyeCapture::poll(DeviceData& dev, std::uint64_t completedTimeline) {
    if (state_ != State::Submitted || completedTimeline < value_ || writerBusy_->load()) {
        return;
    }
    const VkDeviceSize size = VkDeviceSize{extent_.width} * extent_.height * 4;
    if (!coherent_) {
        std::array<VkMappedMemoryRange, 2> ranges{};
        for (int i = 0; i < 2; ++i) {
            ranges[i].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            ranges[i].memory = memory_[i];
            ranges[i].size = VK_WHOLE_SIZE;
        }
        dev.vk.InvalidateMappedMemoryRanges(dev.device, 2, ranges.data());
    }
    state_ = State::Idle;
    if (once_) {
        OneShot shot = std::move(*once_);
        once_.reset();
        writeOnce(std::move(shot));
        return;
    }
    // An exception must not leave the present hook (or the writer thread): it would end the game.
    std::shared_ptr<std::vector<std::uint8_t>> left;
    std::shared_ptr<std::vector<std::uint8_t>> right;
    try {
        left = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(size));
        right = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(size));
    } catch (const std::exception&) {
        EVR_LOG("capture: no memory for eye pair %llu; skipped", static_cast<unsigned long long>(pairIndex_));
        return;
    }
    std::memcpy(left->data(), mapped_[0], left->size());
    std::memcpy(right->data(), mapped_[1], right->size());
    writerBusy_->store(true);
    wchar_t stem[96];
    swprintf_s(stem, L"\\eyes-%lu-p%06llu-t%llu", GetCurrentProcessId(),
               static_cast<unsigned long long>(pairIndex_), static_cast<unsigned long long>(tick_));
    const std::wstring base = directory_ + stem;
    const auto layout = *stereo_seq::pixelLayoutOfVkFormat(static_cast<std::int32_t>(format_));
    const VkExtent2D extent = extent_;
    const bool logIt = ++written_ <= 10;
    const std::uint64_t pair = pairIndex_;
    // The DLL is pinned by the XR worker, so the thread's code outlives any unload of the layer.
    const auto write = [busy = writerBusy_, left, right, base, layout, extent, logIt, pair] {
        bool ok = true;
        stereo_seq::AlphaStats alpha;
        const std::pair<const std::vector<std::uint8_t>*, const wchar_t*> eyes[] = {{left.get(), L"-L.png"},
                                                                                    {right.get(), L"-R.png"}};
        try {
            alpha = stereo_seq::alphaStats(left->data(), extent.width, extent.height,
                                           std::size_t{extent.width} * 4, layout);
            for (const auto& [pixels, suffix] : eyes) {
                const auto rgb = stereo_seq::toRgb8(pixels->data(), extent.width, extent.height,
                                                    std::size_t{extent.width} * 4, layout);
                ok = writeFile(base + suffix,
                               stereo_seq::encodePngRgb8(rgb.data(), extent.width, extent.height)) &&
                     ok;
            }
        } catch (const std::exception&) {
            ok = false; // out of memory for the image
        }
        if (logIt || !ok || alpha.min < 255) {
            // The game's alpha travels into the projection layer; a runtime that blends layer 0 by it shows
            // what lies behind the layer through it (docs/VR_STEREO.md, Projection alpha).
            EVR_LOG("capture: eye pair %llu %s (%ux%u); eye L alpha min %u, mean %.1f, %.1f%% of pixels "
                    "below 255",
                    static_cast<unsigned long long>(pair), ok ? "written" : "NOT written", extent.width,
                    extent.height, alpha.min, alpha.mean, alpha.belowOpaque * 100.0);
        }
        busy->store(false);
    };
    try {
        std::thread(write).detach();
    } catch (const std::exception&) {
        writerBusy_->store(false);
        EVR_LOG("capture: no thread for eye pair %llu; skipped", static_cast<unsigned long long>(pair));
    }
}

void EyeCapture::writeOnce(OneShot shot) {
    LARGE_INTEGER start;
    LARGE_INTEGER frequency;
    QueryPerformanceCounter(&start);
    QueryPerformanceFrequency(&frequency);
    const std::size_t size = std::size_t{extent_.width} * extent_.height * 4;
    const int images = shot.mono ? 1 : 2;
    std::array<std::shared_ptr<std::vector<std::uint8_t>>, 2> pixels;
    try {
        for (int i = 0; i < images; ++i) {
            pixels[i] = std::make_shared<std::vector<std::uint8_t>>(size);
            std::memcpy(pixels[i]->data(), mapped_[i], size);
        }
    } catch (const std::exception&) {
        EVR_LOG("capture: no memory for capture %u; not saved", shot.number);
        return;
    }
    LARGE_INTEGER copied;
    QueryPerformanceCounter(&copied);
    const double seconds = 1.0 / static_cast<double>(frequency.QuadPart);
    const double renderMs = static_cast<double>(copied.QuadPart - start.QuadPart) * seconds * 1000.0;
    writerBusy_->store(true);
    const auto layout = *stereo_seq::pixelLayoutOfVkFormat(static_cast<std::int32_t>(format_));
    const VkExtent2D extent = extent_;
    const auto write = [busy = writerBusy_, pixels, shot, layout, extent, renderMs, seconds, images] {
        bool ok = true;
        const wchar_t* suffixes[2] = {images == 1 ? L"-mono.png" : L"-L.png", L"-R.png"};
        try {
            for (int i = 0; i < images; ++i) {
                const auto rgb = stereo_seq::toRgb8(pixels[i]->data(), extent.width, extent.height,
                                                    std::size_t{extent.width} * 4, layout);
                ok = writeFile(shot.base + suffixes[i],
                               stereo_seq::encodePngRgb8(rgb.data(), extent.width, extent.height)) &&
                     ok;
            }
            std::string text = shot.sidecar;
            text += shot.ui
                        ? std::string("gui target: saved as -UI.png (RGBA, premultiplied, as the HUD quad "
                                      "shows it)\n")
                        : "gui target: not saved (" + shot.uiNote + ")\n";
            ok = writeFile(shot.base + L".txt", std::vector<std::uint8_t>(text.begin(), text.end())) && ok;
        } catch (const std::exception&) {
            ok = false; // out of memory for the image
        }
        LARGE_INTEGER done;
        QueryPerformanceCounter(&done);
        const double afterMs = static_cast<double>(done.QuadPart - shot.requestQpc) * seconds * 1000.0;
        EVR_LOG("capture: %s %s%s to %s-*.png (+ .txt), capture %u: %.1f ms on the render thread, %.0f ms "
                "after the trigger pull",
                ok ? "saved" : "NOT saved (write failed)", images == 1 ? "mono frame (both eyes)" : "eye L/R",
                shot.ui ? " + UI" : "", narrow(shot.base).c_str(), shot.number, renderMs, afterMs);
        busy->store(false);
    };
    try {
        std::thread(write).detach();
    } catch (const std::exception&) {
        writerBusy_->store(false);
        EVR_LOG("capture: no thread for capture %u; not saved", shot.number);
    }
}

void EyeCapture::destroy(DeviceData& dev) {
    freeBuffers(dev);
    state_ = State::Idle;
}

void EyeCapture::record(
    DeviceData& dev, VkCommandBuffer cb, VkImage source, VkExtent2D extent, VkBuffer buffer) {
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent.width, extent.height, 1};
    dev.vk.CmdCopyImageToBuffer(cb, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);
    VkBufferMemoryBarrier toHost{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHost.buffer = buffer;
    toHost.size = VK_WHOLE_SIZE;
    dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr,
                              1, &toHost, 0, nullptr);
}

} // namespace evr::vkcore
