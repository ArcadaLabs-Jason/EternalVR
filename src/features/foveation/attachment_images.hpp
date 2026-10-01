#pragma once

// The images a render pass draws into, followed through the game's image views and framebuffers. Foveated
// rendering (vkcore/vrs_nv.hpp) keeps the passes into the game's GUI target at full rate with it: the GUI
// target has the eye images' size, so the render area alone cannot tell them apart.
//
// A view maps to the image it was made for; a framebuffer keeps the images of its attachments, read when it
// is made (a framebuffer's views stay alive while it is used). Handles are the Vulkan handles as integers,
// so this module needs no Vulkan headers. Bounded: a full table keeps no new entries until some are
// removed, and a pass whose framebuffer is not kept counts as drawing into no known image. Not thread-safe:
// the caller locks.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace evr::foveation {

class AttachmentImages {
public:
    using Handle = std::uint64_t;

    // The game makes a view for most of its images (textures included); its framebuffers are far fewer.
    static constexpr std::size_t kDefaultMaxViews = 131072;
    static constexpr std::size_t kDefaultMaxFramebuffers = 16384;

    explicit AttachmentImages(std::size_t maxViews = kDefaultMaxViews,
                              std::size_t maxFramebuffers = kDefaultMaxFramebuffers);

    // `view` was made for `image` (an earlier entry of the same handle is replaced). False when the view
    // table is full and the view is not kept.
    bool addView(Handle view, Handle image);
    void removeView(Handle view);
    // The image `view` was made for; nullopt for a view not kept.
    [[nodiscard]] std::optional<Handle> imageOf(Handle view) const;

    // `framebuffer` was made with `views` as its attachments (an earlier entry of the same handle is
    // replaced); views not kept are left out. False when the framebuffer table is full and it is not kept.
    bool addFramebuffer(Handle framebuffer, const std::vector<Handle>& views);
    void removeFramebuffer(Handle framebuffer);
    // True when one of `framebuffer`'s attachments is a view of `image`; false for a framebuffer not kept.
    [[nodiscard]] bool draws(Handle framebuffer, Handle image) const;

    [[nodiscard]] std::size_t viewCount() const;
    [[nodiscard]] std::size_t framebufferCount() const;

private:
    std::size_t maxViews_;
    std::size_t maxFramebuffers_;
    std::unordered_map<Handle, Handle> views_;
    std::unordered_map<Handle, std::vector<Handle>> framebuffers_;
};

} // namespace evr::foveation
