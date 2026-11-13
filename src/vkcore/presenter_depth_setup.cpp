// The depth layer's Vulkan objects (presenter_depth.hpp): the copy shader's pipeline and descriptor sets, the
// timestamp queries and the shared buffers, made while the ring is built and freed with it.

#include "vkcore/presenter_depth.hpp"

#include "vkcore/log.hpp"
#include "vkcore/presenter_depth_functions.hpp"
#include "vkcore/presenter_depth_shader.hpp"
#include "vkcore/ui_vulkan.hpp"

#include <algorithm>
#include <memory>
#include <string>

namespace evr::vkcore {

namespace {

// The shader's push constants (presenter_depth_shader.hpp): seven 32-bit words.
constexpr std::uint32_t kPushBytes = 28;

} // namespace

DepthLayer::DepthLayer(bool switchOn, float unitsPerMetre)
    : switchOn_(switchOn), unitsPerMetre_(unitsPerMetre) {}

DepthLayer::~DepthLayer() = default;

void DepthLayer::fail(const char* what) {
    if (!failed_.exchange(true)) {
        EVR_LOG("depth: %s; the depth layer is off for this game (the headset gets colour only)", what);
    }
    active_.store(false, std::memory_order_release);
    stopped_.store(true, std::memory_order_release);
    ui_vulkan::stopHolding();
}

bool DepthLayer::externalBufferImportable(DeviceData& dev) {
    // vkGetPhysicalDeviceExternalBufferProperties (core 1.1, else the KHR name): a D3D12 resource imported as
    // a storage buffer.
    auto query = reinterpret_cast<PFN_vkGetPhysicalDeviceExternalBufferProperties>(
        dev.instance->nextGetInstanceProcAddr(dev.instance->instance,
                                              "vkGetPhysicalDeviceExternalBufferProperties"));
    if (!query) {
        query = reinterpret_cast<PFN_vkGetPhysicalDeviceExternalBufferProperties>(
            dev.instance->nextGetInstanceProcAddr(dev.instance->instance,
                                                  "vkGetPhysicalDeviceExternalBufferPropertiesKHR"));
    }
    if (!query) {
        EVR_LOG("depth: vkGetPhysicalDeviceExternalBufferProperties not found");
        return false;
    }
    VkPhysicalDeviceExternalBufferInfo info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO};
    info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
    VkExternalBufferProperties props{VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
    query(dev.physicalDevice, &info, &props);
    const VkExternalMemoryFeatureFlags features = props.externalMemoryProperties.externalMemoryFeatures;
    if (!(features & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)) {
        EVR_LOG("depth: a D3D12 buffer cannot be imported as a storage buffer (features 0x%x)", features);
        return false;
    }
    return true;
}

bool DepthLayer::createShaderObjects(DeviceData& dev) {
    if (pipeline_) {
        return true;
    }
    if (!fn_) {
        fn_ = std::make_unique<Functions>();
#define EVR_DEPTH_LOAD(name)                                                                                 \
    fn_->name = reinterpret_cast<PFN_vk##name>(dev.nextGetDeviceProcAddr(dev.device, "vk" #name));           \
    if (!fn_->name) {                                                                                        \
        EVR_LOG("depth: vk%s not found", #name);                                                             \
        return false;                                                                                        \
    }
        EVR_DEPTH_FUNCTIONS(EVR_DEPTH_LOAD)
#undef EVR_DEPTH_LOAD
    }
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (fn_->CreateSampler(dev.device, &samplerInfo, nullptr, &sampler_) != VK_SUCCESS) {
        return false;
    }
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[0].pImmutableSamplers = &sampler_;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    setInfo.pBindings = bindings.data();
    if (fn_->CreateDescriptorSetLayout(dev.device, &setInfo, nullptr, &setLayout_) != VK_SUCCESS) {
        return false;
    }
    const VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, kPushBytes};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &setLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &range;
    if (fn_->CreatePipelineLayout(dev.device, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        return false;
    }
    VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    moduleInfo.codeSize = sizeof(kDepthCopySpirv);
    moduleInfo.pCode = kDepthCopySpirv;
    VkShaderModule module = VK_NULL_HANDLE;
    if (fn_->CreateShaderModule(dev.device, &moduleInfo, nullptr, &module) != VK_SUCCESS) {
        return false;
    }
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = module;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout_;
    const VkResult made =
        fn_->CreateComputePipelines(dev.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_);
    fn_->DestroyShaderModule(dev.device, module, nullptr);
    if (made != VK_SUCCESS) {
        pipeline_ = VK_NULL_HANDLE;
        return false;
    }
    const std::array<VkDescriptorPoolSize, 2> sizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kCommandBuffers},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kCommandBuffers}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = kCommandBuffers;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    poolInfo.pPoolSizes = sizes.data();
    if (fn_->CreateDescriptorPool(dev.device, &poolInfo, nullptr, &pool_) != VK_SUCCESS) {
        return false;
    }
    std::array<VkDescriptorSetLayout, kCommandBuffers> layouts{};
    layouts.fill(setLayout_);
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = pool_;
    allocInfo.descriptorSetCount = kCommandBuffers;
    allocInfo.pSetLayouts = layouts.data();
    if (fn_->AllocateDescriptorSets(dev.device, &allocInfo, sets_.data()) != VK_SUCCESS) {
        return false;
    }
    // GPU time of each copy: two timestamps per command buffer (none on a family without timestamps).
    VkPhysicalDeviceProperties props{};
    dev.instance->vk.GetPhysicalDeviceProperties(dev.physicalDevice, &props);
    timestampNs_ = static_cast<double>(props.limits.timestampPeriod);
    std::uint32_t familyCount = 0;
    dev.instance->vk.GetPhysicalDeviceQueueFamilyProperties(dev.physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    dev.instance->vk.GetPhysicalDeviceQueueFamilyProperties(dev.physicalDevice, &familyCount,
                                                            families.data());
    timestampBits_.clear();
    graphicsFamily_.clear();
    for (const VkQueueFamilyProperties& f : families) {
        timestampBits_.push_back(f.timestampValidBits);
        graphicsFamily_.push_back((f.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0);
    }
    VkQueryPoolCreateInfo queryInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryInfo.queryCount = kCommandBuffers * 2;
    if (fn_->CreateQueryPool(dev.device, &queryInfo, nullptr, &queries_) != VK_SUCCESS) {
        queries_ = VK_NULL_HANDLE; // copies without their GPU time
    }
    return true;
}

void DepthLayer::destroyShaderObjects(DeviceData& dev) {
    if (!fn_) {
        return;
    }
    for (const RetiredView& r : retired_) {
        fn_->DestroyImageView(dev.device, r.view, nullptr);
    }
    retired_.clear();
    if (view_) {
        fn_->DestroyImageView(dev.device, view_, nullptr);
        view_ = VK_NULL_HANDLE;
    }
    viewImage_ = VK_NULL_HANDLE;
    viewSerial_ = 0;
    if (queries_) {
        fn_->DestroyQueryPool(dev.device, queries_, nullptr);
        queries_ = VK_NULL_HANDLE;
    }
    if (pool_) {
        fn_->DestroyDescriptorPool(dev.device, pool_, nullptr); // frees the sets
        pool_ = VK_NULL_HANDLE;
        sets_.fill(VK_NULL_HANDLE);
    }
    if (pipeline_) {
        fn_->DestroyPipeline(dev.device, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_) {
        fn_->DestroyPipelineLayout(dev.device, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (setLayout_) {
        fn_->DestroyDescriptorSetLayout(dev.device, setLayout_, nullptr);
        setLayout_ = VK_NULL_HANDLE;
    }
    if (sampler_) {
        fn_->DestroySampler(dev.device, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    timing_.fill({});
}

bool DepthLayer::createSlotBuffer(DeviceData& dev, ID3D12Device* d3d, Slot& slot) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bufferBytes_;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS; // a storage buffer on the Vulkan side (T-080)
    HRESULT hr =
        d3d->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc, D3D12_RESOURCE_STATE_COMMON,
                                     nullptr, IID_PPV_ARGS(&slot.resource));
    HANDLE handle = nullptr;
    if (SUCCEEDED(hr)) {
        hr = d3d->CreateSharedHandle(slot.resource.Get(), nullptr, GENERIC_ALL, nullptr, &handle);
    }
    if (FAILED(hr)) {
        EVR_LOG("depth: a shared D3D12 buffer of %llu bytes failed: 0x%08lx",
                static_cast<unsigned long long>(bufferBytes_), static_cast<unsigned long>(hr));
        slot.resource.Reset();
        return false;
    }
    VkExternalMemoryBufferCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};
    external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, &external};
    info.size = bufferBytes_;
    info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // acquired from and released to the D3D12 side per copy
    VkResult r = dev.vk.CreateBuffer(dev.device, &info, nullptr, &slot.buffer);
    VkMemoryRequirements req{};
    VkMemoryWin32HandlePropertiesKHR handleProps{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
    if (r == VK_SUCCESS) {
        dev.vk.GetBufferMemoryRequirements(dev.device, slot.buffer, &req);
        r = dev.vk.GetMemoryWin32HandlePropertiesKHR(
            dev.device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT, handle, &handleProps);
    }
    std::uint32_t type = UINT32_MAX;
    if (r == VK_SUCCESS) {
        VkPhysicalDeviceMemoryProperties props{};
        dev.instance->vk.GetPhysicalDeviceMemoryProperties(dev.physicalDevice, &props);
        const std::uint32_t bits = req.memoryTypeBits & handleProps.memoryTypeBits;
        for (std::uint32_t t = 0; t < props.memoryTypeCount; ++t) {
            if ((bits & (1u << t)) && (type == UINT32_MAX || (props.memoryTypes[t].propertyFlags &
                                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))) {
                type = t;
            }
        }
        r = type == UINT32_MAX ? VK_ERROR_FEATURE_NOT_PRESENT : VK_SUCCESS;
    }
    if (r == VK_SUCCESS) {
        VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
        dedicated.buffer = slot.buffer;
        VkImportMemoryWin32HandleInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
                                                &dedicated};
        import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
        import.handle = handle;
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &import};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = type;
        r = dev.vk.AllocateMemory(dev.device, &alloc, nullptr, &slot.memory);
    }
    if (r == VK_SUCCESS) {
        r = dev.vk.BindBufferMemory(dev.device, slot.buffer, slot.memory, 0);
    }
    CloseHandle(handle); // importing does not take ownership
    if (r != VK_SUCCESS) {
        EVR_LOG("depth: importing a shared buffer of %llu bytes into the game's device failed (%d)",
                static_cast<unsigned long long>(bufferBytes_), r);
        return false;
    }
    return true;
}

bool DepthLayer::createBuffers(DeviceData& dev,
                               ID3D12Device* d3d,
                               const depth::StartState& startIn,
                               const std::vector<std::int64_t>& formats,
                               VkExtent2D ring,
                               VkExtent2D eye) {
    depth::StartState start = startIn;
    start.switchOn = switchOn_;
    start.extensionEnabled = extension_;
    const std::optional<std::int64_t> format = depth::chooseFormat(formats);
    start.formatOffered = format.has_value();
    const depth::Off off = depth::whyOff(start);
    if (!decided_) {
        decided_ = true;
        EVR_LOG("depth: depth layer %s; the runtime %s XR_KHR_composition_layer_depth", depth::toString(off),
                listed_ ? "lists" : "does not list");
    }
    if (off != depth::Off::None || failed_.load() || !format) {
        return false;
    }
    format_ = *format;
    ring_ = ring;
    eye_ = eye;
    // The footprint of one image of the depth swapchain (every 32-bit depth format has the same one); checked
    // against the swapchain's own images when it is made.
    D3D12_RESOURCE_DESC tex{};
    tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex.Width = ring.width;
    tex.Height = ring.height;
    tex.DepthOrArraySize = 1;
    tex.MipLevels = 1;
    tex.Format = DXGI_FORMAT_R32_TYPELESS;
    tex.SampleDesc.Count = 1;
    tex.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    UINT rows = 0;
    UINT64 rowBytes = 0;
    UINT64 total = 0;
    d3d->GetCopyableFootprints(&tex, 0, 1, 0, &footprint_, &rows, &rowBytes, &total);
    if (total == 0 || footprint_.Footprint.RowPitch % 4 != 0) {
        fail("no copyable footprint for the depth image");
        return false;
    }
    rowTexels_ = footprint_.Footprint.RowPitch / 4;
    bufferBytes_ = total;
    if (!pipeline_ && !externalBufferImportable(dev)) {
        fail("no shared buffer for the depth on this graphics card");
        return false;
    }
    if (!createShaderObjects(dev)) {
        fail("the copy shader's objects could not be made");
        destroyShaderObjects(dev);
        return false;
    }
    for (Slot& slot : slots_) {
        if (!createSlotBuffer(dev, d3d, slot)) {
            // Off for this ring only (no video memory now, say): the next ring build tries again.
            EVR_LOG(
                "depth: the shared depth buffers for a %ux%u ring could not be made; no depth until the next "
                "ring",
                ring.width, ring.height);
            ringFailed_ = true;
            destroyBuffers(dev, false);
            return false;
        }
    }
    if (ringFailed_) {
        ringFailed_ = false;
        EVR_LOG("depth: the shared depth buffers were made for this ring");
    }
    const auto testSwitch = [](const wchar_t* name) {
        std::wstring value;
        return readEnv(name, value) && value == L"1";
    };
    std::wstring readBack;
    readBackOn_ =
        readEnv(L"ETERNALVR_TEST_DEPTH_READBACK", readBack) && !readBack.empty() && readBack != L"0";
    readBackDir_ = readBackOn_ && readBack != L"1" ? readBack : std::wstring();
    fault_ = testSwitch(L"ETERNALVR_TEST_DEPTH_FAULT");
    std::wstring refuse;
    refuse_ = !readEnv(L"ETERNALVR_TEST_DEPTH_REFUSE", refuse) ? Refuse::None
              : refuse == L"1"                                 ? Refuse::Invalid
              : refuse == L"fake"                              ? Refuse::Fake
                                                               : Refuse::None;
    active_.store(true, std::memory_order_release);
    EVR_LOG(
        "depth: depth layer on: %s swapchain of %ux%u (%u eye(s) of %ux%u), %u shared buffer(s) of %.1f MiB "
        "(%.1f MiB in all, row pitch %u bytes); each eye's depth from the game's _upscaledOpaqueDepth by a "
        "compute shader, nothing nearer than %.2f m; planes from the eyes' projection at world size %.2f%s%s",
        depth::formatName(format_), ring.width, ring.height, ring.width / std::max(eye.width, 1u), eye.width,
        eye.height, kSlots, static_cast<double>(bufferBytes_) / (1024.0 * 1024.0),
        static_cast<double>(bufferBytes_) * kSlots / (1024.0 * 1024.0), footprint_.Footprint.RowPitch,
        depth::kNearestMetres, unitsPerMetre_, readBackOn_ ? "; read back every 10 s (test)" : "",
        fault_                       ? "; PLANTED FAULT: eye R's depth into both halves (test)"
        : refuse_ == Refuse::Invalid ? "; PLANTED REFUSAL: an invalid depth info chained (test)"
        : refuse_ == Refuse::Fake    ? "; PLANTED REFUSAL: xrEndFrame taken to fail with depth (test)"
                                     : "");
    return true;
}

void DepthLayer::destroyBuffers(DeviceData& dev, bool all) {
    active_.store(false, std::memory_order_release);
    ui_vulkan::stopHolding();
    for (Slot& slot : slots_) {
        if (slot.buffer) {
            dev.vk.DestroyBuffer(dev.device, slot.buffer, nullptr);
            slot.buffer = VK_NULL_HANDLE;
        }
        if (slot.memory) {
            dev.vk.FreeMemory(dev.device, slot.memory, nullptr);
            slot.memory = VK_NULL_HANDLE;
        }
        slot.resource.Reset();
        slot.eyes = {};
    }
    if (all) {
        destroyShaderObjects(dev);
        fn_.reset();
    }
}

} // namespace evr::vkcore
