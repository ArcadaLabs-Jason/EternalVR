#pragma once

// The XR swapchains the presenter holds, and the check of a swapchain just made against them
// (presenter_swapchains.hpp). A runtime should hand every xrCreateSwapchain a new handle and images of its
// own, of the size asked for. The OpenXR Simulator numbers a new swapchain by how many it holds: after a
// destroy, the next one made gets the handle of one still in use and, through it, that swapchain's images;
// destroying the "new" swapchain then destroys the one in use and frees the images the presenter copies into.
// Handles and images are compared as values only, never read. Pure (no OpenXR, no D3D12), unit tested
// (tests/vkcore/swapchain_book_tests.cpp).

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace evr::vkcore::swapchain_book {

// One image of a swapchain just made: its texture (the D3D12 resource's address) and its size.
struct Image {
    std::uintptr_t texture = 0;
    std::uint64_t width = 0;
    std::uint32_t height = 0;
};

enum class Verdict {
    Use,         // a new handle, images of their own of the size asked for
    HeldHandle,  // the handle is one already held: the swapchain is that one (neither used nor destroyed)
    SharedImage, // an image is one of a held swapchain's: not used, and kept until the session ends
    WrongSize,   // an image has another size (smaller, for a static image): not used, destroyed
    NoImages,    // the swapchain has no image: not used, destroyed
};

// What becomes of the swapchain just made.
enum class Action {
    Use,     // used, and held from now on
    Destroy, // destroyed now
    Keep,    // held unused, destroyed when the session ends
    Leave,   // left as it is: the handle is a held swapchain's, which destroying it would destroy
};

// The action for each verdict: a swapchain that is or shares a held one is never destroyed now.
constexpr Action actionFor(Verdict verdict) {
    switch (verdict) {
    case Verdict::Use:
        return Action::Use;
    case Verdict::HeldHandle:
        return Action::Leave;
    case Verdict::SharedImage:
        return Action::Keep;
    case Verdict::WrongSize:
    case Verdict::NoImages:
        return Action::Destroy;
    }
    return Action::Leave; // an unknown verdict destroys nothing
}

// The size an image must have: exactly the one asked for (copied whole), or at least it (a static image whose
// pixels go into its top-left corner and whose quad shows only that rectangle).
enum class Fit { Exact, AtLeast };

struct Check {
    Verdict verdict = Verdict::Use;
    std::size_t image = 0;       // the image that decided it (SharedImage, WrongSize)
    const char* owner = nullptr; // the held swapchain's name (HeldHandle, SharedImage)
};

class Book {
public:
    // A swapchain now held, with its images' textures; `what` names it in the log. A null handle is ignored.
    void add(std::uintptr_t handle, std::vector<std::uintptr_t> textures, const char* what) {
        if (handle != 0) {
            entries_.push_back({handle, std::move(textures), what});
        }
    }
    // A held swapchain destroyed; false when it was not held.
    bool remove(std::uintptr_t handle) {
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->handle == handle) {
                entries_.erase(it);
                return true;
            }
        }
        return false;
    }
    void clear() { entries_.clear(); }
    [[nodiscard]] bool holds(std::uintptr_t handle) const { return find(handle) != nullptr; }
    [[nodiscard]] std::size_t size() const { return entries_.size(); }

    // A swapchain just made (`handle`, `images`) for `width` x `height` (`fit`). The held handle is checked
    // first: the images of a held swapchain are its own and of its own size, so that is what they would show.
    [[nodiscard]] Check check(std::uintptr_t handle,
                              const std::vector<Image>& images,
                              std::uint64_t width,
                              std::uint32_t height,
                              Fit fit = Fit::Exact) const {
        if (const Entry* held = find(handle)) {
            return {Verdict::HeldHandle, 0, held->what};
        }
        for (std::size_t i = 0; i < images.size(); ++i) {
            for (const Entry& e : entries_) {
                for (const std::uintptr_t t : e.textures) {
                    if (t != 0 && t == images[i].texture) {
                        return {Verdict::SharedImage, i, e.what};
                    }
                }
            }
        }
        if (images.empty()) {
            return {Verdict::NoImages, 0, nullptr};
        }
        for (std::size_t i = 0; i < images.size(); ++i) {
            const bool fits = fit == Fit::Exact ? images[i].width == width && images[i].height == height
                                                : images[i].width >= width && images[i].height >= height;
            if (!fits) {
                return {Verdict::WrongSize, i, nullptr};
            }
        }
        return {};
    }

private:
    struct Entry {
        std::uintptr_t handle = 0;
        std::vector<std::uintptr_t> textures;
        const char* what = "";
    };
    [[nodiscard]] const Entry* find(std::uintptr_t handle) const {
        for (const Entry& e : entries_) {
            if (e.handle == handle) {
                return &e;
            }
        }
        return nullptr;
    }
    std::vector<Entry> entries_;
};

} // namespace evr::vkcore::swapchain_book
