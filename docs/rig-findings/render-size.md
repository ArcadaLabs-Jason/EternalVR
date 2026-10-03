# Render size: each eye at the headset's size, not the window's (T-031)

Static analysis of the retail `DOOMEternalx64vk.exe`, Steam build 25216728 (PE timestamp 0x6A7B9B8C, the exe
of `engine-facts.md` and `mp-guard.md`), and offline checks of the Windows and Vulkan behaviour the approach
relies on. The game was not run for this document. Tools: the Python helpers in `analysis/scripts` (PE
loader, xrefs, cvar names, disassembly), Ghidra 12.1.4 headless on a copy of the ui-layer project
(`analysis/render-size`), and a small Vulkan and Win32 test program on the rig's RTX 4080 (driver 616.92).
All addresses are RVAs in this build.

**[static-verified]**: read in this build's code or data (every signature below matches once in `.text`).
**[offline-verified]**: measured on the rig without the game (section 4). **[inferred]**: what the verified
code implies; section 8 lists the live checks.

## 1. Answer

Stereo rendered each eye at the game window's client size. The launcher fitted that window to a desktop
display, so the eye size depended on the owner's displays (1415 x 1440 per eye with the TV on), and the game
clamps its window to the display's work area.

The game takes both its swapchain size and its output size (the eye image, `_gui`) from `GetClientRect` of
its window, at four call sites (section 2). The layer now answers exactly those four calls with the render
size (the runtime's recommended view size, section 5), reports the same size as the surface's extent, and
creates the swapchain with **present scaling** (`VK_KHR_swapchain_maintenance1`), so the driver scales each
presented image into the real window, which stays a small desktop mirror. Every other part of the layer (the
ring copy, Route S, the window present gate, the UI capture) works on the swapchain's images as before, only
larger. Behind `ETERNALVR_RENDER_SIZE=auto|WxH` and `ETERNALVR_RENDER_SCALE`; anything missing fails closed to
the window-sized render.

| Option | Verdict | Why |
|---|---|---|
| (a) Virtual swapchain: the layer owns the game's swapchain images and blits a scaled eye into a small real one | Not needed now; the fallback if a driver lacks present scaling | Correct but large: acquire and present semaphores and fences, layouts, the engine's `STORAGE` use of the images, the window present gate and the mirror all re-implemented. Present scaling gives the same result with the real swapchain |
| (b) An oversized window beyond the display | Rejected | Windows allows a client area larger than the monitor only when `WM_GETMINMAXINFO` raises the track size [offline-verified], and the game itself clamps its window to the monitor's work area (0x1CC0980, section 2.1). The desktop would compose a 4.5 Mpix window at over 100 Hz, and the desktop window could not be small |
| (c) The engine's own render scale | Rejected | `rs_forceResolution` is "0.0 to 1.0" (registration 0x264120) and DLSS renders below the output size (0x1CBFA60); nothing renders above the output size, which is the window's. No supersampling cvar exists |
| **Chosen**: the client area answered with the render size, present scaling into the real window | Implemented | Four call sites, one import slot, two surface queries and one swapchain create; the engine's own resize path does the rest (section 3) |

## 2. How the game sizes its output and its swapchain [static-verified]

### 2.1 Output size

| What | Where |
|---|---|
| Output size (the eye image, `_gui`, `_upscaledOpaqueDepth`) | globals 0x39AABE4 (width) and 0x39AABE8 (height); render system vtable 0x200 (0x1CBF8D0) and 0x208 (0x1CBF8C0) return them (render system vtable at 0x2EAD3F8) |
| Render size (`_viewColor`; below the output only with DLSS) | 0x39AABDC and 0x39AABE0; vtable 0x1E0 (0x1CBF990) and 0x1E8 (0x1CBF7B0). 0x1CBFA60 sets them to the output size, or through 0x1CC5D40 when DLSS is on (0x14667EC53): NGX's optimal render size for the output size and `PerfQualityValue` (from `r_dlssQuality`: 0 gives 3 Ultra Performance, 1 gives 0, 2 gives 1, 3 gives 2, anything else 1 Balanced). With DLAA (`PerfQualityValue` 5, set by the layer, `dlss-dll.md` section 8) the optimal size is the output size |
| Video init 0x1CC04D0 (callers 0x1CBEFC0, 0x1CC0430, 0x1CC80E0) | windowed (`r_fullscreen` 0x66E82F0 not 1): the output size is `r_windowWidth` x `r_windowHeight` (0x66E8470, 0x66E84F0) when both are positive, else the mode's size, else 960 x 540; fullscreen: `r_mode` (0x66E8050) through the video modes |
| Window apply 0x1CC0980 (after init) | windowed: the client rect at `r_windowPosX/Y` (0x66E8570, 0x66E85F0) with the output size, `AdjustWindowRect` (style 0xCE0000, resizable), then **clamped to the monitor's work area** and `SetWindowPos`: the clamp the rig saw |
| Window moved or sized | the window procedure 0x1DC3B90 handles `WM_WINDOWPOSCHANGED` (0x47, jump table at 0x1DC42D8) by queueing 0x1DC39B0 through 0x1DC0600; it runs **0x1DC3B00**: `GetClientRect` (call 0x1DC3B22) and `ClientToScreen`, then render system vtable 0x298 (**0x1CC1240**) with (x, y, width, height) |
| 0x1CC1240 | unless fullscreen (vtable 0x2C0, 0x1CBFB10: flag 0x39AABF0) or inside the window apply (vtable 0x290, 0x1CBFA40: flag 0x39AAC18, set while 0x1CC0980 runs): writes `r_windowPosX/Y/Width/Height` and the output size (at least 1 x 1), then 0x1CBFA60 |

So in windowed mode the output size follows `GetClientRect` of the game's window whenever the window moves or
changes size, which is how `ETERNALVR_WINDOW` (a `SetWindowPos` before the first swapchain) set the eye size.

### 2.2 Swapchain

The swapchain object is at the render backend + 0xB8 (HWND at +8, "create on the window thread" at +0x10,
recreate flag at +0x18, size at +0x1C / +0x20, surface at +0x28, `VkSwapchainKHR` at +0x30).

| Step | Code |
|---|---|
| Every frame | 0x1CC1320 (render system vtable 0x1C0): **0x1D09070** compares `GetClientRect` (call 0x1D09091) with +0x1C / +0x20 and sets +0x18 when they differ; if set, **0x1D090E0** recreates (then 0x1C4AE30 resizes the back buffer target at +0x1A8 to the new size), then acquire 0x1D09280 |
| Recreate 0x1D090E0 | `GetClientRect` (call 0x1D0912E) gives the size, or, with +0x10 set, `SendMessage(0x9235)` runs **0x1D09190** on the window's thread, which does the same (call 0x1D091B4) |
| Create 0x1D098B0 (window, width, height) | destroys the old swapchain, creates the surface (`vkCreateWin32SurfaceKHR`, IAT thunk 0x2267B2D), checks present support, reads the surface capabilities (`vkGetPhysicalDeviceSurfaceCapabilities2KHR` only with `r_useFullScreenExclusive`, else the plain query at 0x1D09B7F); a `currentExtent` of 0xFFFFFFFF is taken as (width, height), and **any other `currentExtent` than (width, height) returns without a swapchain**. The swapchain is created with that extent, usage 0x1F, `B8G8R8A8_UNORM` (44) unless HDR |
| Acquire 0x1D09280, present 0x1D09670 | `VK_SUBOPTIMAL_KHR` and `VK_ERROR_OUT_OF_DATE_KHR` set the recreate flag |

The Vulkan WSI calls go through the import table (`vulkan-1.dll`: caps 0x2A1CD70, create 0x2A1CD88, images
0x2A1CD98, acquire 0x2A1CDA0, present 0x2A1CDA8), so the layer's own entry points see them.

### 2.3 The four GetClientRect calls

All read the `GetClientRect` import slot 0x2A1C048 (`call [rip + disp]`, FF 15). The other game callers of
`GetClientRect` keep the real client area: the cursor centring 0x5F5970, the raw-input cursor code 0x1DC18B0 and
0x1DC1E60, and a window helper outside the renderer (0x26F5610, 0x26F5F18, 0x26F6D5C).

| Call | Signature (the call is at the given offset) | Return address |
|---|---|---|
| Swapchain resize check (0x1D09070) | `48 8D 54 24 20 48 8B 49 08 FF 15 ?? ?? ?? ?? 8B 44 24 28 2B 44 24 20 3B 43 1C 75 ?? 8B 44 24 2C 2B 44 24 24 3B 43 20` (+9) | 0x1D09097 |
| Swapchain recreate (0x1D090E0) | `48 8D 54 24 30 FF 15 ?? ?? ?? ?? 8B 4C 24 38 2B 4C 24 30 8B 44 24 3C 2B 44 24 34` (+5) | 0x1D09134 |
| Recreate on the window thread (0x1D09190) | `48 8B 09 48 8B 49 08 FF 15 ?? ?? ?? ?? 8B 54 24 28 2B 54 24 20 44 8B 44 24 2C 44 2B 44 24 24 89 53 08 44 89 43 0C` (+7) | 0x1D091BA |
| Window size (0x1DC3B00) | `48 8D 54 24 38 48 8B 09 FF 15 ?? ?? ?? ?? 85 C0 74 ?? 48 8B 0D ?? ?? ?? ?? 48 8D 54 24 30 48 C7 44 24 30 00 00 00 00` (+8) | 0x1DC3B28 |

## 3. What the layer does

`src/vkcore/client_rect.*`, `virtual_client.*`, `surface_entry.cpp`, `swapchain_entry.cpp`; the pure rules
in `src/features/render_size/` (tested in `tests/features/render_size/`).

1. **Client area.** At the game's first window surface, while the multiplayer guard is armed, the four call
   sites are located (each signature once in `.text`, all reading one import slot), and the game's
   `GetClientRect` import is replaced (`import_patch`, shared with key injection). The replacement calls the
   real function and, only when its return address is one of the four and the window is the game's, answers
   `{0, 0, width, height}` with the render size. It remembers the answer per thread.
2. **Surface.** `vkGetPhysicalDeviceSurfaceCapabilities(2)KHR` on the game's surface, on a thread whose last
   answer was the render size, report it as current, minimum and maximum extent. A create that started with
   the real client area (the render size switched on in between) is answered with the real extent, so the game
   never sees a mismatch it would stop at (section 2.2).
3. **Swapchain.** `vkCreateSwapchainKHR` on the game's surface at the render size gets
   `VkSwapchainPresentScalingCreateInfoKHR`: `ASPECT_RATIO_STRETCH` (the eye letterboxed into the window) with
   centred gravity when offered, else `STRETCH`, else `ONE_TO_ONE`. `VK_SUBOPTIMAL_KHR` from that swapchain's
   acquires and presents is returned as `VK_SUCCESS` (the size differs from the window on purpose; otherwise
   the game would recreate every frame).
4. **Size.** `auto` waits for the runtime (the XR worker's `xrGetSystem`, then
   `xrEnumerateViewConfigurationViews`); `WxH` is known at once. When the size is known and the device has
   `VK_KHR_swapchain_maintenance1` and the surface offers present scaling for FIFO, immediate and mailbox with
   the size inside the scaled image range, the answer switches on and the window is moved to
   `ETERNALVR_MIRROR_WINDOW` with `SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS`. Windows then sends
   `WM_WINDOWPOSCHANGED` (even when nothing moves [offline-verified]), the game reads its client area again
   (0x1DC3B00) and sets its output size, and its per-frame check recreates the swapchain at the new size. The
   presenter rebuilds its ring for the new swapchain as for any resize.
5. **Instance and device.** `VK_KHR_get_surface_capabilities2` and `VK_KHR_surface_maintenance1` on the
   instance and `VK_KHR_swapchain_maintenance1` on the device are added when the render size is wanted, as they
   already are for the Route S window gate (with the same retry without them).

**Fail closed.** The render size stays off, and the game renders at its window's size, when: the variable is
unset or `off`; a call site does not match once, the calls read another slot or the import cannot be
replaced; the device lacks `VK_KHR_swapchain_maintenance1`; the surface offers no present scaling or not for
the size; a create with present scaling fails; or the multiplayer guard is refused or tripped. Turning off
puts the window back at `ETERNALVR_WINDOW` and tells the game to read its client area again. Every change to
the game (the import slot) is made only while the guard is armed; each answer and each present checks it
again (`virtual_client::poll`).

## 4. Offline checks on the rig [offline-verified]

A Win32 and Vulkan program (NVIDIA RTX 4080, driver 616.92; not the game):

| Check | Result |
|---|---|
| Present scaling offered for a Win32 surface | `supportedPresentScaling` 0x7 (one-to-one, aspect-ratio stretch, stretch), gravity min, max and centred on both axes, scaled image 1 x 1 to 4294967294 x 4294967294, for FIFO, immediate and mailbox (also in `vulkaninfo`) |
| A 2064 x 2208 swapchain into a 640 x 360 client area, `ASPECT_RATIO_STRETCH`, 120 immediate presents | created; every acquire and present `VK_SUCCESS` |
| The window resized to 484 x 661 during it, 120 more presents | acquires `VK_SUCCESS`, presents **`VK_SUBOPTIMAL_KHR`** (never out of date): the reason for step 3's mapping |
| `SetWindowPos` with only `SWP_FRAMECHANGED` (plus `NOMOVE`, `NOSIZE`), same thread and `SWP_ASYNCWINDOWPOS` from another thread | one `WM_WINDOWPOSCHANGED` each |
| A 3000 x 3200 client area on a 1280 x 800 monitor | 1284 x 781 with the default `WM_GETMINMAXINFO`; 3000 x 3200 when it raises the track size (option (b)) |

## 5. The size

`ETERNALVR_RENDER_SIZE`:

- `auto` (the launcher's default in stereo): the largest recommended image rect over the views, scaled down
  uniformly into a budget of **2064 x 2208 pixels per eye (4.56 Mpix)** and never up, times
  `ETERNALVR_RENDER_SCALE` (default 1.0, 0.5 to 2.0), then scaled down to fit the runtime's maximum image rect
  and maximum swapchain size (Route S keeps both eyes side by side in one image, so the width limit is halved)
  and 16384, and rounded to multiples of 8.
- `WxH` (256 to 8192 per side): that size, fitted to the runtime's limits once they are known.
- `off` or unset: the window's size, as before.

The budget is the default choice: the rig ran Route S at 2064 x 2100 per eye at about 115 stereo ticks per
second with the v1 cvars (VR_STEREO.md, live results), and research 11 puts the native 2496 x 2688 of a Quest 3
at the 90 Hz limit of an RTX 4080 without upscaling. VDXR's 2496 x 2688 at 100 % becomes **2056 x 2216** (the
simulator reports the same recommendation); the owner's Virtual Desktop at "57 %" recommends less than the
budget and renders at exactly its recommendation. A scale above 1 goes past the budget on purpose.

The eye image is near-square, as the headset's views are; the projection already maps each eye's FOV onto the
whole image (VR_STEREO.md, render size), so the pixels become square.

## 6. The UI target

`_gui` has the output size (ui-layer.md section 2.1), so it follows the eye size and is near-square. idSWF lays
the game's screens out in a centred 16:9 frame fitted to the width: in the UI captures at 2064 x 2100 and
2560 x 2100 the menus, the HUD corners, the boss bar and the subtitles all sit inside that band, and the rows
above and below it are empty (only the FPS counter sits at the image's corner) [static-verified on the captures
of `tmp-vr/m6t3-ui` and `tmp-vr/u2-ui`]. So the UI quad and the menu panel show only that band
(`ui_layer::wideContentRect`, `ETERNALVR_UI_CROP=0` shows the whole image): a 16:9 UI at the eye's width
(2056 x 1157 at the default size, near ARCHITECTURE 7a's 2048 x 1152), at the same place and size in metres
as before. The menu pointer's hits on the band are mapped onto the whole image for the game's cursor.

## 7. Launcher

Stereo launches set `ETERNALVR_RENDER_SIZE` (`render_size` in `launcher.ini`, default `auto`) and
`ETERNALVR_RENDER_SCALE` (`render_scale`, 0.50 to 2.00, in the settings window as "Render scale"), and no
longer fit the window to a display: the window is a 1280 x 720 desktop mirror (`ETERNALVR_WINDOW` and
`ETERNALVR_MIRROR_WINDOW`, `r_windowWidth/Height`) at the top-left of a virtual display when there is one (a
virtual display driver, Virtual Desktop's or Meta's virtual monitor: `VirtualDisplays`), else the primary one.
`render_size = off` keeps the eye-sized window fitted to a display, as before. If the layer cannot set the
render size, the game renders at the mirror's size: visibly soft, and the layer log says why (`size: render
size off: ...`).

**The game starts at its final size.** Switching from the mirror's size to the render size a few seconds into
the session makes the engine resize its render targets; on cards with 12 GB or less that resize can ask for
about 1 GB more and fail with "Failed to allocate video memory" (a modal box behind the headset: it looks like a
hang). A game that starts at the render size never resizes (verified on an RTX 3080 Ti). So before a stereo
launch with the render size on, the launcher asks the runtime itself (`OpenXrProbe`: the layer's
`openxr_loader.dll` by full path, `xrCreateInstance` without a graphics extension, `xrGetSystem`,
`xrGetSystemProperties`, `xrEnumerateViewConfigurationViews` for the stereo views, `xrDestroyInstance`; on a
worker thread, at most 5 s), with the game's OpenXR environment (`XR_RUNTIME_JSON` for a chosen runtime, the
OpenXR API layers it switches off) set in its own process for the probe only. It works the size out with the
layer's rules, ported line for line (`RenderSize`, section 5, Route S's two eyes per swapchain image; the C++
tests' vectors are repeated in `RenderSizeMathTests`), and gives the game `ETERNALVR_RENDER_SIZE=WxH` (a fixed
size, which the layer applies before the first swapchain) and `+r_windowWidth W +r_windowHeight H` (video init
takes the output size from them, section 2.1). The window stays the mirror (`ETERNALVR_WINDOW`,
`ETERNALVR_MIRROR_WINDOW`): the layer answers the game's client-area queries with the render size. A fixed
`WxH` setting is fitted to the runtime's limits the same way and goes on the command line too. The launcher log
states the choice (`render:  render size 2056x2216 from the runtime's recommendation 2496x2688 (scale 1.00)`).

If the runtime does not answer (no runtime, the headset off: `XR_ERROR_FORM_FACTOR_UNAVAILABLE`, the loader
missing, the 5 s passed), the launch goes on as before: `ETERNALVR_RENDER_SIZE=auto`, the game starts at the
mirror's size and the layer switches to the render size in-game. The launcher log gives the reason and a note
("Headset not detected; the render size is decided in-game.").

Probe results on the rig (no game): Virtual Desktop's runtime with a Quest 3 recommends 2496 x 2688 (max image
and swapchain 16384), giving 2056 x 2216; the simulator recommends 1280 x 1400 (max 4096), giving 1280 x 1400,
and fits a fixed 2064 x 2208 to 2048 x 2192.

**The mirror's options (layer).** With the render size on, the layer can move and resize the launcher's mirror
window (`mirror_place.hpp`, `features/render_size/mirror_window.hpp`; docs/VR_STEREO.md, settings):
`ETERNALVR_MIRROR_DISPLAY` (`primary`, a display number, a point `x,y`, or `launcher`), `ETERNALVR_MIRROR_SIZE`
(`WxH`, a scale, or `fill`: the whole display, borderless, the image stretched into it), `ETERNALVR_MIRROR_CROP` (`16:9`: the eye image's centred band in a 16:9 window; `full`, the
default, shows the whole image in a window of the eye's shape) and `ETERNALVR_MIRROR_FRONT` (the window brought to the front once when
its surface is made, never activated). The window's rectangle is worked out once from the launcher's and the
displays Windows lists at that moment, and is used both before the first swapchain (`ETERNALVR_WINDOW`'s
placement) and when the render size turns on. The crop needs present scaling `STRETCH` (the rig's surfaces
offer it); without it the window shows the whole image.

With `full` the window takes the eye image's shape (`render_size::shapeToImage`, `mirror_place::shapeToEye`):
the rectangle above is cut to the largest one of the eye's aspect inside it, keeping its top-left corner, or
its centre when a display choice centred it (a 2056 x 2216 eye in 1280 x 720 gives 668 x 720). The eye's size
is the one the window reports, or a fixed render size before that (`auto` waits for the runtime, and the
window is reshaped when the render size turns on). Before, the window kept the 16:9 size and the swapchain's
`ASPECT_RATIO_STRETCH` (`scaling 0x2, gravity 0x4`, logged on the only swapchain create) letterboxed the image;
on an NVIDIA RTX 4080 the first presents were nevertheless stretched to the window, and only after the window
was minimised and restored a few times (no new swapchain, no other log change) did the bars appear. With the
window at the eye's shape a stretch and a letterbox give the same image, so the first frame is right whatever
the driver does. `fill` and the crops keep their shapes.

`ETERNALVR_MIRROR_SIZE=fill` is a size choice rather than a separate switch because it replaces the size (the
launcher's `Window size` row) and works with every display choice and crop. The window gets the display's
whole area (`rcMonitor`, not the work area) and loses its caption, sizing border, system menu and edges when
it is placed before the first swapchain (the same synchronous `SetWindowPos` as `ETERNALVR_WINDOW`, with
`SWP_FRAMECHANGED`); the frame is not put back, also not when the render size turns off. The swapchain is
created with `STRETCH` whatever the crop: with `full` the whole eye image is stretched to the display's shape
(a Quest 3 eye image of 2056 x 2216 on a 16:9 display is squashed to about half its height), with `16:9` the
band fills a 16:9 display exactly. Cropping to each display's own shape instead was left out: the crop setting
already gives that for the usual 16:9 and 16:10 displays. The window is raised once without being activated,
as before, so a display's taskbar (always on top) can stay over it.

## 8. Live checks (phase 2, on the virtual display, the simulator runtime)

1. **Default launch** (launcher, simulator runtime, stereo). Log: `size: render size auto x1.00`, the four
   call sites with their RVAs, `GetClientRect import (slot RVA 0x2A1C048)`, `present scaling 0x7`, `the runtime
   recommends 2496x2688 per eye ... gives 2056x2216`, `the game's window reports a 2056x2216 client area`,
   `render size on: window client area to ...,668x720`, then `vkCreateSwapchainKHR 2056x2216 ...` with
   `size: the swapchain is scaled into the window's real client area (scaling 0x2, gravity 0x4)`, `presenter:
   ring rebuilt for 4112x2216` and every 10 s `size: render size 2056x2216 (swapchain 2056x2216, eye image
   2056x2216)` with client-area answers counting up. The desktop window is 668 x 720 (`mirror: the window
   takes the eye's shape`) and shows eye L without bars.
2. **Each eye's size and projection**: the `xr: the game's own FOV setting for the headset ... the game's image
   is 2056x2216` line, the `latch:` lines (projection `[0][0]` and `[1][1]` against the per-eye tangents), `seq:`
   pairs all complete, `ui: N shared GUI image(s) of 2056x2216`, captures not refused for a size mismatch.
3. **Sharpness**: `ETERNALVR_CAPTURE_EYES=<dir>,120` in two runs, `ETERNALVR_RENDER_SIZE=off` with the old
   1415 x 1440 window (`ETERNALVR_WINDOW=0,0,1415,1440`) and the default; compare the same view's L/R PNGs at
   100 % crop (edge sharpness of HUD-free geometry, text on world surfaces), and confirm the new eye PNGs are
   2056 x 2216.
4. **Fixed size**: `ETERNALVR_RENDER_SIZE=2064x2208`: the size is on before the first swapchain (no
   recreate).
5. **Frame rate**: `rates:` line (stereo ticks per second) against the 2064 x 2100 baseline; `window:` line
   shows no present waiting for the display.
6. **Menus**: title screen, pause menu and settings on the menu panel (16:9), the laser pointer's clicks land
   where it points (`menu:` move and click counts), the HUD quad in play is 16:9 with nothing cut.
7. **Resizing the mirror** by hand: presents stay successful (`suboptimal result(s) taken as success`
   counting), no recreate loop (one `vkCreateSwapchainKHR` per change at most), the eyes keep their size.
8. **Fail closed**: `ETERNALVR_GUARD_TEST_TRIP_MS=20000`: `render size off: the multiplayer guard ...`, the
   window goes back to `ETERNALVR_WINDOW`, the game recreates at the window's size and runs flat on the cinema
   screen.
9. **Known unknowns** [inferred]: the desktop mouse in the game's menus (the game may map real client pixels
   to the larger output; the VR pointer uses relative motion and is not affected); applying video settings in
   the game re-runs 0x1CC0980, which sizes the real window to the render size clamped to the monitor (the eyes
   keep the render size); a fullscreen switch (`r_fullscreen 1`) is not supported with the render size.

## 9. Live results (simulator runtime, virtual display, RTX 4080)

| Check (section 8) | Run | Result |
|---|---|---|
| 1. Default launch, `auto` | rs-r24 | the simulator recommends 1280 x 1400 per eye; the game's swapchain is recreated at 1280 x 1400 about 0.1 s after the runtime is known and scaled into the 1280 x 720 mirror on the virtual display; 135 stereo ticks per second, XR 90 frames and 90 new images per second, eye tags in sync, per-eye TAA picks in both eyes |
| 2. Size and projection | rs-r21, rs-r24 | eye image, UI swapchain and XR image all at the render size; the FOV line reports the render size |
| 3. Sharpness | rs-r19 to rs-r22 | 2048 x 2208 visibly sharper than 1416 x 1440 at 100 % crop (`tmp-vr/rs/sharpness-1416-vs-2048.png`) |
| 4. Fixed size | rs-r21 | `2056x2216` fitted to 2048 x 2208 by the runtime's limits; 109 stereo ticks per second, XR 90 |
| 6. UI band | rs-r20 | HUD, loading screen text and objectives inside the centred 16:9 band of the 1280 x 1400 `_gui` |
| 8. Fail closed | rs-r25 | the test trip turns the size off, the window goes back to `ETERNALVR_WINDOW`, the game recreates at the window's size and the headset shows the cinema screen |
| First create | rs-r23 | the game's first `vkCreateSwapchainKHR` on a new surface can return `VK_ERROR_INITIALIZATION_FAILED` once and succeed on its immediate retry, with or without present scaling; only two failed scaled creates in a row turn the size off |

Not yet checked live: the menu pointer on the 16:9 band (7 and the menu part of 6), resizing the mirror by hand,
and the headset (Virtual Desktop's recommendation is below the budget and is used as it is).

## 10. Drivers without usable present scaling (AMD)

Two cases from players' reports (October 2026), both fail closed to the window-sized render, which capped each
eye at the mirror window (often 33 to 47 % of the planned width):

| Case | What the log showed | Where it turns off |
|---|---|---|
| AMD Radeon RX 5000 and 6000 (RDNA1, RDNA2; an RX 6750 XT) | no `VK_KHR_swapchain_maintenance1` nor the EXT one: `no swapchain maintenance extension matches the instance's ...` (before 0.1.17: `device extension VK_KHR_swapchain_maintenance1 is not supported ...`) | at the game's first surface: `the game's device has no swapchain maintenance extension (present scaling)` |
| NVIDIA driver 581.80 (RTX 3070 Ti; fixed in 0.1.17) | the device listed only `VK_EXT_swapchain_maintenance1` while the instance took `VK_KHR_surface_maintenance1`, so the layer looked for the KHR device name only | as above. The instance now takes both surface maintenance names when it can, and the device the KHR swapchain one, else the EXT one; `ETERNALVR_TEST_HIDE_KHR_MAINTENANCE=1` (a test knob) runs this path on a driver that lists both |
| AMD RDNA3.5 (ROG Ally, Radeon 890M) | the extension exists, but the scaled image range is the window's own size: `present scaling 0x5, gravity 0x0, scaled image 672x701 to 672x701` | `the render size is outside the surface's scaled image range` |

What the layer does now (`window_cap.hpp`, `features/render_size/render_cap.hpp`):

- The off line says how large each eye is against the plan and why:
  `size: render size off: <why>; the game renders at its window's size; each eye renders at the window's 652x713, 50% of the planned 1280x1400 (a driver limit: the graphics driver cannot scale presented images)`.
  For the range case the limit reads `the driver scales presented images only within 672x701 to 672x701; the window is not re-placed for this`.
- `queryScaling` logs each present mode's range and the surface's raw extents:
  `size: present mode 2: scaling 0x7, gravity x 0x7 y 0x7, scaled image 1x1 to 4294967294x4294967294; surface current 658x720, min 658x720, max 658x720`.
- Device create logs the GPU and driver once (`GPU vendor 0x10de device 0x2704, driver version 0x9a170000, driver 'NVIDIA' '616.92'`),
  which swapchain maintenance names the device lists, and when the instance has no surface maintenance extension.
- **The no-extension case is known at device create**, before the game's window is placed, so the window is placed
  at the largest client area of the planned eye's shape that the mirror display's work area allows (frame included,
  centred, never larger than the planned eye):
  `size: no present scaling on the game's device, so each eye renders at the window's size: the window takes the largest eye-shaped client area the work area 0,0 1280x752 allows, 652x713 at 314,31 (planned eye 1280x1400)`.
  `r_windowWidth` and `r_windowHeight` are then held at that window's real client area
  (`cvars: r_windowWidth is held at the placed window's 652, not 1280 (no present scaling)`), which the game already
  reads, so nothing is written and the game does not resize its window (holding the launch size resized it, which
  an AMD driver answered with `VK_ERROR_OUT_OF_DATE_KHR`; `runtime_cvars.cpp`). Not with `ETERNALVR_MIRROR_SIZE=fill`, nor for `auto` before the
  runtime's size is known (the window then goes where the mirror goes).
- The range case is known only once the game's surface exists; the window is not re-placed for it (a mid-session
  resize is what froze AMD drivers), only logged.
- The status file gets `render_planned` and `render_capped`; the launcher compares `render` with `render_planned`
  after the session and warns, and its "Each eye" line shows the real size while the last session was capped.
- `ETERNALVR_TEST_NO_PRESENT_SCALING=1` (a test knob) leaves the swapchain maintenance extension out so the fallback
  runs on any driver.

Not done: the instance's surface maintenance extension is still chosen by `vkCreateInstance` succeeding (the loader
can drop an extension the driver lacks without failing); the device's own list and the raw scaling values in the log
show when that happened. A virtual swapchain (section 1, option (a)) remains the full fix for both cases.

Rig runs (simulator, RTX 4080, the virtual display alone at 0,0 1280x800, work area 1280x752):

| Run | Result |
|---|---|
| aw2, `ETERNALVR_TEST_NO_PRESENT_SCALING=1`, render size 1280x1400 | window placed at 314,31 with client 652x713 (the mirror's eye shape would have been 658x720, whose frame does not fit the work area); one `vkCreateSwapchainKHR` at 652x713; XR swapchain 1304x713 (two eyes of 652x713); `render_capped=1`; the cvars read 652x713 already (no write); no present errors |
| aw3, without the knob | unchanged: render size on at 1280x1400 scaled into the 658x720 mirror; `render_capped=0` |
