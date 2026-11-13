// The depth layer's XR worker side (presenter_depth.hpp): the extension, the depth swapchain, the D3D12 copy
// of a slot's depth into it, the depth info chained to the projection's views, a refusal of it, the 10 s line
// and the test read-back of the depth swapchain's image.

#include "vkcore/presenter_depth.hpp"

#include "vkcore/log.hpp"
#include "vkcore/presenter_types.hpp"
#include "vkcore/ui_vulkan.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace evr::vkcore {

namespace {

constexpr ULONGLONG kReadBackEveryMs = 10000;
constexpr std::size_t kReadBackStep = 8;    // every 8th texel in each direction
constexpr std::uint32_t kReadBackFiles = 3; // images written with a folder

double percent(std::uint64_t part, std::uint64_t whole) {
    return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
}

D3D12_RESOURCE_BARRIER transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}

} // namespace

void DepthLayer::addExtension(const std::vector<XrExtensionProperties>& listed,
                              std::vector<const char*>& extensions) {
    listed_ = std::any_of(listed.begin(), listed.end(), [](const XrExtensionProperties& p) {
        return std::strcmp(p.extensionName, XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME) == 0;
    });
    extension_ = switchOn_ && listed_;
    if (extension_) {
        extensions.push_back(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);
    } else if (active()) {
        fail("the new runtime instance does not list XR_KHR_composition_layer_depth");
    }
}

void DepthLayer::extensionRefused() {
    extension_ = false;
    if (active()) {
        fail("the runtime instance was made again without XR_KHR_composition_layer_depth");
    }
}

bool DepthLayer::createSwapchain(const XrFunctions& xr, XrSession session, XrSwapchainSet& swapchains) {
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
                      (readBackOn_ ? XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT : 0); // the test reads it back
    info.format = format_;
    info.sampleCount = 1;
    info.width = ring_.width;
    info.height = ring_.height;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    XrSwapchain made = XR_NULL_HANDLE;
    const XrResult r = xr.xrCreateSwapchain(session, &info, &made);
    if (XR_FAILED(r)) {
        swapchainFailed_ = true;
        EVR_LOG("depth: xrCreateSwapchain for the %s depth images failed (%d)", depth::formatName(format_),
                static_cast<int>(r));
        fail("no depth swapchain");
        return false;
    }
    if (!swapchains.admit(xr, made, ring_.width, ring_.height, "depth", images_) || images_.empty()) {
        swapchainFailed_ = true;
        images_.clear();
        fail("no depth swapchain of the colour image's size");
        return false;
    }
    // The copy is the shared buffer as a placed footprint of the swapchain's own images (their format may be
    // the typeless one); its pitch must be the one the buffer was laid out with.
    const D3D12_RESOURCE_DESC desc = images_[0]->GetDesc();
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT own{};
    UINT64 total = 0;
    if (SUCCEEDED(images_[0]->GetDevice(IID_PPV_ARGS(&device)))) {
        UINT rows = 0;
        UINT64 rowBytes = 0;
        device->GetCopyableFootprints(&desc, 0, 1, 0, &own, &rows, &rowBytes, &total);
    }
    if (total == 0 || total > bufferBytes_ || own.Footprint.RowPitch != footprint_.Footprint.RowPitch ||
        own.Footprint.Width != footprint_.Footprint.Width ||
        own.Footprint.Height != footprint_.Footprint.Height) {
        EVR_LOG(
            "depth: the runtime's depth images (D3D12 format %d, %llux%u) have a row pitch of %u bytes, the "
            "shared buffers %u",
            static_cast<int>(desc.Format), static_cast<unsigned long long>(desc.Width), desc.Height,
            own.Footprint.RowPitch, footprint_.Footprint.RowPitch);
        swapchainFailed_ = true;
        images_.clear();
        swapchains.destroy(xr, made); // admitted (held): destroyed, not left behind
        fail("the depth images do not fit the shared buffers");
        return false;
    }
    footprint_.Footprint.Format = own.Footprint.Format;
    swapchain_ = made;
    EVR_LOG("depth: depth swapchain %ux%u %s, %zu image(s) (D3D12 format %d, flags 0x%X)", ring_.width,
            ring_.height, depth::formatName(format_), images_.size(), static_cast<int>(desc.Format),
            static_cast<unsigned>(desc.Flags));
    return true;
}

bool DepthLayer::slotComplete(std::uint32_t slot) const {
    return active() && slot < kSlots && slots_[slot].eyes[0] && slots_[slot].eyes[1];
}

bool DepthLayer::acquire(const XrFunctions& xr,
                         XrSession session,
                         XrSwapchainSet& swapchains,
                         WaitStats& waits) {
    if (!active() || (!swapchain_ && (swapchainFailed_ || !createSwapchain(xr, session, swapchains)))) {
        return false;
    }
    if (!image_.acquired()) {
        std::uint32_t index = 0;
        if (XR_FAILED(xr.xrAcquireSwapchainImage(swapchain_, nullptr, &index)) || index >= images_.size()) {
            return false;
        }
        image_.onAcquired(index);
    }
    if (!image_.waited()) {
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = kSwapchainWaitTimeout;
        const XrResult r = timedCall(waits, xr.xrWaitSwapchainImage, swapchain_, &wait);
        if (r != XR_SUCCESS) {
            // A timeout keeps the image acquired for the next frame (T-081); the runtime holding the depth
            // image costs a frame's wait each time, so a run of them stops the layer.
            const depth::WaitTimeouts::Step step = waits_.onTimeout();
            if (step.log) {
                EVR_LOG(
                    "depth: waiting for the depth image ended with %d (%llu so far); this image goes without "
                    "depth",
                    static_cast<int>(r), static_cast<unsigned long long>(waits_.total));
            }
            if (step.stop) {
                fail("the runtime held the depth image through several frames in a row");
            }
            return false;
        }
        waits_.onWaited();
        image_.onWaited();
    }
    return true;
}

void DepthLayer::recordXrCopy(ID3D12GraphicsCommandList* list, std::uint32_t slotIndex) {
    const Slot& slot = slots_[slotIndex];
    ID3D12Resource* target = images_[static_cast<std::size_t>(image_.index())].Get();
    // An acquired depth swapchain image is in DEPTH_WRITE (XR_KHR_D3D12_enable) and goes back there.
    const std::array<D3D12_RESOURCE_BARRIER, 2> before{
        transition(slot.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
        transition(target, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_DEST)};
    list->ResourceBarrier(static_cast<UINT>(before.size()), before.data());
    // A depth image is copied whole (CopyTextureRegion with depth-stencil): both eyes at once.
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = target;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = slot.resource.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint_;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_STATES imageState = D3D12_RESOURCE_STATE_COPY_DEST;
    if (readBackOn_ && !readBackPending_ && GetTickCount64() - lastReadBack_ >= kReadBackEveryMs) {
        recordReadBack(list, target, slot);
        imageState = D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
    const std::array<D3D12_RESOURCE_BARRIER, 2> after{
        transition(slot.resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
        transition(target, imageState, D3D12_RESOURCE_STATE_DEPTH_WRITE)};
    list->ResourceBarrier(static_cast<UINT>(after.size()), after.data());
    // The slot may be written again once its colour copy is done (completeCopy frees it before finish()): its
    // planes are taken now, while it is being read.
    const auto model = depth::modelFromEngineRow(slot.m10, slot.m11);
    image_.onCopyRecorded(model ? depth::planesOf(*model, unitsPerMetre_) : std::nullopt);
}

void DepthLayer::recordReadBack(ID3D12GraphicsCommandList* list, ID3D12Resource* image, const Slot& slot) {
    // ETERNALVR_TEST_DEPTH_READBACK: the depth swapchain's image itself, as the runtime gets it, into a
    // read-back buffer (made, or made again larger, here).
    if (!readBack_ || readBackBytes_ < bufferBytes_) {
        readBack_.Reset();
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bufferBytes_;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(image->GetDevice(IID_PPV_ARGS(&device))) ||
            FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                   IID_PPV_ARGS(&readBack_)))) {
            EVR_LOG("depth: no read-back buffer; the test read-back is off");
            readBackOn_ = false;
            readBack_.Reset();
            return;
        }
        readBackBytes_ = bufferBytes_;
    }
    const D3D12_RESOURCE_BARRIER toSource =
        transition(image, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->ResourceBarrier(1, &toSource);
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = readBack_.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint_;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = image;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    readBackPending_ = true;
    lastReadBack_ = GetTickCount64();
    readBackModel_ = depth::modelFromEngineRow(slot.m10, slot.m11);
}

void DepthLayer::finish(const XrFunctions& xr) {
    const bool hadCopy = image_.pending();
    if (image_.onNewImage()) {
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xr.xrReleaseSwapchainImage(swapchain_, &release);
    }
    if (hadCopy) {
        ++imagesWith_;
    } else if (active()) {
        ++imagesWithout_;
    }
    if (readBackPending_) {
        readBackPending_ = false;
        if (hadCopy) {
            readBack();
        }
    }
}

bool DepthLayer::fill(std::array<XrCompositionLayerProjectionView, 2>& views,
                      std::array<XrCompositionLayerDepthInfoKHR, 2>& infos) {
    if (!image_.shown() || !swapchain_ || !active() || stopped_.load(std::memory_order_acquire)) {
        if (active()) {
            ++framesWithout_;
        }
        return false;
    }
    const depth::Planes& planes = image_.planes();
    for (std::size_t i = 0; i < views.size(); ++i) {
        XrCompositionLayerDepthInfoKHR& d = infos[i];
        d = XrCompositionLayerDepthInfoKHR{XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR};
        d.subImage.swapchain = swapchain_;
        d.subImage.imageRect = views[i].subImage.imageRect; // the eye's half, as its colour
        d.subImage.imageArrayIndex = 0;
        d.minDepth = 0.0f;
        d.maxDepth = 1.0f;
        d.nearZ = planes.nearZ; // depth 0: the far plane (reversed depth: nearZ > farZ), finite
        d.farZ = planes.farZ;   // depth 1: the near plane
        if (refuse_ == Refuse::Invalid) {
            d.minDepth = 1.0f; // the planted refusal: minDepth above maxDepth, which the extension forbids
            d.maxDepth = 0.0f;
        }
        views[i].next = &d;
    }
    ++framesWith_;
    if (!loggedFirstFrame_) {
        loggedFirstFrame_ = true;
        EVR_LOG("depth: first frame submitted with depth (minDepth 0, maxDepth 1, nearZ %.1f m, farZ %.4f m)",
                planes.nearZ, planes.farZ);
    }
    return true;
}

void DepthLayer::refused(int result) {
    image_.onRefused();
    EVR_LOG("depth: xrEndFrame failed (%d) on a frame with depth", result);
    fail("the runtime refused the depth layer");
}

XrSwapchain DepthLayer::detachSwapchain() {
    const XrSwapchain swapchain = swapchain_;
    swapchain_ = XR_NULL_HANDLE;
    images_.clear();
    image_.onDetached();
    swapchainFailed_ = false;
    readBackPending_ = false;
    return swapchain;
}

void DepthLayer::destroyXr(const XrFunctions& xr, XrSwapchainSet& swapchains) {
    XrSwapchain swapchain = detachSwapchain();
    swapchains.destroy(xr, swapchain);
    readBack_.Reset();
    readBackBytes_ = 0;
}

void DepthLayer::logStats() {
    if (!switchOn_ || !decided_ || (!active() && framesWith_ == 0 && imagesWith_ == 0)) {
        return;
    }
    const std::uint64_t copied = eyesCopied_.exchange(0);
    const std::uint64_t notCopied = eyesNotCopied_.exchange(0);
    const std::uint64_t samples = gpuSamples_.exchange(0);
    const std::uint64_t sumNs = gpuSumNs_.exchange(0);
    const std::uint64_t maxNs = gpuMaxNs_.exchange(0);
    const std::uint64_t shaderRead = layouts_[0].exchange(0);
    const std::uint64_t depthRead = layouts_[1].exchange(0);
    const std::uint64_t attachment = layouts_[2].exchange(0);
    const depth::Planes planes = image_.shown() ? image_.planes() : depth::Planes{};
    EVR_LOG(
        "depth: last 10 s: %llu of %llu projection frame(s) with depth; %llu new image(s) with depth, %llu "
        "without; eyes copied %llu, not copied %llu (last reason: %s); copy GPU time %.3f ms average, %.3f "
        "ms longest (%llu copies timed); nearZ %.1f m, farZ %.4f m; source layout shader read-only %llu, "
        "depth read-only %llu, attachment %llu; waits added to other queues %llu in all",
        static_cast<unsigned long long>(framesWith_),
        static_cast<unsigned long long>(framesWith_ + framesWithout_),
        static_cast<unsigned long long>(imagesWith_), static_cast<unsigned long long>(imagesWithout_),
        static_cast<unsigned long long>(copied), static_cast<unsigned long long>(notCopied),
        notCopied ? lastWhy_.load() : "none",
        samples ? static_cast<double>(sumNs) / static_cast<double>(samples) / 1e6 : 0.0,
        static_cast<double>(maxNs) / 1e6, static_cast<unsigned long long>(samples), planes.nearZ, planes.farZ,
        static_cast<unsigned long long>(shaderRead), static_cast<unsigned long long>(depthRead),
        static_cast<unsigned long long>(attachment), static_cast<unsigned long long>(ui_vulkan::holdWaits()));
    framesWith_ = 0;
    framesWithout_ = 0;
    imagesWith_ = 0;
    imagesWithout_ = 0;
}

void DepthLayer::readBack() {
    if (!readBack_ || !readBackModel_) {
        return;
    }
    const depth::DepthModel model = *readBackModel_;
    void* mapped = nullptr;
    const D3D12_RANGE range{0, static_cast<SIZE_T>(bufferBytes_)};
    if (FAILED(readBack_->Map(0, &range, &mapped)) || !mapped) {
        return;
    }
    const auto* texels = static_cast<const float*>(mapped);
    const float nearest = depth::nearestDepth(model, unitsPerMetre_);
    std::array<depth::EyeStats, 2> eyes{};
    for (std::size_t e = 0; e < eyes.size(); ++e) {
        eyes[e] = depth::statsOf(texels, rowTexels_, e * eye_.width, eye_.width, eye_.height, kReadBackStep,
                                 model, unitsPerMetre_, nearest);
    }
    const double same = depth::sameShare(texels, rowTexels_, eye_.width, eye_.height, kReadBackStep);
    if (!readBackDir_.empty() && readBackFiles_ < kReadBackFiles) {
        writeReadBack(texels);
    }
    const D3D12_RANGE none{0, 0};
    readBack_->Unmap(0, &none);
    const auto eyeText = [](const depth::EyeStats& s) {
        char text[256];
        std::snprintf(
            text, sizeof(text),
            "%.1f%% far (sky), %.1f%% at the nearest value, %llu invalid; %.2f to %.1f m, median %.2f m, "
            "centre %.2f m",
            percent(s.sky, s.samples), percent(s.clamped, s.samples),
            static_cast<unsigned long long>(s.invalid), s.minMetres, s.maxMetres, s.medianMetres,
            std::isfinite(s.centreMetres) ? s.centreMetres : -1.0);
        return std::string(text);
    };
    EVR_LOG("depth: read back from the depth swapchain image (every %zu texel(s)): eye L: %s; eye R: %s; the "
            "eyes "
            "hold the same value in %.1f%% of the texels outside the sky (%s)",
            kReadBackStep, eyeText(eyes[0]).c_str(), eyeText(eyes[1]).c_str(), same * 100.0,
            same > 0.5 ? "THE SAME depth in both eyes" : "the eyes differ");
}

void DepthLayer::writeReadBack(const float* texels) {
    if (!readBackModel_) {
        return;
    }
    const depth::DepthModel model = *readBackModel_;
    // Both eyes as one 8-bit grey image (PGM): 255 at the nearest value, darker by 40 a doubling of the
    // distance, the sky black.
    const std::wstring path = readBackDir_ + L"\\depth-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                              std::to_wstring(readBackFiles_++) + L".pgm";
    std::FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) {
        return;
    }
    const std::size_t width = std::size_t{eye_.width} * 2;
    std::fprintf(f, "P5\n%zu %u\n255\n", width, eye_.height);
    std::vector<unsigned char> row(width);
    for (std::size_t y = 0; y < eye_.height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const float d = texels[y * rowTexels_ + x];
            const double m = d > 0.0f ? depth::unitsAt(model, d) / unitsPerMetre_ : 0.0;
            const double v =
                m > 0.0 && std::isfinite(m) ? 255.0 - 40.0 * std::log2(std::max(m, 0.25) / 0.25) : 0.0;
            row[x] = static_cast<unsigned char>(std::clamp(v, 0.0, 255.0));
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    EVR_LOG("depth: read back written to %ls", path.c_str());
}

} // namespace evr::vkcore
