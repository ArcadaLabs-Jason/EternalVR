#pragma once

// Locating the engine code and cvars per-eye TAA uses (docs/rig-findings/stereo-temporal.md; Steam build
// 25216728). Every signature must match once in .text; nothing here writes to the game.

#include "vkcore/game_text.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace evr::vkcore {

// The device context's per-view slot loop (RVA 0x1C1A190, in the device context constructor 0x1C19C10):
// for each render view i it prepares slot i (0x1C1CC40) and builds its images with the slot builder
// (RVA 0x1C20150: viewColor, accumulationBuffer0/1, accumulationBufferOpaque, distortion and their render
// targets). The hook goes on the instruction after the builder call, where r14 = the device context and
// ebx = i.
struct TaaSlotSite {
    const std::byte* hookSite = nullptr;    // RVA 0x1C1A1B9
    const std::byte* slotBuilder = nullptr; // RVA 0x1C20150: (deviceContext, slot, deviceContextIndex)
    // The device context's render-target resize (RVA 0x1C21600: (deviceContext, size, upscaled size)); it
    // resizes slot 0's targets in place with the render-target resize (RVA 0x1C743C0: (target, width,
    // height, flag)), which it calls first at +0xCB.
    const std::byte* contextResize = nullptr;
    const std::byte* targetResize = nullptr;
};
bool locateTaaSlotSite(const GameImage& image, TaaSlotSite& out);

struct TaaEngine {
    // The accumulation selectors (renderSystem, idRenderView) -> render target: the TAA output (RVA
    // 0x1CBB5A0) and the previous frame's (RVA 0x1CBB6C0), both read by the render-view job only.
    const std::byte* outputSelector = nullptr;
    const std::byte* historySelector = nullptr;
    // The opaque accumulation selector (RVA 0x1CBB580): slot + 0x68, one image per slot.
    const std::byte* opaqueSelector = nullptr;
    // idCVar::SetString (RVA 0x376020): (cvar object, value, force).
    const std::byte* setCvar = nullptr;
    // The instruction after the auto-exposure index store (RVA 0x1C98D46, in 0x1C988E0): rsi = the
    // post-process context, its +0x140 the index just stored.
    const std::byte* exposureSite = nullptr;
    // The render-view job binding the view colour image (slot + 0x48, the previous frame's scene colour)
    // as distortionLastFrameMap: the `add r8, 0xC8` after its load (RVA 0x1C56657), r8 = the image.
    const std::byte* distortionSite = nullptr;
};
bool locateTaaEngine(const GameImage& image, TaaEngine& out);

// idCVar::SetString (RVA 0x376020): (cvar object, value, force). nullptr (logged) unless it matches once.
const std::byte* findCvarSetter(const GameImage& image);

// The cvar objects registered under `names` (in the same order; nullptr for a name not found once). A
// registration is `lea r8, [default]; lea rdx, [name]; lea rcx, [object]; call`. The object's first
// member points at its value block: the value string at +0x0, the integer at +0x8.
std::vector<std::byte*> findCvarObjects(const GameImage& image, const std::vector<std::string_view>& names);

// The integer value of a cvar object (0 for nullptr).
int cvarInt(const std::byte* object);

} // namespace evr::vkcore
