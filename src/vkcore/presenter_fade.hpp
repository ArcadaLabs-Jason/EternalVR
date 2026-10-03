#pragma once

// The fade to black (docs/VR_ROOMSCALE.md): the head in geometry, the blink over a re-anchor and a glory
// kill shown as a fade. A black quad layer locked to the head, drawn over the projection layer with the
// fade as its alpha; made whatever ETERNALVR_HEAD_FADE says (that turns off only the head's own fade). A quad
// with source alpha is core OpenXR, so it works the same on every runtime (VDXR, SteamVR, the simulator); no
// extension is needed.
//
// XR worker only. The quad's swapchain is tiny (16 x 16) and cleared to (0, 0, 0, alpha) with D3D12 in
// the frames where the fade is above zero; frames with no fade submit no quad and touch nothing.

#include <windows.h>

#include <d3d12.h>
#include <wrl/client.h>

#include <openxr/openxr.h>

#include <cstdint>
#include <vector>

namespace evr::vkcore {

struct XrFunctions;

class FadeLayer {
public:
    // Creates the swapchain and the D3D12 objects; false (logged) leaves the fade off.
    bool create(const XrFunctions& xr, XrSession session, ID3D12Device* device, std::int64_t format);
    // Clears the next image to black with `alpha` and fills `quad` (head-locked in `viewSpace`); false
    // when nothing should be drawn (no fade, not created, or an image was not available).
    bool prepare(const XrFunctions& xr,
                 ID3D12CommandQueue* queue,
                 float alpha,
                 XrSpace viewSpace,
                 XrCompositionLayerQuad& quad);
    void destroy(const XrFunctions& xr);

    [[nodiscard]] bool ready() const { return swapchain_ != XR_NULL_HANDLE; }

private:
    XrSwapchain swapchain_ = XR_NULL_HANDLE;
    std::vector<ID3D12Resource*> images_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    HANDLE event_ = nullptr;
    std::uint64_t fenceValue_ = 0;
    UINT descriptorSize_ = 0;
    std::uint64_t frames_ = 0;
    std::int64_t acquired_ = -1; // the acquired image, held across frames until its wait succeeds
    bool waited_ = false;
};

} // namespace evr::vkcore
