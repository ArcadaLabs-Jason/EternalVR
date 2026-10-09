// The UI layer's presenter part (ETERNALVR_UI_LAYER=1, docs/rig-findings/ui-layer.md section 5): the
// game's GUI target is copied next to each head-tracked (or eye L) image into its ring slot's own shared
// image, carried to the headset with that image, and shown on a head-locked quad in front of the
// projection layer. While the quad shows, the engine's composite of the GUI into the eye images is
// skipped (ui_engine.cpp); menus and loading screens keep the game's own composite on the cinema screen.

#include "vkcore/presenter_impl.hpp"

#include "vkcore/controllers.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/taa_hooks.hpp"
#include "vkcore/ui_engine.hpp"
#include "vkcore/ui_vulkan.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

namespace evr::vkcore {

namespace {

// The GUI target is RGBA8 (FMT_RGBA8, VK_FORMAT_R8G8B8A8_UNORM), premultiplied, display-encoded; its bytes
// go into an sRGB swapchain image unchanged, as the eye images do.
constexpr VkFormat kUiVkFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kUiDxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kUiSwapchainFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
// The centred square left out of the GUI copy under hand aim, as a fraction of the target's height: the
// game's crosshair and the ability indicators around it.
constexpr float kCrosshairMask = 0.12f;

} // namespace

// ---- Worker: start-up and the shared images -----------------------------------------------------------

void XrPresenter::Impl::startUi() {
    const bool located = ui_engine::install(settings.ui.skipComposite);
    EVR_LOG("ui: UI layer %s; quad %.2f m wide, %.2f m ahead, %.2f m up, head-locked; GUI composite in the "
            "eyes %s",
            located ? "on" : "OFF (reason above)", settings.ui.widthMetres, settings.ui.distanceMetres,
            settings.ui.offsetYMetres,
            ui_engine::skipHookInstalled() ? "skipped while the quad shows" : "kept");
    const ui_layer::WristSettings& w = settings.ui.wrist;
    EVR_LOG("ui: HUD %s%s; wrist: %s (facing %.0f/%.0f deg, gaze %.0f/%.0f deg), %.2f m row at (%.3f, %.3f, "
            "%.3f), fade %.2f s %s, abilities %s",
            ui_layer::hudModeName(settings.ui.hud),
            settings.ui.hud == ui_layer::HudMode::Wrist    ? " (corner blocks on the off hand's wrist)"
            : settings.ui.hud == ui_layer::HudMode::Weapon ? " (ammo above the gun in the weapon hand)"
                                                           : "",
            w.always ? "always shown" : "shown while facing the head", w.showDegrees, w.hideDegrees,
            w.gazeShowDegrees, w.gazeHideDegrees, w.widthMetres, w.offset.x, w.offset.y, w.offset.z,
            w.fadeInSeconds, wrist.colorScaleBias ? "(colour scale)" : "(no colour scale: switched)",
            w.abilities ? "on" : "off");
    if (settings.ui.hud == ui_layer::HudMode::Weapon) {
        const ui_layer::WeaponHudSettings& g = settings.ui.weapon;
        EVR_LOG("ui: weapon HUD: %s, %.2f m wide at (%.3f, %.3f, %.3f) in the gun's frame, tilted %.0f deg, "
                "shown while facing the head (%.0f/%.0f deg)",
                g.vitals ? "ammo, health and armor" : "ammo (health and armor stay on the panel)",
                g.widthMetres, g.offset.x, g.offset.y, g.offset.z, g.tiltDegrees, g.showDegrees,
                g.hideDegrees);
    }
    std::wstring motion;
    if (ui_vulkan::motionCaptureRequested() && readEnv(L"ETERNALVR_CAPTURE_MOTION", motion)) {
        const auto setting = stereo_seq::parseCaptureSetting(motion);
        std::lock_guard lock(mutex);
        if (setting && taaRequested()) {
            motionCapture.configure(*setting);
        } else {
            EVR_LOG("motion: ETERNALVR_CAPTURE_MOTION needs <dir>[,<every N pairs>] and per-eye TAA "
                    "(ETERNALVR_STEREO_TAA); motion capture off");
        }
    }
    std::wstring text;
    if (located && readEnv(L"ETERNALVR_CAPTURE_UI", text) && !text.empty()) {
        const auto setting = stereo_seq::parseCaptureSetting(text);
        std::lock_guard lock(mutex);
        if (setting) {
            uiCapture.configure(*setting);
        } else {
            EVR_LOG("ui: ETERNALVR_CAPTURE_UI is not <dir>[,<every N captures>]; UI capture off");
        }
    }
}

bool XrPresenter::Impl::importUiImage(HANDLE handle, UiImage& ui) {
    const InstanceDispatch& ivk = dev.instance->vk;
    VkPhysicalDeviceExternalImageFormatInfo externalInfo{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
    externalInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
    VkPhysicalDeviceImageFormatInfo2 formatInfo{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
                                                &externalInfo};
    formatInfo.format = kUiVkFormat;
    formatInfo.type = VK_IMAGE_TYPE_2D;
    formatInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    formatInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkExternalImageFormatProperties externalProps{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
    VkImageFormatProperties2 formatProps{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, &externalProps};
    if (ivk.GetPhysicalDeviceImageFormatProperties2(dev.physicalDevice, &formatInfo, &formatProps) !=
            VK_SUCCESS ||
        !(externalProps.externalMemoryProperties.externalMemoryFeatures &
          VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)) {
        return false;
    }
    VkExternalMemoryImageCreateInfo externalCreate{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    externalCreate.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, &externalCreate};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = kUiVkFormat;
    imageInfo.extent = {uiExtent.width, uiExtent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (dev.vk.CreateImage(dev.device, &imageInfo, nullptr, &ui.image) != VK_SUCCESS) {
        return false;
    }
    VkMemoryRequirements req{};
    dev.vk.GetImageMemoryRequirements(dev.device, ui.image, &req);
    VkMemoryWin32HandlePropertiesKHR handleProps{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
    if (dev.vk.GetMemoryWin32HandlePropertiesKHR(dev.device,
                                                 VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT, handle,
                                                 &handleProps) != VK_SUCCESS) {
        return false;
    }
    VkPhysicalDeviceMemoryProperties memProps{};
    ivk.GetPhysicalDeviceMemoryProperties(dev.physicalDevice, &memProps);
    const std::uint32_t bits = req.memoryTypeBits & handleProps.memoryTypeBits;
    std::uint32_t typeIndex = UINT32_MAX;
    for (std::uint32_t t = 0; t < memProps.memoryTypeCount; ++t) {
        if ((bits & (1u << t)) && (typeIndex == UINT32_MAX || (memProps.memoryTypes[t].propertyFlags &
                                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))) {
            typeIndex = t;
        }
    }
    if (typeIndex == UINT32_MAX) {
        return false;
    }
    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.image = ui.image;
    VkImportMemoryWin32HandleInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
                                            &dedicated};
    import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
    import.handle = handle;
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &import};
    allocInfo.allocationSize = req.size;
    allocInfo.memoryTypeIndex = typeIndex;
    return dev.vk.AllocateMemory(dev.device, &allocInfo, nullptr, &ui.memory) == VK_SUCCESS &&
           dev.vk.BindImageMemory(dev.device, ui.image, ui.memory, 0) == VK_SUCCESS;
}

void XrPresenter::Impl::createUiImages() {
    uiReady = false;
    if (!settings.ui.enabled || !ui_engine::located()) {
        return;
    }
    uiExtent = gameExtent; // the GUI target has the output size, as the swapchain does
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = uiExtent.width;
    desc.Height = uiExtent.height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = kUiDxgiFormat;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    for (RingSlot& slot : ring) {
        HANDLE handle = nullptr;
        HRESULT hr = d3dDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc,
                                                        D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                        IID_PPV_ARGS(&slot.ui.resource));
        if (SUCCEEDED(hr)) {
            hr =
                d3dDevice->CreateSharedHandle(slot.ui.resource.Get(), nullptr, GENERIC_ALL, nullptr, &handle);
        }
        const bool ok = SUCCEEDED(hr) && importUiImage(handle, slot.ui);
        if (handle) {
            CloseHandle(handle);
        }
        if (!ok) {
            EVR_LOG("ui: the shared GUI images (%ux%u) could not be made (0x%08lx); UI layer off",
                    uiExtent.width, uiExtent.height, hr);
            destroyUiImages();
            return;
        }
    }
    uiReady = true;
    EVR_LOG("ui: %u shared GUI image(s) of %ux%u", kRingSize, uiExtent.width, uiExtent.height);
}

void XrPresenter::Impl::destroyUiImages() {
    for (RingSlot& slot : ring) {
        if (slot.ui.image) {
            dev.vk.DestroyImage(dev.device, slot.ui.image, nullptr);
            slot.ui.image = VK_NULL_HANDLE;
        }
        if (slot.ui.memory) {
            dev.vk.FreeMemory(dev.device, slot.ui.memory, nullptr);
            slot.ui.memory = VK_NULL_HANDLE;
        }
        slot.ui.resource.Reset();
        slot.ui.written = false;
    }
    uiReady = false;
    uiCapture.destroy(dev);
    uiBackdrop.destroy(dev);
    motionCapture.destroy(dev);
}

// ---- Present hook (render thread, under `mutex`): the GUI target into the slot ------------------------

bool XrPresenter::Impl::recordUiCopy(VkCommandBuffer cb,
                                     std::uint32_t family,
                                     RingSlot& slot,
                                     const SwapchainState* panel) {
    if (!uiReady || !slot.ui.image) {
        return false;
    }
    const auto fail = [this](ui_layer::TargetCheck why) {
        ++uiNotCaptured;
        uiLastCheck = why;
        return false;
    };
    const std::optional<ui_layer::GuiImageFields> fields = ui_engine::readTarget();
    if (!fields) {
        return fail(ui_layer::TargetCheck::NoVkImage);
    }
    const auto image = reinterpret_cast<VkImage>(fields->vkImage);
    const std::optional<ui_layer::ImageRecord> record = ui_vulkan::recordOf(image);
    if (record) {
        ui_vulkan::watch(image);
    }
    const std::optional<ui_vulkan::ImageState> state = ui_vulkan::stateOf(image);
    std::optional<std::uint32_t> lastFamily;
    if (state) {
        std::lock_guard lock(dev.queueMutex);
        const auto it = dev.queueFamilies.find(state->queue);
        if (it != dev.queueFamilies.end()) {
            lastFamily = it->second;
        }
    }
    ui_layer::TargetCheck check = ui_layer::checkTarget(
        *fields, record ? &*record : nullptr,
        state ? std::optional<std::int32_t>(state->layout) : std::nullopt, lastFamily, family);
    if (check == ui_layer::TargetCheck::Ok &&
        (static_cast<std::uint32_t>(fields->width) != uiExtent.width ||
         static_cast<std::uint32_t>(fields->height) != uiExtent.height)) {
        check = ui_layer::TargetCheck::SizeMismatch; // the shared images have the swapchain's size
    }
    if (check != ui_layer::TargetCheck::Ok) {
        return fail(check);
    }
    const VkImageLayout layout = state->layout;
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    std::array<VkImageMemoryBarrier, 2> before{};
    // The GUI target, last written by the game's GUI pass (earlier on this queue or covered by the
    // present's wait semaphores), moves to TRANSFER_SRC for the copy and back afterwards.
    before[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    before[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    before[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    before[0].oldLayout = layout;
    before[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    before[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before[0].image = image;
    before[0].subresourceRange = range;
    before[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    before[1].srcAccessMask = 0;
    before[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    before[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    before[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    before[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    before[1].dstQueueFamilyIndex = family;
    before[1].image = slot.ui.image;
    before[1].subresourceRange = range;
    dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                              nullptr, 0, nullptr, static_cast<std::uint32_t>(before.size()), before.data());
    // Under hand aim the game's crosshair (the centre of the target) marks the head's ray, not the gun's:
    // it is left out (cleared) and the reticle quad marks the hand's ray instead. Not while a menu is up,
    // nor while the target holds a full-screen backdrop (a menu's, a cinematic's fade): the square would
    // be a see-through hole in it. The backdrop test reads the pixels around the square of earlier copies.
    // The crosshair stays out with the reticle off (ETERNALVR_UI_RETICLE=0): it would still mark the head.
    const bool handAim = controllers::weaponAimSpace() != XR_NULL_HANDLE;
    const bool menu = menuUp.load(std::memory_order_relaxed);
    const bool backdrop = uiBackdrop.backdrop();
    const bool mask = handAim && !menu && !backdrop;
    if (handAim && menu) {
        ++uiUnmaskedMenu;
    } else if (handAim && backdrop) {
        ++uiUnmaskedBackdrop;
    }
    const std::vector<ui_layer::PixelRect> rects =
        ui_layer::copyRegionsWithoutCentre(uiExtent.width, uiExtent.height, mask ? kCrosshairMask : 0.0f);
    if (rects.size() > 1) {
        const VkClearColorValue clear{};
        dev.vk.CmdClearColorImage(cb, slot.ui.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
        VkMemoryBarrier cleared{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        cleared.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        cleared.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1,
                                  &cleared, 0, nullptr, 0, nullptr);
    }
    std::vector<VkImageCopy> regions;
    for (const ui_layer::PixelRect& r : rects) {
        VkImageCopy region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.srcOffset = {r.x, r.y, 0};
        region.dstOffset = {r.x, r.y, 0};
        region.extent = {r.width, r.height, 1};
        regions.push_back(region);
    }
    dev.vk.CmdCopyImage(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot.ui.image,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<std::uint32_t>(regions.size()),
                        regions.data());
    if (panel) {
        // A menu is up: the desktop window shows what the panel shows (presenter_mirror.hpp).
        mirror.keepPanel(dev, cb, family, image, uiExtent, panel->format, panel->extent);
    }
    uiBackdrop.record(dev, cb, image, uiExtent, kCrosshairMask);
    if (const VkBuffer buffer = uiCapture.bufferFor(dev, uiCaptures, uiExtent)) {
        EyeCapture::record(dev, cb, image, uiExtent, buffer);
    }
    std::array<VkImageMemoryBarrier, 2> after{};
    after[0] = before[0];
    after[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    after[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    after[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    after[0].newLayout = layout; // where the game's own tracking has it
    after[1] = before[1];
    after[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    after[1].dstAccessMask = 0;
    after[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    after[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    after[1].srcQueueFamilyIndex = family;
    after[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                              nullptr, 0, nullptr, static_cast<std::uint32_t>(after.size()), after.data());
    ++uiCaptures;
    return true;
}

void XrPresenter::Impl::logUiStats() {
    const ULONGLONG now = GetTickCount64();
    if (!settings.ui.enabled || now - lastUiStatsTicks < 10000) {
        return;
    }
    lastUiStatsTicks = now;
    const ui_engine::Counters e = ui_engine::counters();
    const ui_vulkan::Counters v = ui_vulkan::counters();
    EVR_LOG(
        "ui: %llu GUI capture(s), %llu not captured (last reason: %s); %llu image(s) prepared, GUI target "
        "changed %llu time(s) (%llu not followed); composites %llu, %llu without the GUI; %llu target read "
        "failure(s); skip %s; backdrop test: %llu reading(s), %llu backdrop(s) (%s now), hand-aim copies not "
        "masked: %llu for a backdrop, %llu for a menu",
        static_cast<unsigned long long>(uiCaptures), static_cast<unsigned long long>(uiNotCaptured),
        ui_layer::toString(uiLastCheck), static_cast<unsigned long long>(v.candidates),
        static_cast<unsigned long long>(v.watchChanges),
        static_cast<unsigned long long>(v.candidatesNotFollowed),
        static_cast<unsigned long long>(e.composites), static_cast<unsigned long long>(e.skipped),
        static_cast<unsigned long long>(e.readFailures), ui_engine::skipComposite() ? "on" : "off",
        static_cast<unsigned long long>(uiBackdrop.readings()),
        static_cast<unsigned long long>(uiBackdrop.backdropsSeen()), uiBackdrop.backdrop() ? "up" : "none",
        static_cast<unsigned long long>(uiUnmaskedBackdrop), static_cast<unsigned long long>(uiUnmaskedMenu));
}

// ---- Worker: the UI swapchain, its copy and the quad ------------------------------------------------

bool XrPresenter::Impl::createUiSwapchain() {
    if (std::find(xrFormats.begin(), xrFormats.end(), static_cast<std::int64_t>(kUiSwapchainFormat)) ==
        xrFormats.end()) {
        EVR_LOG("ui: the runtime offers no R8G8B8A8 sRGB swapchain; UI layer off");
        return false;
    }
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    info.format = kUiSwapchainFormat;
    info.sampleCount = 1;
    info.width = uiExtent.width;
    info.height = uiExtent.height;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    EVR_XR_CHECK(xr.xrCreateSwapchain(session, &info, &uiSwapchain));
    std::uint32_t count = 0;
    EVR_XR_CHECK(xr.xrEnumerateSwapchainImages(uiSwapchain, 0, &count, nullptr));
    std::vector<XrSwapchainImageD3D12KHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
    EVR_XR_CHECK(xr.xrEnumerateSwapchainImages(uiSwapchain, count, &count,
                                               reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())));
    uiXrImages.clear();
    for (const auto& image : images) {
        const D3D12_RESOURCE_DESC d = image.texture->GetDesc();
        if (d.Width != uiExtent.width || d.Height != uiExtent.height) {
            // A copy into it would fail and remove the device (presenter_xr.cpp, createXrSwapchain).
            EVR_LOG("ui: the runtime's UI swapchain image is %llux%u, not %ux%u; UI layer off",
                    static_cast<unsigned long long>(d.Width), d.Height, uiExtent.width, uiExtent.height);
            xr.xrDestroySwapchain(uiSwapchain);
            uiSwapchain = XR_NULL_HANDLE;
            uiXrImages.clear();
            return false;
        }
        uiXrImages.push_back(image.texture);
    }
    EVR_LOG("ui: UI swapchain %ux%u, %u image(s)", uiExtent.width, uiExtent.height, count);
    if (settings.ui.removeWash) {
        const char* why = "";
        if (uiWash.create(d3dDevice.Get(), why)) {
            EVR_LOG("ui: the full-screen additive wash (low-health vignette) is removed from the HUD quad");
        } else {
            EVR_LOG("ui: wash filter off (%s); the GUI image is copied as the game drew it", why);
        }
    }
    return true;
}

bool XrPresenter::Impl::acquireUiXrImage() {
    if (!uiReady || uiFailed) {
        return false;
    }
    if (!uiSwapchain && !createUiSwapchain()) {
        uiFailed = true;
        return false;
    }
    if (uiAcquired < 0) {
        std::uint32_t index = 0;
        if (XR_FAILED(xr.xrAcquireSwapchainImage(uiSwapchain, nullptr, &index))) {
            return false;
        }
        uiAcquired = index;
        uiAcquiredWaited = false;
    }
    if (!uiAcquiredWaited) {
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = kSwapchainWaitTimeout;
        if (timedCall(inRuntime.waitImage, xr.xrWaitSwapchainImage, uiSwapchain, &wait) != XR_SUCCESS) {
            return false; // a timeout keeps the image acquired for the next frame (T-081)
        }
        uiAcquiredWaited = true;
    }
    return true;
}

void XrPresenter::Impl::recordUiXrCopy(RingSlot& slot) {
    ID3D12Resource* target = uiXrImages[static_cast<std::size_t>(uiAcquired)];
    std::array<D3D12_RESOURCE_BARRIER, 2> barriers{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = slot.ui.resource.Get();
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = target;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    // The HUD (not a menu, whose panel shows the same image) without the full-screen additive wash: the
    // filtered image is copied instead of the shared one (ui_wash.hpp).
    ID3D12Resource* filtered =
        !menuUp.load(std::memory_order_relaxed)
            ? uiWash.record(d3dList.Get(), slot.ui.resource.Get(), uiExtent.width, uiExtent.height)
            : nullptr;
    d3dList->ResourceBarrier(filtered ? 1u : static_cast<UINT>(barriers.size()),
                             filtered ? &barriers[1] : barriers.data());
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = target;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = filtered ? filtered : slot.ui.resource.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    d3dList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    for (auto& b : barriers) {
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    }
    d3dList->ResourceBarrier(filtered ? 1u : static_cast<UINT>(barriers.size()),
                             filtered ? &barriers[1] : barriers.data());
    if (filtered) {
        uiWash.afterCopy(d3dList.Get());
        if (!uiWashLogged) {
            uiWashLogged = true;
            EVR_LOG("ui: first GUI image filtered for the wash");
        }
    }
    uiCopyPending = true;
}

void XrPresenter::Impl::finishUiXrCopy() {
    if (!uiCopyPending) {
        return;
    }
    uiCopyPending = false;
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xr.xrReleaseSwapchainImage(uiSwapchain, &release);
    uiAcquired = -1;
    uiAcquiredWaited = false;
    if (!uiHasImage) {
        EVR_LOG("ui: first GUI image on the quad");
    }
    uiHasImage = true;
    uiShownQpc = qpcNow();
    ++uiXrCopies;
}

bool XrPresenter::Impl::uiFresh() const {
    return uiHasImage && uiSwapchain && qpcSeconds(qpcNow() - uiShownQpc) <= kViewStaleSeconds;
}

bool XrPresenter::Impl::fillUiQuad(XrCompositionLayerQuad& quad) {
    // Only a fresh GUI image: a capture that stopped (menus, a failed check) hides the quad, and the
    // composite comes back with it.
    if (!uiFresh()) {
        return false;
    }
    // The 16:9 band the game's HUD and menus are laid out in (the rows above and below it on a near-square
    // eye image are empty).
    const ui_layer::PixelRect content = settings.ui.wideCrop
                                            ? ui_layer::wideContentRect(uiExtent.width, uiExtent.height)
                                            : ui_layer::PixelRect{0, 0, uiExtent.width, uiExtent.height};
    const auto size = ui_layer::panelSize(settings.ui.widthMetres, content.width, content.height);
    if (!size) {
        return false;
    }
    // Premultiplied alpha, as the game draws the target (OpenXR's default without the unpremultiplied bit).
    quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    quad.space = viewSpace;
    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain = uiSwapchain;
    quad.subImage.imageRect = {
        {content.x, content.y},
        {static_cast<std::int32_t>(content.width), static_cast<std::int32_t>(content.height)}};
    quad.subImage.imageArrayIndex = 0;
    quad.pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, settings.ui.offsetYMetres, -settings.ui.distanceMetres}};
    quad.size = {size->width, size->height};
    return true;
}

void XrPresenter::Impl::destroyUiXrObjects() {
    if (uiSwapchain) {
        xr.xrDestroySwapchain(uiSwapchain); // valid with an image still acquired
        uiSwapchain = XR_NULL_HANDLE;
    }
    uiXrImages.clear();
    uiAcquired = -1;
    uiAcquiredWaited = false;
    uiCopyPending = false;
    uiHasImage = false;
    uiFailed = false;
    if (reticleSwapchain) {
        xr.xrDestroySwapchain(reticleSwapchain);
        reticleSwapchain = XR_NULL_HANDLE;
    }
    reticleFailed = false;
    uiWash.destroy();
    destroyMenuXrObjects();
}

} // namespace evr::vkcore
