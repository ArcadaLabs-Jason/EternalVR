#pragma once

// The refraction's temporal history per eye under Route S (stereo_seq/refraction_history.hpp; Steam build
// 25216728).
//
// - Eye R's images are made in the device context constructor (RVA 0x1C1CD80) right after the engine's: two
//   mask tile images after the engine's pair (+0x488/+0x490; hook at RVA 0x1C1D4AA) and four accumulation
//   images after the engine's four (+0x440..+0x458, their targets +0x5F0..+0x608; hook at RVA 0x1C1DD63),
//   each through the image manager with the engine's own image description. When the render size changes
//   the engine resizes its own in place (RVA 0x1CDD6D0, before every frame) and eye R's follow
//   (refraction_resize.hpp, hook at RVA 0x1CDDBBB); both eyes then start over. While eye R's images are not
//   the engine's size every render blends with no history; 8 such eye R renders in a row fail closed. Dynamic
//   resolution (rs_enable not 0) scales the engine's images of the counter's parity at each frame's start
//   (RVA 0x1CDE950) and never eye R's, so the history is held off in stereo while rs_enable reads non-zero,
//   and both eyes start over when it is 0 again.
// - The mask pass (RVA 0x1C64360) and the refraction pass (RVA 0x1C63DB0) read the pairs from the device
//   context by the parity of the transparency context's backend counter (+0xF8, stored at RVA 0x1C60253). At
//   each pass's entry the render's eye is found by that counter and the plan made; then the mask pass's
//   output (its write mark, size and binding, all three through r15 + the device context) and its last-render
//   input (RVA 0x1C64711), and the refraction pass's four images on its argument block (mask path RVA
//   0x1C63EF0, path without the mask RVA 0x1C6402B) are the eye's own. A restart sets the pass's reset flag
//   (r9b, the engine's own when its update count changed: blend 1.0, no history), or the blend 1.0 without
//   the mask. Each update also starts over when the eye's update count (transparency context + 0x137D0, the
//   job's call at RVA 0x1C62A93) differs from its own last render's or that render did not run it; when
//   neither holds, the engine's reset flag (set because another render's count differs, the other eye's) is
//   cleared. The engine's device context is never written.
//
// Located by signature; anything missing leaves the game untouched (the engine's history, shared between the
// eyes). The signatures also match the Game Pass build (1.0.56.0), where this has not been tested. A check
// after start-up that fails (another device context, an image that is not the engine's, eye R's images not
// the size of the engine's after a retry, an update not below the render's count) holds the history off in
// stereo until the device context is built again: every stereo render blends with no history, so neither eye
// reads the other's. r_refractionTAA (read only without the mask) is written 1 by per-eye TAA's set while the
// history is per eye, else held 0 as before. Route S only.

namespace evr::vkcore {

// Locates and installs the hooks once per process, before the renderer starts (eye R's images are made with
// the device context); later calls return the first result. From vkCreateInstance whenever Route S is
// requested: the history needs the eye tags only, not per-eye TAA.
bool installRefractionHooksEarly();

// For the start-up line of the temporal modes: "per eye" or "NOT hooked".
const char* refractionHooksText();

// The per-eye history runs (stereo_seq::refractionNotReady: installed, eye R's images made, not failed
// closed, rs_enable 0): per-eye TAA's set writes r_refractionTAA 1 instead of 0.
bool refractionPerEyeReady();

} // namespace evr::vkcore
