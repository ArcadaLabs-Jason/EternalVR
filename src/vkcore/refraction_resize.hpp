#pragma once

// Eye R's refraction images follow the engine's resize (refraction_sizes.hpp, refraction_hooks.hpp; Steam
// build 25216728). The render-size change (RVA 0x1CDD6D0) runs before every frame on the renderer's thread
// and resizes the engine's refraction images in place, changing them only when the render size changed; a
// hook right after it (RVA 0x1CDDBBB) brings eye R's six to the engine's sizes with the engine's resize. When
// any had to, both eyes start their history over. A resize that leaves an image at its size is logged with
// the image's index (the first few); eye R's renders then blend with no history until the images match
// (refraction_sizes::SizeGuard).

#include "vkcore/refraction_sizes.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace evr::vkcore {

struct RefractionSites;

// Installs the hook. `restart` (both eyes start over) runs on the renderer's thread, with no lock of this
// file held, whenever an eye R image followed.
bool installRefractionResizeHook(const RefractionSites& sites, void (*restart)(), std::string& error);

// A device context's refraction images were built (`deviceContext` null when eye R's were not): the pairs eye
// R's follow from now on; the guard starts over.
void trackRefractionImages(const std::byte* deviceContext, const refraction_sizes::ImagePairs& pairs);

// Eye R's render `render` (job threads): whether its images are the engine's size.
refraction_sizes::SizeCheck checkRefractionImageSizes(std::uint32_t render);

// Any other render (job threads): whether the last eye R render found its images of another size.
bool refractionImagesMismatched();

} // namespace evr::vkcore
