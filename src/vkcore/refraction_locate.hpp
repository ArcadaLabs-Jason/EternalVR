#pragma once

// Where the refraction history's hooks go (refraction_hooks.hpp; Steam build 25216728), located by signature
// and checked before anything is changed: every site in its function and in order, the loops' image names,
// one image manager and create function for both loops, one device context global for every pass site and the
// resize, and the transparency job's call of the refraction pass with its update count
// (kRefractionUpdateCount).

#include <cstddef>

namespace evr::vkcore {

struct GameImage;

struct RefractionSites {
    // The device context constructor (RVA 0x1C1CD80), after the engine's images: rdi the device context, rbp
    // + kRefractionMaskDescription / kRefractionAccDescription the image description the engine used.
    std::byte* maskImagesBuilt = nullptr; // RVA 0x1C1D4AA: the mask tiles' pair (+0x488 / +0x490)
    std::byte* accImagesBuilt = nullptr;  // RVA 0x1C1DD63: the accumulation images and targets
    // The mask pass (RVA 0x1C64360).
    std::byte* maskEntry = nullptr; // its entry: rdx the transparency context
    std::byte* maskSlots = nullptr; // RVA 0x1C64413: r15 = parity * 8 + 0x488, rcx the device context
    std::byte* maskLast = nullptr;  // RVA 0x1C64711: r8 the engine's tiles [parity ^ 1], before their binding
    // The refraction pass (RVA 0x1C63DB0).
    std::byte* passEntry = nullptr; // its entry: rdx the transparency context, r8d the update, r9b the reset
    std::byte* passMask =
        nullptr; // RVA 0x1C63EF0: the argument block filled (with the mask), before `test bl`
    std::byte* passPlain = nullptr; // RVA 0x1C6402B: the same without the mask, the blend stored
    // The render-size change (RVA 0x1CDD6D0), after it resized the engine's refraction images.
    std::byte* imagesResized = nullptr;       // RVA 0x1CDDBBB
    const std::byte* resizeImage = nullptr;   // (image, width, height, depth, mips), RVA 0x1C4AE30
    const std::byte* imageManager = nullptr;  // [ ] = the image manager
    const std::byte* createImage = nullptr;   // (manager, name, description) -> image
    const std::byte* deviceContext = nullptr; // [ ] = the device context
};

inline constexpr std::size_t kRefractionMaskDescription = 0x1B0; // rbp + 0x1B0
inline constexpr std::size_t kRefractionAccDescription = 0x140;  // rbp + 0x140
// The transparency context's refraction update count for the render (+0x137D0, set before the transparency
// job runs, RVA 0x1C624CF), which the job compares with the last render's for the pass's reset flag (RVA
// 0x1C62A97); part of the call site's signature.
inline constexpr std::size_t kRefractionUpdateCount = 0x137D0;

// False (logged under `tag`) when a site is missing, not unique or does not check out.
bool locateRefractionSites(const GameImage& image, const char* tag, RefractionSites& out);

} // namespace evr::vkcore
