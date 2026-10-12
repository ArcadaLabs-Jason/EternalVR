#pragma once

// The XR presenter's swapchains as made (XR worker only): each one is checked against those still held
// (swapchain_book.hpp) before it is used, and destroyed through here. A swapchain that is not the new one it
// should be is never used, and never destroyed while that could free the images of one in use.

#include "vkcore/swapchain_book.hpp"

#include <windows.h>

#include <d3d12.h>
#include <wrl/client.h>

#include <openxr/openxr.h>

#include <cstdint>
#include <vector>

namespace evr::vkcore {

struct XrFunctions;

class XrSwapchainSet {
public:
    using Images = std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>;

    // `made` was just returned by xrCreateSwapchain for `width` x `height`; `what` names it in the log. True:
    // its images (referenced, so they outlive any mix-up in the runtime) are in `images` and it is held.
    // False (logged): not used; a handle already held is left as it is, one sharing a held swapchain's images
    // is kept until the session ends, and any other is destroyed.
    bool admit(const XrFunctions& xr,
               XrSwapchain made,
               std::uint32_t width,
               std::uint32_t height,
               const char* what,
               Images& images,
               swapchain_book::Fit fit = swapchain_book::Fit::Exact);
    // A swapchain made elsewhere (the fade layer), held without a check.
    void hold(XrSwapchain swapchain, const std::vector<ID3D12Resource*>& images, const char* what);
    // A held swapchain about to be destroyed elsewhere (the fade layer, the vignette): no longer held.
    void forget(XrSwapchain swapchain);
    // Destroys a held swapchain (any other is left alone) and nulls the handle.
    void destroy(const XrFunctions& xr, XrSwapchain& swapchain);
    // At the session's end, after every held swapchain was destroyed or forgotten: the ones kept unused, then
    // the record.
    void endSession(const XrFunctions& xr);

private:
    swapchain_book::Book book_;
    std::vector<XrSwapchain> kept_; // shared a held swapchain's images: destroyed at the session's end
};

} // namespace evr::vkcore
