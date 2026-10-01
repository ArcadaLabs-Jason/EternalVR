#include "features/foveation/attachment_images.hpp"

#include <algorithm>
#include <utility>

namespace evr::foveation {

AttachmentImages::AttachmentImages(std::size_t maxViews, std::size_t maxFramebuffers)
    : maxViews_(maxViews), maxFramebuffers_(maxFramebuffers) {}

bool AttachmentImages::addView(Handle view, Handle image) {
    if (const auto it = views_.find(view); it != views_.end()) {
        it->second = image;
        return true;
    }
    if (views_.size() >= maxViews_) {
        return false;
    }
    views_.emplace(view, image);
    return true;
}

void AttachmentImages::removeView(Handle view) {
    views_.erase(view);
}

std::optional<AttachmentImages::Handle> AttachmentImages::imageOf(Handle view) const {
    const auto it = views_.find(view);
    if (it == views_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool AttachmentImages::addFramebuffer(Handle framebuffer, const std::vector<Handle>& views) {
    const auto it = framebuffers_.find(framebuffer);
    if (it == framebuffers_.end() && framebuffers_.size() >= maxFramebuffers_) {
        return false;
    }
    std::vector<Handle> images;
    images.reserve(views.size());
    for (const Handle view : views) {
        if (const std::optional<Handle> image = imageOf(view)) {
            images.push_back(*image);
        }
    }
    if (it != framebuffers_.end()) {
        it->second = std::move(images);
    } else {
        framebuffers_.emplace(framebuffer, std::move(images));
    }
    return true;
}

void AttachmentImages::removeFramebuffer(Handle framebuffer) {
    framebuffers_.erase(framebuffer);
}

bool AttachmentImages::draws(Handle framebuffer, Handle image) const {
    const auto it = framebuffers_.find(framebuffer);
    return it != framebuffers_.end() &&
           std::find(it->second.begin(), it->second.end(), image) != it->second.end();
}

std::size_t AttachmentImages::viewCount() const {
    return views_.size();
}

std::size_t AttachmentImages::framebufferCount() const {
    return framebuffers_.size();
}

} // namespace evr::foveation
