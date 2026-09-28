// The shared ring: D3D12 images and fence created with NT handles, imported into the game's Vulkan
// device (T-040, T-080), and rebuilt when the game's swapchain changes shape.

#include "vkcore/presenter_impl.hpp"
#include "vkcore/status_file.hpp"

#include <algorithm>
#include <array>

namespace evr::vkcore {

bool XrPresenter::Impl::importRing(const std::array<HANDLE, kRingSize>& imageHandles, HANDLE fenceHandle) {
    const InstanceDispatch& ivk = dev.instance->vk;

    // Import support for D3D12 resources in this format and usage (T-080).
    VkPhysicalDeviceExternalImageFormatInfo externalInfo{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
    externalInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
    VkPhysicalDeviceImageFormatInfo2 formatInfo{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
                                                &externalInfo};
    formatInfo.format = ringFormat;
    formatInfo.type = VK_IMAGE_TYPE_2D;
    formatInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    formatInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkExternalImageFormatProperties externalProps{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
    VkImageFormatProperties2 formatProps{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, &externalProps};
    VkResult r = ivk.GetPhysicalDeviceImageFormatProperties2(dev.physicalDevice, &formatInfo, &formatProps);
    const VkExternalMemoryFeatureFlags memFeatures =
        externalProps.externalMemoryProperties.externalMemoryFeatures;
    EVR_LOG("presenter: D3D12 resource import for format %d: result %d, features 0x%x", ringFormat, r,
            memFeatures);
    if (r != VK_SUCCESS || !(memFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)) {
        return false;
    }

    VkPhysicalDeviceMemoryProperties memProps{};
    ivk.GetPhysicalDeviceMemoryProperties(dev.physicalDevice, &memProps);

    for (std::uint32_t i = 0; i < kRingSize; ++i) {
        RingSlot& slot = ring[i];
        VkExternalMemoryImageCreateInfo externalCreate{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
        externalCreate.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, &externalCreate};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = ringFormat;
        imageInfo.extent = {ringExtent.width, ringExtent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        r = dev.vk.CreateImage(dev.device, &imageInfo, nullptr, &slot.image);
        if (r != VK_SUCCESS) {
            EVR_LOG("presenter: vkCreateImage for ring slot %u failed: %d", i, r);
            return false;
        }
        VkMemoryRequirements req{};
        dev.vk.GetImageMemoryRequirements(dev.device, slot.image, &req);
        VkMemoryWin32HandlePropertiesKHR handleProps{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
        r = dev.vk.GetMemoryWin32HandlePropertiesKHR(
            dev.device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT, imageHandles[i], &handleProps);
        if (r != VK_SUCCESS) {
            EVR_LOG("presenter: vkGetMemoryWin32HandlePropertiesKHR failed: %d", r);
            return false;
        }
        const std::uint32_t bits = req.memoryTypeBits & handleProps.memoryTypeBits;
        std::uint32_t typeIndex = UINT32_MAX;
        for (std::uint32_t t = 0; t < memProps.memoryTypeCount; ++t) {
            if (!(bits & (1u << t))) {
                continue;
            }
            if (typeIndex == UINT32_MAX ||
                (memProps.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                typeIndex = t;
                if (memProps.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
                    break;
                }
            }
        }
        if (typeIndex == UINT32_MAX) {
            EVR_LOG("presenter: no memory type for the ring (image bits 0x%x, handle bits 0x%x)",
                    req.memoryTypeBits, handleProps.memoryTypeBits);
            return false;
        }
        VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
        dedicated.image = slot.image;
        VkImportMemoryWin32HandleInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
                                                &dedicated};
        import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
        import.handle = imageHandles[i];
        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &import};
        allocInfo.allocationSize = req.size;
        allocInfo.memoryTypeIndex = typeIndex;
        r = dev.vk.AllocateMemory(dev.device, &allocInfo, nullptr, &slot.memory);
        if (r != VK_SUCCESS) {
            EVR_LOG("presenter: importing ring slot %u failed: %d", i, r);
            return false;
        }
        r = dev.vk.BindImageMemory(dev.device, slot.image, slot.memory, 0);
        if (r != VK_SUCCESS) {
            EVR_LOG("presenter: binding ring slot %u failed: %d", i, r);
            return false;
        }
    }

    if (!fenceHandle) {
        EVR_LOG("presenter: ring of %u re-imported (%ux%u format %d)", kRingSize, ringExtent.width,
                ringExtent.height, ringFormat);
        return true; // the timeline from the first import stays
    }
    // The shared D3D12 fence becomes a timeline semaphore.
    VkSemaphoreTypeCreateInfo typeInfo{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    typeInfo.initialValue = 0;
    VkPhysicalDeviceExternalSemaphoreInfo semInfo{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO,
                                                  &typeInfo};
    semInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
    VkExternalSemaphoreProperties semProps{VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES};
    ivk.GetPhysicalDeviceExternalSemaphoreProperties(dev.physicalDevice, &semInfo, &semProps);
    EVR_LOG("presenter: D3D12 fence import features 0x%x", semProps.externalSemaphoreFeatures);
    if (!(semProps.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT)) {
        return false;
    }
    VkSemaphoreCreateInfo createInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &typeInfo};
    r = dev.vk.CreateSemaphore(dev.device, &createInfo, nullptr, &timeline);
    if (r != VK_SUCCESS) {
        EVR_LOG("presenter: timeline vkCreateSemaphore failed: %d", r);
        return false;
    }
    VkImportSemaphoreWin32HandleInfoKHR importSem{VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR};
    importSem.semaphore = timeline;
    importSem.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
    importSem.handle = fenceHandle;
    r = dev.vk.ImportSemaphoreWin32HandleKHR(dev.device, &importSem);
    if (r != VK_SUCCESS) {
        EVR_LOG("presenter: importing the shared fence failed: %d", r);
        return false;
    }
    EVR_LOG("presenter: ring of %u imported (%ux%u format %d)", kRingSize, ringExtent.width,
            ringExtent.height, ringFormat);
    return true;
}

void XrPresenter::Impl::destroyVulkanObjects() {
    retiredSemaphores.drain([this](VkSemaphore s) { dev.vk.DestroySemaphore(dev.device, s, nullptr); });
    for (auto& [handle, sc] : swapchains) {
        for (VkSemaphore s : sc.presentSemaphores) {
            dev.vk.DestroySemaphore(dev.device, s, nullptr);
        }
    }
    swapchains.clear();
    for (auto& [family, fc] : commands) {
        dev.vk.DestroyCommandPool(dev.device, fc.pool, nullptr);
    }
    commands.clear();
    for (RingSlot& slot : ring) {
        if (slot.image) {
            dev.vk.DestroyImage(dev.device, slot.image, nullptr);
            slot.image = VK_NULL_HANDLE;
        }
        if (slot.memory) {
            dev.vk.FreeMemory(dev.device, slot.memory, nullptr);
            slot.memory = VK_NULL_HANDLE;
        }
    }
    if (timeline) {
        dev.vk.DestroySemaphore(dev.device, timeline, nullptr);
        timeline = VK_NULL_HANDLE;
    }
    capture.destroy(dev);
    mirror.destroy(dev);
    destroyUiImages();
}

bool XrPresenter::Impl::createRing() {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = ringExtent.width;
    desc.Height = ringExtent.height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format =
        ringFormat == VK_FORMAT_R8G8B8A8_UNORM ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    std::array<HANDLE, kRingSize> handles{};
    HANDLE fenceHandle = nullptr;
    auto closeHandles = [&] {
        for (HANDLE& h : handles) {
            if (h) {
                CloseHandle(h);
                h = nullptr;
            }
        }
        if (fenceHandle) {
            CloseHandle(fenceHandle);
            fenceHandle = nullptr;
        }
    };
    for (std::uint32_t i = 0; i < kRingSize; ++i) {
        HRESULT hr = d3dDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc,
                                                        D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                        IID_PPV_ARGS(&ring[i].resource));
        if (SUCCEEDED(hr)) {
            hr = d3dDevice->CreateSharedHandle(ring[i].resource.Get(), nullptr, GENERIC_ALL, nullptr,
                                               &handles[i]);
        }
        if (FAILED(hr)) {
            EVR_LOG("d3d12: shared ring image %u failed: 0x%08lx", i, hr);
            closeHandles();
            return false;
        }
    }
    if (!sharedFence) {
        HRESULT hr = d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&sharedFence));
        if (SUCCEEDED(hr)) {
            hr =
                d3dDevice->CreateSharedHandle(sharedFence.Get(), nullptr, GENERIC_ALL, nullptr, &fenceHandle);
        }
        if (FAILED(hr)) {
            EVR_LOG("d3d12: shared fence failed: 0x%08lx", hr);
            closeHandles();
            return false;
        }
    }
    // Importing an NT handle does not transfer ownership, so ours are closed afterwards either way.
    const bool ok = !stop.load() && importRing(handles, fenceHandle);
    closeHandles();
    if (ok) {
        createUiImages(); // UI layer (without it the ring alone serves)
    }
    return ok;
}

void XrPresenter::Impl::recreateRing() {
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::uint64_t lastWrite = 0;
    {
        std::lock_guard lock(mutex);
        ringReady.store(false); // presents pass through from here on
        resizeRequested.store(false);
        extent = requestedExtent;
        format = requestedFormat;
        lastWrite = timelineValue;
    }
    // Every copy into the old ring, and every D3D12 copy out of it, must finish before it goes.
    const auto waitFence = [this](ID3D12Fence* fence, std::uint64_t value) {
        if (fence->GetCompletedValue() >= value) {
            return true;
        }
        fence->SetEventOnCompletion(value, copyEvent);
        return WaitForSingleObject(copyEvent, 2000) == WAIT_OBJECT_0;
    };
    if (!waitFence(sharedFence.Get(), lastWrite) || !waitFence(copyFence.Get(), copyFenceValue)) {
        EVR_LOG("presenter: copies into the old ring did not finish within 2 s; the headset keeps the last "
                "image");
        return;
    }
    {
        std::lock_guard lock(mutex);
        if (stop.load()) {
            return; // shutdown has begun (and freed the ring); the game's device may be gone
        }
        for (RingSlot& slot : ring) {
            if (slot.image) {
                dev.vk.DestroyImage(dev.device, slot.image, nullptr);
                slot.image = VK_NULL_HANDLE;
            }
            if (slot.memory) {
                dev.vk.FreeMemory(dev.device, slot.memory, nullptr);
                slot.memory = VK_NULL_HANDLE;
            }
            slot.resource.Reset();
            slot.state.store(kSlotFree);
            slot.hasView = false;
        }
        destroyUiImages();
        // A Route S pair half-written into the old ring is gone with it.
        if (pairing.pending()) {
            pairing.leftNotStored();
        }
        pendingSlot = kRingSize;
        capture.cancel();
        gameExtent = extent;
        gameFormat = format;
        setRingExtent(extent);
    }
    // The new XR swapchain is made before the old one goes: a runtime may hand a new swapchain the images of
    // one just destroyed (the OpenXR Simulator does), which createXrSwapchain refuses.
    const XrSwapchain oldSwapchain = xrSwapchain;
    xrSwapchain = XR_NULL_HANDLE;
    xrImages.clear();
    destroyUiXrObjects();
    acquiredIndex = -1;
    acquiredWaited = false;
    copyStalled = false;
    hasImage = false;
    shownHasView = false;
    lastConsumed = latest.load() >> 2; // the old ring's contents are gone
    const bool made = createXrSwapchain();
    if (oldSwapchain) {
        xr.xrDestroySwapchain(oldSwapchain); // valid with an image still acquired
    }
    if (!made || !createRing()) {
        status::flat("the images for the headset could not be recreated after a size change");
        EVR_LOG("presenter: rebuilding the ring for %ux%u failed; the game runs flat", extent.width,
                extent.height);
        return;
    }
    std::lock_guard lock(mutex);
    ringReady.store(true);
    EVR_LOG("presenter: ring rebuilt for %ux%u format %d (D3D12 device 0x%08lx)", ringExtent.width,
            ringExtent.height, ringFormat, static_cast<unsigned long>(d3dDevice->GetDeviceRemovedReason()));
    requestRebuildIfStale();
}

std::uint32_t XrPresenter::Impl::acquireFreeSlot(const FamilyCommands& fc, std::uint64_t completed) {
    // With two eyes per slot, never the newest published slot: the worker may not have taken it yet, and a
    // left half written into it would mix with the pair it holds.
    const auto newest = static_cast<std::uint32_t>(latest.load() & 3u);
    const bool skipNewest = ringEyes == 2 && (latest.load() >> 2) != 0;
    for (std::uint32_t n = 0; n < kRingSize; ++n) {
        const std::uint32_t candidate = (nextSlot + n) % kRingSize;
        if (skipNewest && candidate == newest) {
            continue;
        }
        RingSlot& slot = ring[candidate];
        int expected = kSlotFree;
        if (!slot.state.compare_exchange_strong(expected, kSlotWriting)) {
            continue;
        }
        if (completed < slot.value.load() || completed < fc.lastValue[candidate * 2] ||
            completed < fc.lastValue[candidate * 2 + 1]) {
            slot.state.store(kSlotFree);
            continue;
        }
        nextSlot = (candidate + 1) % kRingSize;
        return candidate;
    }
    return kRingSize;
}

void XrPresenter::Impl::publishSlot(std::uint32_t slotIndex, std::uint64_t value) {
    RingSlot& slot = ring[slotIndex];
    slot.value.store(value);
    slot.state.store(kSlotFree);
    latest.store((value << 2) | slotIndex);
    ++framesCopied;
}

void XrPresenter::Impl::setRingExtent(VkExtent2D game) {
    // Each eye's image is the game's (T-031: the render size still follows the window); a Route S ring
    // holds two of them side by side.
    const std::uint32_t eyes = ringEyes == 2 ? 2 : 1;
    eyeExtent.width = std::min(game.width, maxSwapchainWidth / eyes);
    eyeExtent.height = std::min(game.height, maxSwapchainHeight);
    ringExtent = {eyeExtent.width * eyes, eyeExtent.height};
}

void XrPresenter::Impl::requestRebuildIfStale() {
    ringBuilt = true;
    if (requestedExtent.width == gameExtent.width && requestedExtent.height == gameExtent.height &&
        requestedFormat == gameFormat) {
        return;
    }
    if (!resizeRequested.exchange(true)) {
        EVR_LOG("presenter: the game's swapchain is now %ux%u format %d; the ring will be rebuilt",
                requestedExtent.width, requestedExtent.height, requestedFormat);
    }
}

} // namespace evr::vkcore
