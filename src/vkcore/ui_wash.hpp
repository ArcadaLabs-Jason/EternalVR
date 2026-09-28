#pragma once

// The GPU side of ui_layer/additive_wash.hpp: two D3D12 compute passes on the XR worker that take the
// full-screen additive wash (the red low-health vignette) out of the GUI image before it is copied into
// the HUD quad's swapchain image. Pass 1 writes each 64x64 block's floor of excess light, pass 2 writes the
// image less the wash (the same rule, byte for byte, as removeAdditiveWash).
//
// The shaders are compiled at start-up from the HLSL below with d3dcompiler_47.dll (part of Windows); if
// that or anything else fails, create() logs it and the image is copied unfiltered, as before.
//
// XR worker only. record() rewrites the pass's descriptors, so the previous frame's commands must be done
// when it is called (the worker waits for every copy before it records the next; presenter_frame.cpp).

#include <windows.h>

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>

namespace evr::vkcore {

class UiWash {
public:
    // Compiles the shaders and makes the root signature, pipelines and descriptor heap; false (with the
    // reason in `why`) leaves the filter off.
    bool create(ID3D12Device* device, const char*& why);
    // Records both passes reading `source` (R8G8B8A8_UNORM, width x height, in the COMMON state, left in
    // it). Returns the filtered image, in the COPY_SOURCE state, for the caller to copy from and then pass
    // afterCopy() on the same list; nullptr when the filter is off or its images could not be made (the
    // caller then copies `source`).
    ID3D12Resource* record(ID3D12GraphicsCommandList* list,
                           ID3D12Resource* source,
                           std::uint32_t width,
                           std::uint32_t height);
    // Returns the filtered image to the state record() expects, after the caller's copy from it.
    void afterCopy(ID3D12GraphicsCommandList* list);
    void destroy();

    [[nodiscard]] bool ready() const { return blockPipeline_ != nullptr; }

private:
    bool makeImages(std::uint32_t width, std::uint32_t height);

    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> blockPipeline_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> applyPipeline_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    UINT descriptorSize_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> floors_; // R32_UINT, one texel per block: r | g << 8 | b << 16
    Microsoft::WRL::ComPtr<ID3D12Resource> output_; // R8G8B8A8_UNORM, the image's size
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

} // namespace evr::vkcore
