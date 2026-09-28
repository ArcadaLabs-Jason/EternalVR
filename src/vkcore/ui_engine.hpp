#pragma once

// Engine side of the UI layer (docs/rig-findings/ui-layer.md sections 2 and 5; Steam build 25216728).
//
// The game draws every 2D element (HUD, menus, subtitles, loading overlays) into its GUI render target
// `_gui` (renderSystem + 0x5C8 -> colour image; RGBA8, premultiplied alpha, the output size) and blends it
// onto the frame in its final compute pass, the view colour upsample (RVA 0x1CDF6E0), which binds it as
// `guiMap`. This module:
// - locates the render system's GUI target slot and the composite's load of the GUI image by signature,
//   cross-checked against each other and against the engine's own "no GUI" bind of `_black` in the same
//   function (its screenshot-without-HUD path);
// - reads the GUI target's VkImage and shape each frame (no write);
// - optionally installs one mid hook on the composite's load (RVA 0x1CDF7CB, `mov [rsp+0x70], rax`) that,
//   while skipping is requested and the multiplayer guard allows it, hands the composite the engine's
//   `_black` image (16x16 RGBA8 of zeros) instead, exactly as the engine does for its own no-GUI capture.
//   The GUI is then still drawn into `_gui` but not into the eye images.
//
// Any failed check leaves the game untouched (no capture, no skip), with the reason in the log.

#include "ui_layer/gui_target.hpp"

#include <cstdint>
#include <optional>

namespace evr::vkcore::ui_engine {

// Locates and checks everything once per process; with `skipHook`, also installs the composite hook
// (skipping stays off until setSkipComposite(true)). Only while the multiplayer guard is armed. Later
// calls return the first result.
bool install(bool skipHook);
bool located();
bool skipHookInstalled();

// The GUI target's colour image as the game holds it now; nullopt when not located, the guard is off, or
// a pointer on the way is null or unreadable.
std::optional<ui_layer::GuiImageFields> readTarget();

// Requested by the XR worker every frame the UI quad shows the captured target; a request lasts 250 ms, so
// the game composites its GUI again on its own if the worker stops renewing it.
void setSkipComposite(bool skip);
bool skipComposite();

struct Counters {
    std::uint64_t composites = 0; // composite passes seen by the hook
    std::uint64_t skipped = 0;    // of them, given `_black` as guiMap
    std::uint64_t readFailures = 0;
};
Counters counters();

} // namespace evr::vkcore::ui_engine
