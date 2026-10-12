// The XR presenter's swapchains as made (presenter_swapchains.hpp).

#include "vkcore/presenter_swapchains.hpp"

#include "vkcore/log.hpp"
#include "vkcore/presenter_types.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace evr::vkcore {

namespace {

std::uintptr_t key(XrSwapchain swapchain) {
    return reinterpret_cast<std::uintptr_t>(swapchain);
}

std::uintptr_t key(const ID3D12Resource* texture) {
    return reinterpret_cast<std::uintptr_t>(texture);
}

} // namespace

bool XrSwapchainSet::admit(const XrFunctions& xr,
                           XrSwapchain made,
                           std::uint32_t width,
                           std::uint32_t height,
                           const char* what,
                           Images& images,
                           swapchain_book::Fit fit) {
    images.clear();
    std::uint32_t count = 0;
    std::vector<XrSwapchainImageD3D12KHR> list;
    XrResult r = xr.xrEnumerateSwapchainImages(made, 0, &count, nullptr);
    if (XR_SUCCEEDED(r)) {
        list.assign(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        r = xr.xrEnumerateSwapchainImages(made, count, &count,
                                          reinterpret_cast<XrSwapchainImageBaseHeader*>(list.data()));
        list.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
    }
    if (XR_FAILED(r)) {
        EVR_LOG("xr: the %s swapchain's images could not be listed (XrResult %d); not used", what,
                static_cast<int>(r));
        if (!book_.holds(key(made))) {
            xr.xrDestroySwapchain(made);
        }
        return false;
    }
    std::vector<swapchain_book::Image> seen;
    std::vector<std::uintptr_t> textures;
    for (const XrSwapchainImageD3D12KHR& image : list) {
        D3D12_RESOURCE_DESC desc{};
        if (image.texture) {
            desc = image.texture->GetDesc();
        }
        seen.push_back({key(image.texture), desc.Width, desc.Height});
        textures.push_back(key(image.texture));
    }
    const swapchain_book::Check check = book_.check(key(made), seen, width, height, fit);
    switch (check.verdict) {
    case swapchain_book::Verdict::Use:
        break;
    case swapchain_book::Verdict::HeldHandle:
        EVR_LOG(
            "xr: the runtime returned the %s swapchain, still in use, for a new %s swapchain of %ux%u; not "
            "used, not destroyed",
            check.owner, what, width, height);
        break;
    case swapchain_book::Verdict::SharedImage:
        EVR_LOG("xr: the new %s swapchain's image %zu is one of the %s swapchain's; not used, kept until the "
                "session ends",
                what, check.image, check.owner);
        break;
    case swapchain_book::Verdict::WrongSize:
        // A copy into an image of another size fails to record, and executing it would remove the device.
        EVR_LOG("xr: the runtime's %s swapchain image %zu is %llux%u, not %ux%u; swapchain refused", what,
                check.image, static_cast<unsigned long long>(seen[check.image].width),
                seen[check.image].height, width, height);
        break;
    case swapchain_book::Verdict::NoImages:
        EVR_LOG("xr: the runtime's %s swapchain has no image; swapchain refused", what);
        break;
    }
    // Never destroyed now when it is, or shares the images of, a swapchain in use (swapchain_book.hpp).
    switch (swapchain_book::actionFor(check.verdict)) {
    case swapchain_book::Action::Use:
        for (const XrSwapchainImageD3D12KHR& image : list) {
            images.emplace_back(image.texture); // a reference of our own
        }
        book_.add(key(made), std::move(textures), what);
        return true;
    case swapchain_book::Action::Keep:
        book_.add(key(made), std::move(textures), what);
        kept_.push_back(made);
        return false;
    case swapchain_book::Action::Destroy:
        xr.xrDestroySwapchain(made);
        return false;
    case swapchain_book::Action::Leave:
        return false;
    }
    return false;
}

void XrSwapchainSet::hold(XrSwapchain swapchain,
                          const std::vector<ID3D12Resource*>& images,
                          const char* what) {
    std::vector<std::uintptr_t> textures;
    for (const ID3D12Resource* image : images) {
        textures.push_back(key(image));
    }
    book_.add(key(swapchain), std::move(textures), what);
}

void XrSwapchainSet::forget(XrSwapchain swapchain) {
    book_.remove(key(swapchain));
}

void XrSwapchainSet::destroy(const XrFunctions& xr, XrSwapchain& swapchain) {
    if (swapchain != XR_NULL_HANDLE && book_.remove(key(swapchain))) {
        xr.xrDestroySwapchain(swapchain); // valid with an image still acquired
    }
    swapchain = XR_NULL_HANDLE;
}

void XrSwapchainSet::endSession(const XrFunctions& xr) {
    // Every held swapchain was destroyed or forgotten by now; the ones kept unused go last.
    for (const XrSwapchain swapchain : kept_) {
        xr.xrDestroySwapchain(swapchain);
    }
    if (!kept_.empty()) {
        EVR_LOG("xr: %zu swapchain(s) kept unused destroyed at the session's end", kept_.size());
    }
    kept_.clear();
    book_.clear();
}

} // namespace evr::vkcore
