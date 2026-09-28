#include "vkcore/motion_capture.hpp"

#include "ui_layer/motion_target.hpp"
#include "vkcore/eye_capture.hpp"
#include "vkcore/log.hpp"
#include "vkcore/ui_engine.hpp"
#include "vkcore/ui_vulkan.hpp"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "motion";
// The post-process context's velocity pointers: this render's and the one before, `_motionVector[b & 1]` and
// `[(b + 1) & 1]` (docs/rig-findings/stereo-temporal.md section 2.2).
constexpr std::size_t kPostProcessVelocity = 0x60;
constexpr std::size_t kPostProcessPreviousVelocity = 0x68;

std::mutex g_targetMutex;
MotionTarget g_targets[2];

std::string narrow(const std::wstring& text) {
    std::string out;
    for (const wchar_t c : text) {
        out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    return out;
}

std::string format(const char* f, ...) {
    char line[512];
    va_list args;
    va_start(args, f);
    const int n = std::vsnprintf(line, sizeof(line), f, args);
    va_end(args);
    return n > 0 ? std::string(line) : std::string();
}

// Layouts a copy may start from (the game's images sit in one of these between its passes).
bool copyableLayout(VkImageLayout layout) {
    return layout == VK_IMAGE_LAYOUT_GENERAL || layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
           layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
           layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
}

bool writeFile(const std::wstring& path, const void* data, std::size_t size) {
    std::FILE* f = nullptr;
    bool ok = _wfopen_s(&f, path.c_str(), L"wb") == 0 && f;
    if (ok) {
        ok = std::fwrite(data, 1, size, f) == size;
        ok = std::fclose(f) == 0 && ok;
    }
    return ok;
}

} // namespace

void noteMotionTarget(const stereo_seq::RenderTag& tag, const std::byte* postProcessContext) {
    if (!ui_vulkan::motionCaptureRequested() || tag.eye == stereo_seq::Eye::Mono) {
        return;
    }
    MotionTarget m;
    std::memcpy(&m.current, postProcessContext + kPostProcessVelocity, sizeof(m.current));
    std::memcpy(&m.previous, postProcessContext + kPostProcessPreviousVelocity, sizeof(m.previous));
    m.tick = tag.tick;
    m.backendFrame = tag.backendFrame;
    m.eyeSeq = tag.eyeSeq;
    if (const auto fields = ui_engine::readImageOrTarget(m.current, &m.viaTarget)) {
        m.resolved = true;
        m.fields = *fields;
        m.vkImage = fields->vkImage;
        if ((fields->flags & ui_layer::engine::kImageSetFlag) != 0) {
            const auto member = ui_engine::readSetMember(fields->vkImage);
            m.setIndex = member ? member->first : -1;
            m.vkImage = member ? member->second : 0;
        }
        // Its layout is followed from its first use as velocity (the game makes many images like it).
        if (m.vkImage != 0) {
            ui_vulkan::follow(reinterpret_cast<VkImage>(m.vkImage));
        }
    }
    std::lock_guard lock(g_targetMutex);
    g_targets[stereo_seq::eyeIndex(tag.eye)] = m;
}

std::optional<MotionTarget> motionTargetFor(int eye) {
    if (eye < 0 || eye > 1) {
        return std::nullopt;
    }
    std::lock_guard lock(g_targetMutex);
    if (g_targets[eye].current == 0) {
        return std::nullopt;
    }
    return g_targets[eye];
}

void MotionCapture::configure(const stereo_seq::CaptureSetting& setting) {
    directory_ = setting.directory;
    every_ = setting.everyPairs;
    if (!CreateDirectoryW(directory_.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        EVR_LOG("%s: cannot create the motion capture folder; motion capture off", kTag);
        every_ = 0;
        return;
    }
    EVR_LOG(
        "%s: both eyes' velocity images copied every %u pair(s) and with each in-headset capture, into %s",
        kTag, every_, narrow(directory_).c_str());
}

bool MotionCapture::ensureBuffer(DeviceData& dev, Eye& eye, VkDeviceSize size) {
    if (eye.buffer && eye.size >= size) {
        return true;
    }
    freeBuffer(dev, eye);
    VkPhysicalDeviceMemoryProperties props{};
    dev.instance->vk.GetPhysicalDeviceMemoryProperties(dev.physicalDevice, &props);
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bool ok = dev.vk.CreateBuffer(dev.device, &info, nullptr, &eye.buffer) == VK_SUCCESS;
    VkMemoryRequirements req{};
    std::uint32_t type = UINT32_MAX;
    if (ok) {
        dev.vk.GetBufferMemoryRequirements(dev.device, eye.buffer, &req);
        for (std::uint32_t t = 0; t < props.memoryTypeCount; ++t) {
            const VkMemoryPropertyFlags f = props.memoryTypes[t].propertyFlags;
            if ((req.memoryTypeBits & (1u << t)) && (f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                if (type == UINT32_MAX || (f & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) {
                    type = t;
                }
            }
        }
        ok = type != UINT32_MAX;
    }
    if (ok) {
        eye.coherent = (props.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = type;
        ok = dev.vk.AllocateMemory(dev.device, &alloc, nullptr, &eye.memory) == VK_SUCCESS &&
             dev.vk.BindBufferMemory(dev.device, eye.buffer, eye.memory, 0) == VK_SUCCESS &&
             dev.vk.MapMemory(dev.device, eye.memory, 0, VK_WHOLE_SIZE, 0, &eye.mapped) == VK_SUCCESS;
    }
    if (!ok) {
        freeBuffer(dev, eye);
        return false;
    }
    eye.size = size;
    return true;
}

void MotionCapture::freeBuffer(DeviceData& dev, Eye& eye) {
    if (eye.mapped) {
        dev.vk.UnmapMemory(dev.device, eye.memory);
        eye.mapped = nullptr;
    }
    if (eye.buffer) {
        dev.vk.DestroyBuffer(dev.device, eye.buffer, nullptr);
        eye.buffer = VK_NULL_HANDLE;
    }
    if (eye.memory) {
        dev.vk.FreeMemory(dev.device, eye.memory, nullptr);
        eye.memory = VK_NULL_HANDLE;
    }
    eye.size = 0;
}

bool MotionCapture::recordEye(DeviceData& dev, VkCommandBuffer cb, std::uint32_t family, int index) {
    Eye& eye = eyes_[static_cast<std::size_t>(index)];
    eye.taken = false;
    const std::optional<MotionTarget> target = motionTargetFor(index);
    if (!target) {
        eye.note = "no velocity target noted for this eye (per-eye TAA off, or no render yet)";
        return false;
    }
    eye.note = format("tick %llu, backend frame %u, eye frame %u; velocity pointer %p (previous %p)",
                      static_cast<unsigned long long>(target->tick), target->backendFrame, target->eyeSeq,
                      reinterpret_cast<void*>(target->current), reinterpret_cast<void*>(target->previous));
    if (!target->resolved) {
        eye.note += "; it reads as neither a render target nor an image";
        return false;
    }
    const ui_layer::GuiImageFields* fields = &target->fields;
    const auto image = reinterpret_cast<VkImage>(target->vkImage);
    eye.note += format(" = %s; engine format %u, %dx%d, flags 0x%X, set member %d, VkImage %p",
                       target->viaTarget ? "render target" : "image", fields->format, fields->width,
                       fields->height, fields->flags, target->setIndex, reinterpret_cast<void*>(image));
    if (!image) {
        eye.note += "; no VkImage";
        return false;
    }
    const std::optional<ui_layer::ImageRecord> record = ui_vulkan::recordOf(image);
    if (!record) {
        eye.note +=
            "; a VkImage the layer did not prepare (created before the variable took effect, or another "
            "format)";
        return false;
    }
    const std::uint32_t bpp = ui_layer::motionBytesPerPixel(record->format);
    eye.note += format("; VkFormat %d", record->format);
    if (bpp == 0 || record->width != static_cast<std::uint32_t>(fields->width) ||
        record->height != static_cast<std::uint32_t>(fields->height)) {
        eye.note += "; format or size differ from the record";
        return false;
    }
    const std::optional<ui_vulkan::ImageState> state = ui_vulkan::stateOf(image);
    if (!state) {
        eye.note += "; layout not known yet";
        return false;
    }
    const auto layout = static_cast<VkImageLayout>(state->layout);
    eye.note += format("; layout %d", state->layout);
    if (!copyableLayout(layout)) {
        eye.note += " (not a layout a copy starts from)";
        return false;
    }
    if (record->sharingMode != VK_SHARING_MODE_CONCURRENT) {
        std::optional<std::uint32_t> lastFamily;
        {
            std::lock_guard lock(dev.queueMutex);
            const auto it = dev.queueFamilies.find(state->queue);
            if (it != dev.queueFamilies.end()) {
                lastFamily = it->second;
            }
        }
        if (!lastFamily || *lastFamily != family) {
            eye.note += format("; exclusive to queue family %d, copy on %u",
                               lastFamily ? static_cast<int>(*lastFamily) : -1, family);
            return false;
        }
    }
    const VkExtent2D extent{record->width, record->height};
    if (!ensureBuffer(dev, eye, VkDeviceSize{extent.width} * extent.height * bpp)) {
        eye.note += "; no host buffer";
        return false;
    }
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageMemoryBarrier before{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    before.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    before.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    before.oldLayout = layout;
    before.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    before.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before.image = image;
    before.subresourceRange = range;
    dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                              nullptr, 0, nullptr, 1, &before);
    EyeCapture::record(dev, cb, image, extent, eye.buffer);
    VkImageMemoryBarrier after = before;
    after.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    after.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    after.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    after.newLayout = layout; // where the game's own tracking has it
    dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                              nullptr, 0, nullptr, 1, &after);
    eye.taken = true;
    eye.format = record->format;
    eye.extent = extent;
    return true;
}

bool MotionCapture::record(DeviceData& dev, VkCommandBuffer cb, std::uint32_t family, bool eyeR) {
    if (!eyeR) {
        return false;
    }
    const bool once = !onceBase_.empty();
    const std::uint64_t pair = pairs_++;
    if ((!enabled() && !once) || state_ != State::Idle || writerBusy_->load() ||
        (!once && pair % every_ != 0)) {
        return false;
    }
    const bool left = recordEye(dev, cb, family, 0);
    const bool right = recordEye(dev, cb, family, 1);
    if (!left && !right) {
        if (++failures_ <= 10) {
            EVR_LOG("%s: pair %llu not captured: eye L: %s; eye R: %s", kTag,
                    static_cast<unsigned long long>(pair), eyes_[0].note.c_str(), eyes_[1].note.c_str());
        }
        return false;
    }
    state_ = State::Recorded;
    index_ = pair;
    currentBase_ = std::move(onceBase_);
    onceBase_.clear();
    return true;
}

void MotionCapture::submitted(std::uint64_t timelineValue) {
    if (state_ == State::Recorded) {
        state_ = State::Submitted;
        value_ = timelineValue;
    }
}

void MotionCapture::cancel() {
    if (state_ == State::Recorded) {
        state_ = State::Idle;
        if (onceBase_.empty()) {
            onceBase_ = std::move(currentBase_); // the next pair takes it
        }
        currentBase_.clear();
    }
}

void MotionCapture::poll(DeviceData& dev, std::uint64_t completedTimeline) {
    if (state_ != State::Submitted || completedTimeline < value_ || writerBusy_->load()) {
        return;
    }
    struct Out {
        bool taken = false;
        std::int32_t format = 0;
        VkExtent2D extent{};
        std::string note;
        std::vector<std::uint8_t> bytes;
    };
    auto outs = std::make_shared<std::array<Out, 2>>();
    for (std::size_t e = 0; e < 2; ++e) {
        Eye& eye = eyes_[e];
        Out& out = (*outs)[e];
        out.taken = eye.taken;
        out.note = eye.note;
        if (!eye.taken) {
            continue;
        }
        if (!eye.coherent) {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = eye.memory;
            range.size = VK_WHOLE_SIZE;
            dev.vk.InvalidateMappedMemoryRanges(dev.device, 1, &range);
        }
        out.format = eye.format;
        out.extent = eye.extent;
        const std::size_t size =
            std::size_t{eye.extent.width} * eye.extent.height * ui_layer::motionBytesPerPixel(eye.format);
        out.bytes.resize(size);
        std::memcpy(out.bytes.data(), eye.mapped, size);
    }
    state_ = State::Idle;
    std::wstring base = currentBase_;
    currentBase_.clear();
    const bool once = !base.empty();
    if (once) {
        base += L"-MV";
    } else {
        wchar_t name[96];
        swprintf_s(name, L"\\mv-%lu-p%06llu", GetCurrentProcessId(), static_cast<unsigned long long>(index_));
        base = directory_ + name;
    }
    const bool logIt = once || ++written_ <= 10;
    const std::uint64_t index = index_;
    writerBusy_->store(true);
    const auto write = [busy = writerBusy_, outs, base, logIt, index] {
        std::string text;
        bool ok = true;
        const char* names[2] = {"L", "R"};
        for (std::size_t e = 0; e < 2; ++e) {
            const Out& out = (*outs)[e];
            text += std::string("eye ") + names[e] + ": " + out.note + "\n";
            if (!out.taken) {
                text += std::string("eye ") + names[e] + ": NOT copied\n";
                continue;
            }
            const std::wstring path = base + (e == 0 ? L"-L.raw" : L"-R.raw");
            ok = writeFile(path, out.bytes.data(), out.bytes.size()) && ok;
            text += format("eye %s: %s, VkFormat %d, %ux%u, %zu bytes\n", names[e], narrow(path).c_str(),
                           out.format, out.extent.width, out.extent.height, out.bytes.size());
            const std::optional<ui_layer::MotionStats> s = ui_layer::motionStats(
                out.bytes.data(), out.bytes.size(), out.extent.width, out.extent.height, out.format);
            if (s) {
                text += std::string("eye ") + names[e] + " stats: " + ui_layer::describe(*s) + "\n";
            }
        }
        const std::wstring txt = base + L".txt";
        ok = writeFile(txt, text.data(), text.size()) && ok;
        if (logIt || !ok) {
            EVR_LOG("%s: capture %llu %s: %s", kTag, static_cast<unsigned long long>(index),
                    ok ? "written" : "NOT fully written", narrow(txt).c_str());
        }
        busy->store(false);
    };
    try {
        std::thread(write).detach();
    } catch (const std::exception&) {
        writerBusy_->store(false);
        EVR_LOG("%s: no thread for capture %llu; not saved", kTag, static_cast<unsigned long long>(index));
    }
}

void MotionCapture::destroy(DeviceData& dev) {
    for (Eye& eye : eyes_) {
        freeBuffer(dev, eye);
    }
    state_ = State::Idle;
}

} // namespace evr::vkcore
