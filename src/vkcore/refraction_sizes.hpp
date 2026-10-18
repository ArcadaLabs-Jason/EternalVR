#pragma once

// Eye R's refraction images kept at the engine's size (refraction_hooks.hpp; Steam build 25216728).
//
// The device context constructor makes the refraction images at the render size it starts with, and eye R's
// are made there with them. The render-size change (RVA 0x1CDD6D0, which runs before every frame and changes
// an image only when its size differs) resizes the engine's in place: the accumulation targets (+0x5F0 ..
// +0x608, through the render-target resize RVA 0x1C743C0) to half the render size and the mask tiles (+0x488
// / +0x490) to 1/32 of it, all through `resize(image, width, height, depth, mips)` (RVA 0x1C4AE30). Nothing
// resized eye R's: on the rig the engine's accumulation images were 1024x1104 at a 2048x2208 render size and
// eye R's still 594x566 from the start-up window, their mask tiles 64x69 against 74x70. The refraction pass
// writes and reads eye R's history as an image of another size, which left the scene behind glass about 30%
// darker in eye R than in eye L and in a mono render. Right after the engine's resize eye R's follow, by the
// same function and to the size the engine's now have; when any had to, both eyes start their history over.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace evr::vkcore::refraction_sizes {

// An image's extent (the fields the engine's resize reads and writes): width +0x64, height +0x68, depth
// +0x6C, mip count +0x74.
inline constexpr std::size_t kImageWidth = 0x64;
inline constexpr std::size_t kImageHeight = 0x68;
inline constexpr std::size_t kImageDepth = 0x6C;
inline constexpr std::size_t kImageMips = 0x74;

struct Extent {
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t depth = 0;
    std::int32_t mips = 0;
    friend constexpr bool operator==(const Extent&, const Extent&) = default;
};

inline Extent extentOf(const void* image) {
    Extent e;
    if (!image) {
        return e;
    }
    const auto* b = static_cast<const std::byte*>(image);
    std::memcpy(&e.width, b + kImageWidth, sizeof(e.width));
    std::memcpy(&e.height, b + kImageHeight, sizeof(e.height));
    std::memcpy(&e.depth, b + kImageDepth, sizeof(e.depth));
    std::memcpy(&e.mips, b + kImageMips, sizeof(e.mips));
    return e;
}

// The engine's image resize (RVA 0x1C4AE30): true when it changed the image.
using ResizeImageFn = bool (*)(void* image, int width, int height, int depth, int mips);

// An engine image and eye R's image in its place.
struct ImagePair {
    void* engine = nullptr;
    void* eyeR = nullptr;
};

// The six pairs: the mask tiles [slot] and the accumulation images [update][slot].
inline constexpr std::size_t kPairs = 6;
using ImagePairs = std::array<ImagePair, kPairs>;
using MaskImages = std::array<void*, 2>;               // [slot]
using AccImages = std::array<std::array<void*, 2>, 2>; // [update][slot]

// The pairs of the engine's set and eye R's: per slot its mask tiles, then its accumulation images by update.
inline ImagePairs pairsOf(const MaskImages& engineMask,
                          const AccImages& engineAcc,
                          const MaskImages& eyeRMask,
                          const AccImages& eyeRAcc) {
    ImagePairs p;
    for (std::size_t slot = 0; slot < 2; ++slot) {
        p[slot * 3] = {engineMask[slot], eyeRMask[slot]};
        for (std::size_t u = 0; u < 2; ++u) {
            p[slot * 3 + 1 + u] = {engineAcc[u][slot], eyeRAcc[u][slot]};
        }
    }
    return p;
}

// What followEngine did: the eye R images it resized, and those the resize left as they were (`firstFailed`
// the index of the first in the pairs, -1 when none).
struct FollowResult {
    int changed = 0;
    int failed = 0;
    int firstFailed = -1;
};

// Brings every eye R image whose extent differs from its engine image's to that extent, with the engine's
// resize (which returns false when it changed nothing).
inline FollowResult followEngine(const ImagePairs& pairs, ResizeImageFn resize) {
    FollowResult r;
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        const ImagePair& p = pairs[i];
        if (!p.engine || !p.eyeR) {
            continue;
        }
        const Extent want = extentOf(p.engine);
        if (extentOf(p.eyeR) == want) {
            continue;
        }
        if (resize(p.eyeR, want.width, want.height, want.depth, want.mips) && extentOf(p.eyeR) == want) {
            ++r.changed;
        } else if (r.failed++ == 0) {
            r.firstFailed = static_cast<int>(i);
        }
    }
    return r;
}

// Every eye R image has its engine image's extent.
inline bool sameExtents(const ImagePairs& pairs) {
    for (const ImagePair& p : pairs) {
        if (!p.engine || !p.eyeR || extentOf(p.engine) != extentOf(p.eyeR)) {
            return false;
        }
    }
    return true;
}

// While eye R's images are not the engine's size, every render blends with no history (eye R's would use
// images of another size; the others, with eye R's render left to the engine, would read an engine slot eye R
// wrote): the first such eye R render (HoldOffFirst) also starts both eyes' history over. The next
// render-size change, which runs before every frame, brings the images in line; only when they still differ
// after kMismatchedRenders eye R renders in a row does the history fail closed.
enum class SizeCheck : std::uint8_t { Same, HoldOffFirst, HoldOff, FailClosed };
inline constexpr int kMismatchedRenders = 8;

class SizeGuard {
public:
    // Eye R's render `render` (the backend counter; each of its passes asks again and gets the same answer)
    // found its images `sameSize`. A pass of an older render than the last one asked (a straggler) changes
    // nothing and holds off unless the sizes match.
    SizeCheck atRender(std::uint32_t render, bool sameSize) {
        if (asked_ && render == render_) {
            return result_;
        }
        if (asked_ && static_cast<std::int32_t>(render - render_) < 0) {
            return sameSize ? SizeCheck::Same : SizeCheck::HoldOff;
        }
        asked_ = true;
        render_ = render;
        mismatched_ = sameSize ? 0 : mismatched_ + 1;
        result_ = sameSize                           ? SizeCheck::Same
                  : mismatched_ == 1                 ? SizeCheck::HoldOffFirst
                  : mismatched_ > kMismatchedRenders ? SizeCheck::FailClosed
                                                     : SizeCheck::HoldOff;
        return result_;
    }
    // The last eye R render found its images of another size: the other renders hold off too.
    bool mismatched() const { return mismatched_ > 0; }
    void reset() { *this = SizeGuard{}; }

private:
    bool asked_ = false;
    std::uint32_t render_ = 0;
    int mismatched_ = 0;
    SizeCheck result_ = SizeCheck::Same;
};

} // namespace evr::vkcore::refraction_sizes
