// The game's menus and HUD at full rate under foveated rendering (vrs_gui.hpp).

#include "vkcore/vrs_gui.hpp"

#include "features/foveation/attachment_images.hpp"
#include "vkcore/dump_format.hpp"
#include "vkcore/log.hpp"
#include "vkcore/ui_vulkan.hpp"
#include "vkcore/vrs_nv.hpp"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace evr::vkcore::vrs_gui {

namespace {

using DispatchKey = void*;
using Handle = foveation::AttachmentImages::Handle;

template <typename T>
DispatchKey keyOf(T handle) {
    return *reinterpret_cast<void**>(handle);
}

template <typename T>
Handle handleValue(T handle) {
    return reinterpret_cast<Handle>(handle);
}

struct GuiDevice {
    bool follow = false; // views and framebuffers are followed
    PFN_vkCreateImageView createImageView = nullptr;
    PFN_vkDestroyImageView destroyImageView = nullptr;
    PFN_vkCreateFramebuffer createFramebuffer = nullptr;
    PFN_vkDestroyFramebuffer destroyFramebuffer = nullptr;
    std::shared_mutex mutex;
    foveation::AttachmentImages attachments;
    bool viewsFull = false; // logged once
    bool framebuffersFull = false;
};

// Allocated once and never destroyed (see layer_entry.cpp: no teardown at process exit).
std::shared_mutex& g_devicesMutex = *new std::shared_mutex;
auto& g_devices = *new std::unordered_map<DispatchKey, GuiDevice*>;

GuiDevice* deviceOf(DispatchKey key) {
    std::shared_lock lock(g_devicesMutex);
    const auto it = g_devices.find(key);
    return it == g_devices.end() ? nullptr : it->second;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateImageView(VkDevice device,
                                               const VkImageViewCreateInfo* pCreateInfo,
                                               const VkAllocationCallbacks* pAllocator,
                                               VkImageView* pView) {
    GuiDevice* d = deviceOf(keyOf(device));
    const VkResult result = d->createImageView(device, pCreateInfo, pAllocator, pView);
    if (result == VK_SUCCESS && d->follow) {
        std::unique_lock lock(d->mutex);
        if (!d->attachments.addView(handleValue(*pView), handleValue(pCreateInfo->image)) && !d->viewsFull) {
            d->viewsFull = true;
            EVR_LOG("vrs: %zu image views followed; render passes into newer ones count as eye passes",
                    d->attachments.viewCount());
        }
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroyImageView(VkDevice device,
                                            VkImageView view,
                                            const VkAllocationCallbacks* pAllocator) {
    GuiDevice* d = deviceOf(keyOf(device));
    if (d->follow && view != VK_NULL_HANDLE) {
        std::unique_lock lock(d->mutex);
        d->attachments.removeView(handleValue(view));
    }
    d->destroyImageView(device, view, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateFramebuffer(VkDevice device,
                                                 const VkFramebufferCreateInfo* pCreateInfo,
                                                 const VkAllocationCallbacks* pAllocator,
                                                 VkFramebuffer* pFramebuffer) {
    GuiDevice* d = deviceOf(keyOf(device));
    const VkResult result = d->createFramebuffer(device, pCreateInfo, pAllocator, pFramebuffer);
    // An imageless framebuffer has no views yet: each render pass names them (drawsGuiTarget).
    if (result != VK_SUCCESS || !d->follow || (pCreateInfo->flags & VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT)) {
        return result;
    }
    std::vector<Handle> views(pCreateInfo->attachmentCount);
    for (std::uint32_t i = 0; i < pCreateInfo->attachmentCount; ++i) {
        views[i] = handleValue(pCreateInfo->pAttachments[i]);
    }
    std::unique_lock lock(d->mutex);
    if (!d->attachments.addFramebuffer(handleValue(*pFramebuffer), views) && !d->framebuffersFull) {
        d->framebuffersFull = true;
        EVR_LOG("vrs: %zu framebuffers followed; render passes into newer ones count as eye passes",
                d->attachments.framebufferCount());
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroyFramebuffer(VkDevice device,
                                              VkFramebuffer framebuffer,
                                              const VkAllocationCallbacks* pAllocator) {
    GuiDevice* d = deviceOf(keyOf(device));
    if (d->follow && framebuffer != VK_NULL_HANDLE) {
        std::unique_lock lock(d->mutex);
        d->attachments.removeFramebuffer(handleValue(framebuffer));
    }
    d->destroyFramebuffer(device, framebuffer, pAllocator);
}

} // namespace

void onDeviceCreated(DeviceData& data, bool enabled) {
    if (!vrs_nv::wanted()) {
        return;
    }
    auto* d = new GuiDevice; // kept for the process's life, like the other device tables
    const auto next = [&](const char* name) {
        return data.nextGetDeviceProcAddr(data.device, name);
    };
    d->createImageView = reinterpret_cast<PFN_vkCreateImageView>(next("vkCreateImageView"));
    d->destroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(next("vkDestroyImageView"));
    d->createFramebuffer = reinterpret_cast<PFN_vkCreateFramebuffer>(next("vkCreateFramebuffer"));
    d->destroyFramebuffer = reinterpret_cast<PFN_vkDestroyFramebuffer>(next("vkDestroyFramebuffer"));
    d->follow = enabled && ui_vulkan::enabled();
    if (enabled) {
        EVR_LOG("vrs: %s", d->follow ? "render passes into the GUI target keep full rate (menus and HUD)"
                                     : "the UI layer is off: render passes into the GUI target are foveated");
    }
    std::unique_lock lock(g_devicesMutex);
    g_devices[keyOf(data.device)] = d;
}

bool drawsGuiTarget(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo& begin) {
    const VkImage gui = ui_vulkan::guiTarget();
    if (gui == VK_NULL_HANDLE) {
        return false;
    }
    GuiDevice* d = deviceOf(keyOf(commandBuffer));
    if (!d || !d->follow) {
        return false;
    }
    const auto* imageless = dump::findInChain<VkRenderPassAttachmentBeginInfo>(
        begin.pNext, VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO);
    std::shared_lock lock(d->mutex);
    if (!imageless) {
        return d->attachments.draws(handleValue(begin.framebuffer), handleValue(gui));
    }
    for (std::uint32_t i = 0; i < imageless->attachmentCount; ++i) {
        if (d->attachments.imageOf(handleValue(imageless->pAttachments[i])) == handleValue(gui)) {
            return true;
        }
    }
    return false;
}

PFN_vkVoidFunction findHook(const char* name) {
    if (!vrs_nv::wanted()) {
        return nullptr;
    }
#define EVR_VRS_GUI_HOOK(fn)                                                                                 \
    if (std::strcmp(name, "vk" #fn) == 0) {                                                                  \
        return reinterpret_cast<PFN_vkVoidFunction>(&fn);                                                    \
    }
    EVR_VRS_GUI_HOOK(CreateImageView)
    EVR_VRS_GUI_HOOK(DestroyImageView)
    EVR_VRS_GUI_HOOK(CreateFramebuffer)
    EVR_VRS_GUI_HOOK(DestroyFramebuffer)
#undef EVR_VRS_GUI_HOOK
    return nullptr;
}

} // namespace evr::vkcore::vrs_gui
